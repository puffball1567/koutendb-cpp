#include <koutendb/tcp.hpp>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <sodium.h>
#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>

namespace koutendb::tcp {
namespace {
using Clock = std::chrono::steady_clock;
using Socket = asio::ip::tcp;
constexpr std::size_t header_limit = 8192;
constexpr std::size_t frame_limit = 64 * 1024 * 1024;
[[noreturn]] void protocol() { throw Error(ErrorKind::protocol, "Invalid wire response"); }

std::vector<std::string> split(const std::string& value, char separator = ' ') {
  std::vector<std::string> result;
  std::size_t start = 0;
  for (;;) {
    auto pos = value.find(separator, start);
    result.push_back(value.substr(start, pos == std::string::npos ? pos : pos - start));
    if (pos == std::string::npos) return result;
    start = pos + 1;
  }
}

std::uint64_t number(const std::string& text, std::uint64_t maximum) {
  std::uint64_t result = 0;
  if (text.empty() || text.size() > 20) protocol();
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result > maximum) protocol();
  return result;
}

double coordinate(const std::string& text) {
  if (text.empty() || text.find_first_not_of("0123456789+-.eE") != std::string::npos) protocol();
  std::istringstream input(text);
  input.imbue(std::locale::classic());
  double value = 0;
  input >> value;
  if (!input || input.peek() != EOF || !std::isfinite(value)) protocol();
  return value;
}

void expect(const std::vector<std::string>& p, const char* tag, std::size_t n, bool auth = false) {
  if (!p.empty() && p[0] == "ERR")
    throw Error(auth ? ErrorKind::authentication : ErrorKind::server,
                auth ? "Authentication or galaxy rejected" : "Server rejected request");
  if (p.size() != n || p[0] != tag) protocol();
}

void codec(const std::string& c) {
  if (c != "raw" && c != "json" && c != "nif" && c != "bif") protocol();
}

Clock::time_point deadline(double seconds) {
  return Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
}

struct Key {
  std::array<unsigned char, crypto_secretbox_KEYBYTES> bytes{};
  explicit Key(const std::string& seed) {
    if (crypto_generichash(bytes.data(), bytes.size(),
          reinterpret_cast<const unsigned char*>(seed.data()), seed.size(), nullptr, 0) != 0)
      throw Error(ErrorKind::protocol, "Key derivation failed");
  }
  ~Key() { sodium_memzero(bytes.data(), bytes.size()); }
};

std::string seal(const Key& key, const std::string& value) {
  std::string result(crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + value.size(), '\0');
  auto* bytes = reinterpret_cast<unsigned char*>(result.data());
  randombytes_buf(bytes, crypto_secretbox_NONCEBYTES);
  if (crypto_secretbox_easy(bytes + crypto_secretbox_NONCEBYTES,
        reinterpret_cast<const unsigned char*>(value.data()), value.size(), bytes, key.bytes.data()) != 0) protocol();
  return result;
}

std::string unseal(const Key& key, const std::string& value) {
  if (value.size() < 40) protocol();
  std::string result(value.size() - 40, '\0');
  const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
  if (crypto_secretbox_open_easy(reinterpret_cast<unsigned char*>(result.data()), bytes + 24,
        value.size() - 24, bytes, key.bytes.data()) != 0)
    throw Error(ErrorKind::protocol, "Encrypted frame authentication failed");
  return result;
}

std::pair<std::string, std::string> endpoint(const std::string& peer) {
  auto colon = peer.rfind(':');
  if (colon == std::string::npos || colon == 0) throw std::invalid_argument("Invalid TCP peer");
  auto host = peer.substr(0, colon);
  if (host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
  if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-:") != std::string::npos)
    throw std::invalid_argument("Invalid TCP peer");
  auto port = peer.substr(colon + 1);
  try { if (number(port, 65535) == 0) protocol(); }
  catch (const Error&) { throw std::invalid_argument("Invalid TCP port"); }
  return {host, port};
}

class Connection {
  asio::io_context io_;
  asio::ssl::context tls_context_{asio::ssl::context::tls_client};
  asio::ssl::stream<Socket::socket> stream_{io_, tls_context_};
  const Options& o_;
  std::unique_ptr<Key> key_;
  std::string plain_;
  std::size_t offset_ = 0;
  Clock::time_point read_deadline_;

  template<class Start, class Cancel>
  void timed(Clock::time_point until, Start start, Cancel cancel) {
    if (Clock::now() >= until) throw Error(ErrorKind::timeout, "TCP operation timed out");
    io_.restart();
    asio::steady_timer timer(io_, until);
    asio::error_code result;
    bool expired = false;
    timer.async_wait([&](asio::error_code ec) { if (!ec) { expired = true; cancel(); } });
    start([&](asio::error_code ec) { result = ec; timer.cancel(); });
    io_.run();
    if (expired) throw Error(ErrorKind::timeout, "TCP operation timed out");
    if (result) throw Error(ErrorKind::connection, "TCP connection failed");
  }

  template<class Start>
  void timed(Clock::time_point until, Start start) {
    timed(until, start, [&] { asio::error_code ignored; stream_.next_layer().cancel(ignored); stream_.next_layer().close(ignored); });
  }

  std::string raw(std::size_t n) {
    std::string result(n, '\0');
    if (n == 0) return result;
    timed(read_deadline_, [&](auto done) {
      auto handler = [done](asio::error_code ec, std::size_t) { done(ec); };
      if (o_.tls) asio::async_read(stream_, asio::buffer(result), handler);
      else asio::async_read(stream_.next_layer(), asio::buffer(result), handler);
    });
    return result;
  }

  template<class Read>
  std::vector<std::string> line(Read read) {
    std::string value;
    for (;;) {
      auto c = read(1)[0];
      if (c == '\n') break;
      if (value.size() >= header_limit) protocol();
      value += c;
    }
    if (!value.empty() && value.back() == '\r') value.pop_back();
    if (value.empty()) protocol();
    for (unsigned char c : value) if (c < 32 || c > 126) protocol();
    return split(value);
  }

  std::vector<std::string> exchange(const std::string& header) { send(header); return this->header(); }

public:
  Connection(const std::string& peer, const Options& o) : o_(o) {
    auto address = endpoint(peer);
    Socket::resolver resolver(io_);
    Socket::resolver::results_type endpoints;
    const auto connect_deadline = deadline(o.timeout);
    timed(connect_deadline, [&](auto done) {
      resolver.async_resolve(address.first, address.second,
        [&, done](asio::error_code ec, Socket::resolver::results_type results) { endpoints = std::move(results); done(ec); });
    }, [&] { resolver.cancel(); });
    timed(connect_deadline, [&](auto done) {
      asio::async_connect(stream_.next_layer(), endpoints,
        [done](asio::error_code ec, const Socket::endpoint&) { done(ec); });
    });
    stream_.next_layer().set_option(Socket::no_delay(true));
    if (o.tls) {
      if (!SSL_set_min_proto_version(stream_.native_handle(), TLS1_2_VERSION)) protocol();
      const auto name = o.tls_server_name.empty() ? address.first : o.tls_server_name;
      if (!SSL_set_tlsext_host_name(stream_.native_handle(), name.c_str())) protocol();
      if (o.tls_insecure_skip_verify) stream_.set_verify_mode(asio::ssl::verify_none);
      else {
        // The SSL object already exists; load trust into its shared context.
        if (o.tls_ca_file.empty()) tls_context_.set_default_verify_paths();
        else tls_context_.load_verify_file(o.tls_ca_file);
        stream_.set_verify_mode(asio::ssl::verify_peer);
        stream_.set_verify_callback(asio::ssl::host_name_verification(name));
      }
      timed(connect_deadline, [&](auto done) { stream_.async_handshake(asio::ssl::stream_base::client, done); });
    }
    if (!o.username.empty()) {
      if (o.secret_key.empty()) expect(exchange("AUTH " + o.username + " " + o.password), "OK", 2, true);
      else {
        auto challenge = exchange("AUTHCHAL " + o.username);
        expect(challenge, "CHAL", 2, true);
        if (challenge[1].size() != 64 || challenge[1].find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) protocol();
        constexpr char box_domain[] = "koutendb-auth-v1\0box\0";
        Key box(std::string(box_domain, sizeof(box_domain) - 1) + o.secret_key);
        auto message = "koutendb-auth-v1\n" + o.username + "\n" + o.password + "\n" + challenge[1];
        auto cipher = seal(box, message);
        sodium_memzero(message.data(), message.size());
        std::string hex(cipher.size() * 2 + 1, '\0');
        sodium_bin2hex(hex.data(), hex.size(), reinterpret_cast<const unsigned char*>(cipher.data()), cipher.size());
        hex.pop_back();
        expect(exchange("AUTHRESP " + hex), "OK", 2, true);
        constexpr char transport_domain[] = "koutendb-auth-v1\0transport\0";
        key_ = std::make_unique<Key>(std::string(transport_domain, sizeof(transport_domain) - 1) + challenge[1] + '\0' + o.secret_key);
      }
    }
    if (!o.galaxy.empty()) expect(exchange("HELLO " + o.galaxy), "OK", 2, true);
    auto version = exchange("WIREVER");
    expect(version, "WIREVER", 2, true);
    if (version[1] != "1") throw Error(ErrorKind::version_mismatch, "Unsupported KoutenDB wire version");
    auto ack = exchange("CODECMETA ON");
    expect(ack, "OK", 2);
    if (ack[1] != "codec-metadata") protocol();
  }

  void send(const std::string& header, const std::string& body = {}, bool* attempted = nullptr) {
    if (header.size() > header_limit || header.find_first_of("\r\n") != std::string::npos || body.size() > o_.max_frame_bytes)
      throw std::invalid_argument("Invalid request or size");
    auto frame = header + '\n' + body;
    if (key_) { frame = seal(*key_, frame); frame = "SEC " + std::to_string(frame.size()) + '\n' + frame; }
    timed(deadline(o_.write_timeout), [&](auto done) {
      if (attempted) *attempted = true;
      auto handler = [done](asio::error_code ec, std::size_t) { done(ec); };
      if (o_.tls) asio::async_write(stream_, asio::buffer(frame), handler);
      else asio::async_write(stream_.next_layer(), asio::buffer(frame), handler);
    });
    read_deadline_ = deadline(o_.read_timeout);
  }

  std::string read(std::size_t n) {
    if (!key_) return raw(n);
    while (plain_.size() - offset_ < n) {
      auto p = line([&](auto count) { return raw(count); });
      expect(p, "SEC", 2);
      auto value = unseal(*key_, raw(number(p[1], o_.max_frame_bytes + header_limit + 41)));
      if (plain_.size() - offset_ + value.size() > o_.max_frame_bytes + header_limit + 1) protocol();
      plain_ = plain_.substr(offset_) + value;
      offset_ = 0;
    }
    auto result = plain_.substr(offset_, n);
    offset_ += n;
    return result;
  }

  std::vector<std::string> header() { return line([&](auto n) { return read(n); }); }
};
} // namespace

Id Id::parse(const std::string& value) {
  auto p = split(value, ':');
  if (p.size() != 6) protocol();
  Id result{number(p[0], UINT64_MAX), static_cast<std::uint32_t>(number(p[1], UINT32_MAX)),
    static_cast<std::uint32_t>(number(p[2], UINT32_MAX)), coordinate(p[3]), coordinate(p[4]), coordinate(p[5])};
  if (result.period <= 0) protocol();
  return result;
}

std::string Id::str() const {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << parent << ':' << epoch << ':' << seq << ':' << std::setprecision(17) << t_write << ':' << period << ':' << head;
  const auto value = out.str();
  parse(value);
  return value;
}

struct Client::Impl {
  std::vector<std::string> peers;
  Options options;
  std::map<unsigned, std::unique_ptr<Connection>> connections;
  std::mutex mutex;
  bool closed = false;

  Impl(std::vector<std::string> p, Options o) : peers(std::move(p)), options(std::move(o)) {
    if (sodium_init() < 0) throw Error(ErrorKind::protocol, "Crypto initialization failed");
    if (peers.empty() || peers.size() > 64) throw std::invalid_argument("Provide 1..64 ordered peers");
    for (const auto& peer : peers) endpoint(peer);
    for (double t : {options.timeout, options.read_timeout, options.write_timeout})
      if (!std::isfinite(t) || t <= 0 || t > 3600) throw std::invalid_argument("Invalid timeout");
    if (!options.max_frame_bytes || options.max_frame_bytes > frame_limit || options.max_redirects > 32)
      throw std::invalid_argument("Invalid TCP limits");
    if (options.username.empty() && !options.auth_token.empty()) {
      options.username = "token"; options.password = options.auth_token;
    }
    for (const auto* value : {&options.username, &options.password, &options.galaxy}) {
      if (value->size() > 1024) throw std::invalid_argument("Authentication field exceeds limit");
      for (unsigned char c : *value) if (c <= 32 || c == 127) throw std::invalid_argument("Invalid authentication field");
    }
    if (options.username.empty() && (!options.password.empty() || !options.secret_key.empty()))
      throw std::invalid_argument("Authentication requires username");
    if (!options.tls && (!options.tls_ca_file.empty() || !options.tls_server_name.empty() || options.tls_insecure_skip_verify))
      throw std::invalid_argument("TLS options require tls=true");
    if (options.tls_server_name.find('\0') != std::string::npos || options.tls_ca_file.find('\0') != std::string::npos)
      throw std::invalid_argument("Invalid TLS configuration");
    connection(0);
  }

  ~Impl() {
    connections.clear();
    for (auto* s : {&options.password, &options.auth_token, &options.secret_key}) sodium_memzero(s->data(), s->size());
  }

  Connection& connection(unsigned node) {
    if (closed) throw Error(ErrorKind::connection, "TCP client is closed");
    if (node >= peers.size()) protocol();
    if (!connections.count(node)) {
      try { connections.emplace(node, std::make_unique<Connection>(peers[node], options)); }
      catch (const asio::system_error&) { throw Error(ErrorKind::connection, "Unable to connect or configure TLS"); }
    }
    return *connections.at(node);
  }

  template<class Operation>
  auto retry(Operation op) -> decltype(op()) {
    for (unsigned attempt = 0;; ++attempt) {
      try { return op(); }
      catch (const Error& error) {
        connections.clear();
        if (closed || !options.retry_reads || attempt ||
            (error.kind() != ErrorKind::connection && error.kind() != ErrorKind::timeout)) throw;
      }
    }
  }

  std::optional<Payload> read(const Id& original, const std::optional<std::string>& selection) {
    const std::string body = selection.value_or("");
    if (body.size() > options.max_frame_bytes) throw std::invalid_argument("Selection exceeds limit");
    original.str();
    std::lock_guard<std::mutex> guard(mutex);
    return retry([&]() -> std::optional<Payload> {
      auto id = original;
      unsigned node = 0;
      for (unsigned redirects = 0;; ++redirects) {
        auto& c = connection(node);
        auto fields = id.str();
        std::replace(fields.begin(), fields.end(), ':', ' ');
        c.send(selection ? "QRYID " + fields + ' ' + std::to_string(body.size()) : "GETID " + fields, body);
        auto p = c.header();
        if (p[0] == "MISS" || p[0] == "GONE") { expect(p, p[0].c_str(), 1); return std::nullopt; }
        if (p[0] == "FWD") {
          if ((p.size() != 7 && p.size() != 8) || redirects >= options.max_redirects) protocol();
          id = Id::parse(p[1]+':'+p[2]+':'+p[3]+':'+p[4]+':'+p[5]+':'+p[6]);
          if (p.size() == 8) node = static_cast<unsigned>(number(p[7], peers.size()-1));
          continue;
        }
        expect(p, "VAL", 4);
        number(p[1], peers.size()-1);
        codec(p[3]);
        return Payload{c.read(number(p[2], options.max_frame_bytes)), p[3]};
      }
    });
  }
};

Client::Client(std::vector<std::string> peers, Options options) : impl_(std::make_unique<Impl>(std::move(peers), std::move(options))) {}
Client::~Client() = default;
void Client::close() { std::lock_guard<std::mutex> guard(impl_->mutex); impl_->closed = true; impl_->connections.clear(); }

Id Client::put(const std::string& ring, const std::string& payload, const std::string& type) {
  codec(type);
  if (ring.empty() || ring.size() > impl_->options.max_frame_bytes || payload.size() > impl_->options.max_frame_bytes - ring.size())
    throw std::invalid_argument("Invalid ring or payload size");
  std::lock_guard<std::mutex> guard(impl_->mutex);
  bool attempted = false;
  try {
    auto& c = impl_->connection(0);
    c.send("PUTR " + std::to_string(ring.size()) + ' ' + std::to_string(payload.size()) + " 0 " + type, ring + payload, &attempted);
    auto p = c.header(); expect(p, "ID", 7);
    return Id::parse(p[1]+':'+p[2]+':'+p[3]+':'+p[4]+':'+p[5]+':'+p[6]);
  } catch (const Error& error) {
    impl_->connections.clear();
    if (attempted && (error.kind() == ErrorKind::connection || error.kind() == ErrorKind::timeout || error.kind() == ErrorKind::protocol))
      throw Error(ErrorKind::indeterminate_write, "Write outcome unknown; do not automatically retry");
    throw;
  }
}

Id Client::put_json(const std::string& ring, const nlohmann::json& value) { return put(ring, value.dump(), "json"); }
std::optional<Payload> Client::get(const Id& id) { return impl_->read(id, std::nullopt); }
std::optional<nlohmann::json> Client::get_json(const Id& id) { auto p = get(id); return p ? std::make_optional(nlohmann::json::parse(p->bytes)) : std::nullopt; }
std::optional<Payload> Client::query(const Id& id, const std::string& selection) { return impl_->read(id, selection); }
std::string Client::health() {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->retry([&] {
    auto& c = impl_->connection(0); c.send("HEALTH"); auto p = c.header();
    if (p[0] == "ERR") throw Error(ErrorKind::server, "Health request rejected");
    if (p.size() < 2 || p[0] != "OK" || p[1].substr(0,5) != "node=") protocol();
    number(p[1].substr(5), UINT32_MAX);
    std::string result = p[1];
    for (std::size_t i = 2; i < p.size(); ++i) result += ' ' + p[i];
    return result;
  });
}
} // namespace koutendb::tcp

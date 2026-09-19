#include <koutendb/tcp.hpp>
#include <sodium.h>
#include <iostream>

using namespace koutendb::tcp;
using Json = nlohmann::json;

std::string decode(const std::string& text) {
  std::string bytes(text.size(), '\0'); std::size_t size = 0;
  if (sodium_base642bin(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size(), text.data(), text.size(), nullptr, &size, nullptr, sodium_base64_VARIANT_ORIGINAL))
    throw std::invalid_argument("Invalid base64");
  bytes.resize(size); return bytes;
}
std::string encode(const std::string& bytes) {
  std::string text(sodium_base64_ENCODED_LEN(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
  sodium_bin2base64(text.data(), text.size(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), sodium_base64_VARIANT_ORIGINAL);
  text.resize(text.find('\0')); return text;
}
const char* name(ErrorKind kind) {
  switch (kind) {
    case ErrorKind::connection: return "ConnectionException";
    case ErrorKind::timeout: return "ConnectionTimeoutException";
    case ErrorKind::authentication: return "AuthenticationException";
    case ErrorKind::protocol: return "ProtocolException";
    case ErrorKind::version_mismatch: return "VersionMismatchException";
    case ErrorKind::server: return "ServerException";
    case ErrorKind::indeterminate_write: return "IndeterminateWriteException";
  }
  return "Error";
}
int main() {
  std::unique_ptr<Client> db;
  for (std::string line; std::getline(std::cin, line);) {
    try {
      auto p = Json::parse(line); Json result;
      const auto op = p.at("op").get<std::string>();
      if (op == "connect") {
        db.reset(); Options o;
        o.timeout = p.value("timeout", 1.0); o.read_timeout = p.value("readTimeout", 1.0); o.write_timeout = p.value("writeTimeout", 1.0);
        auto c = p.value("options", Json::object());
        o.username = c.value("username", ""); o.password = c.value("password", "");
        o.auth_token = c.value("authToken", ""); o.secret_key = c.value("secretKey", ""); o.galaxy = c.value("galaxy", "");
        o.tls = c.value("tls", false); o.tls_ca_file = c.value("tlsCaFile", ""); o.tls_server_name = c.value("tlsServerName", "");
        o.tls_insecure_skip_verify = c.value("tlsInsecureSkipVerify", false);
        o.max_frame_bytes = c.value("maxFrameBytes", o.max_frame_bytes); o.max_redirects = c.value("maxRedirects", o.max_redirects);
        o.retry_reads = c.value("retryReads", o.retry_reads);
        o.timeout = c.value("timeout", o.timeout); o.read_timeout = c.value("readTimeout", o.read_timeout); o.write_timeout = c.value("writeTimeout", o.write_timeout);
        db = std::make_unique<Client>(p.at("peers").get<std::vector<std::string>>(), o); result = "connected";
      } else if (op == "health") result = db->health();
      else if (op == "close") { db->close(); result = "closed"; }
      else if (op == "debug") result = "Native TCP client";
      else if (op == "putJson") result = db->put_json(p.at("ring"), p.at("value")).str();
      else if (op == "put") result = db->put(p.at("ring"), decode(p.at("payload")), p.value("codec", "raw")).str();
      else if (op == "getJson") { auto v = db->get_json(Id::parse(p.at("id"))); result = v ? *v : Json(nullptr); }
      else if (op == "get") { auto v = db->get(Id::parse(p.at("id"))); result = v ? Json{{"payload", encode(v->bytes)}, {"codec", v->codec}} : Json(nullptr); }
      else if (op == "query") { auto v = db->query(Id::parse(p.at("id")), p.at("selection")); result = v ? Json::parse(v->bytes) : Json(nullptr); }
      else throw std::invalid_argument("Invalid adapter operation");
      std::cout << Json{{"ok", true}, {"result", result}} << std::endl;
    } catch (const Error& e) { std::cout << Json{{"ok", false}, {"error", name(e.kind())}, {"message", e.what()}} << std::endl; }
    catch (const std::exception&) { std::cout << Json{{"ok", false}, {"error", "InvalidArgument"}, {"message", "Invalid adapter input"}} << std::endl; }
  }
}

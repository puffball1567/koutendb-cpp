#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace koutendb::tcp {

enum class ErrorKind {
  connection, timeout, authentication, protocol, version_mismatch,
  server, indeterminate_write
};

class Error : public std::runtime_error {
public:
  Error(ErrorKind kind, const char* message) : std::runtime_error(message), kind_(kind) {}
  ErrorKind kind() const noexcept { return kind_; }
private:
  ErrorKind kind_;
};

struct Id {
  std::uint64_t parent = 0;
  std::uint32_t epoch = 0, seq = 0;
  double t_write = 0, period = 0, head = 0;
  static Id parse(const std::string& value);
  std::string str() const;
};

struct Options {
  double timeout = 3, read_timeout = 5, write_timeout = 5;
  std::size_t max_frame_bytes = 64 * 1024 * 1024;
  unsigned max_redirects = 8;
  bool retry_reads = true;
  std::string username, password, auth_token, secret_key, galaxy;
  bool tls = false, tls_insecure_skip_verify = false;
  std::string tls_ca_file, tls_server_name;
};

struct Payload {
  std::string bytes;
  std::string codec;
};

// Binary payloads use std::string with explicit size, including embedded NUL.
// A client serializes operations and owns its persistent connections.
class Client {
public:
  explicit Client(std::vector<std::string> peers, Options options = {});
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  void close();
  Id put(const std::string& ring, const std::string& payload,
         const std::string& codec = "raw");
  Id put_json(const std::string& ring, const nlohmann::json& value);
  std::optional<Payload> get(const Id& id);
  std::optional<nlohmann::json> get_json(const Id& id);
  std::optional<Payload> query(const Id& id, const std::string& selection);
  std::string health();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace koutendb::tcp

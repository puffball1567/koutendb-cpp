// Connect to a TLS-enabled koutend over the cluster wire protocol.
//
// Requires an KoutenDB core built with -d:ssl. Run against a TLS listener,
// e.g. the one from the core's scripts/cluster_tls_smoke.sh. Configure the
// connection through environment variables:
//
//   KOUTEN_PEERS=127.0.0.1:17651 KOUTEN_TLS_CA=/path/server.crt \
//     ./koutendb_cpp_cluster_tls
#include <cstdlib>
#include <iostream>
#include <string>

#include "koutendb/koutendb.hpp"

namespace {
std::string envOr(const char* key, const std::string& fallback) {
  const char* value = std::getenv(key);
  return value ? std::string(value) : fallback;
}
}  // namespace

int main() {
  const std::string peers = envOr("KOUTEN_PEERS", "127.0.0.1:17651");
  const std::string caFile = envOr("KOUTEN_TLS_CA", "");
  const std::string serverName = envOr("KOUTEN_TLS_SERVER_NAME", "");
  // A CA file keeps certificate verification on, which is the right way to
  // reach a server with a private CA or self-signed certificate. Set
  // KOUTEN_TLS_INSECURE only for local smoke tests.
  const bool insecure = std::getenv("KOUTEN_TLS_INSECURE") != nullptr;

  try {
    auto db = koutendb::Db::connectAuthTls(
        peers, "alice", "secret", "", "shared-secret", "", caFile, serverName,
        insecure);
    koutendb::Id id =
        db.putJson("secure/demo", R"({"title":"tls smoke","ok":true})");
    std::cout << "id=" << id.parent << ":" << id.epoch << ":" << id.seq << ":"
              << id.t_write << "\n";
    return 0;
  } catch (const koutendb::Error& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}

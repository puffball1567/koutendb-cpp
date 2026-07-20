// Parametrized security probe: build a connection from ORBP_* environment
// variables, attempt a put, print SUCCESS or REJECT:<msg>. Used by the
// cross-driver security matrix harness.
#include <cstdlib>
#include <iostream>
#include <string>

#include "koutendb/koutendb.hpp"

namespace {
std::string env(const char* key) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string();
}
bool flag(const char* key) { return env(key) == "1"; }
}  // namespace

int main() {
  const std::string peers = env("ORBP_PEERS");
  const std::string user = env("ORBP_USER");
  const std::string pass = env("ORBP_PASS");
  const std::string secret = env("ORBP_SECRET");
  const std::string ca = env("ORBP_CA");
  const std::string sni = env("ORBP_SNI");
  const bool tls = flag("ORBP_TLS") || !ca.empty() || !sni.empty() || flag("ORBP_INSECURE");
  try {
    koutendb::Db db =
        tls ? koutendb::Db::connectAuthTls(peers, user, pass, "", secret, "",
                                             ca, sni, flag("ORBP_INSECURE"))
            : koutendb::Db::connectAuth(peers, user, pass, "", secret, "");
    db.putJson("secure/demo", R"({"probe":1})");
    std::cout << "SUCCESS\n";
    return 0;
  } catch (const koutendb::Error& e) {
    std::cout << "REJECT:" << std::string(e.what()).substr(0, 50) << "\n";
    return 0;
  }
}

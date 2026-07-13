#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "rochedb/rochedb.hpp"

int main() {
  assert(rochedb::abiVersion() == 2);

  auto db = rochedb::Db::open(4);
  db.configureRing("docs", 45.0);
  db.setGalaxyDescription("Smoke-test galaxy for C++ binding.");
  db.setRingDescription("docs", "Documents used by the C++ binding smoke test.");

  rochedb::Id first = db.putJson("docs", R"({"title":"alpha","body":"hello"})");
  rochedb::Id second =
      db.putJsonVec("docs", R"({"title":"beta","body":"vector"})",
                    std::vector<float>{0.9f, 0.1f, 0.2f});
  std::vector<std::uint8_t> bif{0x42, 0x49, 0x46, 0x00, 0x01};
  rochedb::Id bifId = db.putBifVec("docs/bif", bif,
                                   std::vector<float>{0.1f, 0.2f, 0.9f});

  auto got = db.getString(first);
  assert(got.has_value());
  assert(got->find("alpha") != std::string::npos);

  auto encoded = db.getEncoded(first);
  assert(encoded.has_value());
  assert(encoded->codec == rochedb::PayloadCodec::Json);
  assert(std::string(encoded->payload.begin(), encoded->payload.end())
             .find("alpha") != std::string::npos);

  auto encodedBif = db.getEncoded(bifId);
  assert(encodedBif.has_value());
  assert(encodedBif->codec == rochedb::PayloadCodec::Bif);
  assert(encodedBif->payload == bif);

  auto projected = db.queryString(first, "{ title }");
  assert(projected.has_value());
  assert(*projected == R"({"title":"alpha"})");

  std::string page = db.readRingJson("docs", R"({"title":"alpha"})",
                                     "{ title }", 1);
  assert(page.find(R"("items")") != std::string::npos);
  assert(page.find(R"("codec":"json")") != std::string::npos);
  assert(page.find("alpha") != std::string::npos);

  std::string bifPage = db.readRingJson("docs/bif", "{}", "", 1);
  assert(bifPage.find(R"("codec":"bif")") != std::string::npos);
  assert(bifPage.find(R"("encoding":"base64")") != std::string::npos);

  auto batch = db.batchGet(std::vector<rochedb::Id>{first, second});
  assert(batch.size() == 2);
  assert(batch[1].has_value());

  auto result = db.retrieve(std::vector<float>{1.0f, 0.0f, 0.0f}, "docs", 5, 50, 3);
  assert(result.returned >= 1);
  assert(result.scanned >= result.returned);

  std::string atlas = db.atlas(std::vector<float>{1.0f, 0.0f, 0.0f});
  assert(atlas.find("galaxyMap") != std::string::npos);
  assert(atlas.find("Documents used by the C++ binding smoke test.") !=
         std::string::npos);

  int located = db.locate(first);
  assert(located >= 0);
  assert(db.nextVisit(first, located) >= 0.0);
  assert(db.nextJoin(first, second) >= -1.0);

  std::cout << "C++ driver OK\n";
  return 0;
}

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "koutendb/koutendb.hpp"

int main() {
  assert(koutendb::abiVersion() == 2);

  auto db = koutendb::Db::open(4);
  db.configureRing("docs", 45.0);
  db.setGalaxyDescription("Smoke-test galaxy for C++ binding.");
  db.setRingDescription("docs", "Documents used by the C++ binding smoke test.");

  koutendb::Id first = db.putJson("docs", R"({"title":"alpha","body":"hello"})");
  koutendb::Id second =
      db.putJsonVec("docs", R"({"title":"beta","body":"vector"})",
                    std::vector<float>{0.9f, 0.1f, 0.2f});
  std::vector<std::uint8_t> bif{0x42, 0x49, 0x46, 0x00, 0x01};
  koutendb::Id bifId = db.putBifVec("docs/bif", bif,
                                   std::vector<float>{0.1f, 0.2f, 0.9f});

  auto got = db.getString(first);
  assert(got.has_value());
  assert(got->find("alpha") != std::string::npos);

  auto encoded = db.getEncoded(first);
  assert(encoded.has_value());
  assert(encoded->codec == koutendb::PayloadCodec::Json);
  assert(std::string(encoded->payload.begin(), encoded->payload.end())
             .find("alpha") != std::string::npos);

  auto encodedBif = db.getEncoded(bifId);
  assert(encodedBif.has_value());
  assert(encodedBif->codec == koutendb::PayloadCodec::Bif);
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

  auto batch = db.batchGet(std::vector<koutendb::Id>{first, second});
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

  const auto nonce = std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count());
  const auto dataDir = std::filesystem::temp_directory_path() /
                       ("koutendb-cpp-v012-" + nonce);
  const auto checkpointRoot = std::filesystem::path(dataDir.string() + "-checkpoints");
  const auto restoredDir = std::filesystem::path(dataDir.string() + "-restored");
  std::filesystem::create_directories(dataDir);
  koutendb::OpenDirOptions openOptions;
  openOptions.nodes = 1;
  openOptions.strongDurability = true;
  openOptions.diskBacked = true;
  auto disk = koutendb::Db::openDir(dataDir.string(), openOptions);
  auto mutableId = disk.put("docs/mutable", "before");
  assert(disk.exists(mutableId));
  disk.updateJson(mutableId, R"({"state":"after"})");
  assert(disk.getEncoded(mutableId)->codec == koutendb::PayloadCodec::Json);
  assert(disk.metrics(koutendb::MetricsFormat::Prometheus).find("koutendb_items") !=
         std::string::npos);
  koutendb::SegmentMaintenancePolicy policy;
  policy.staleRatio = 0.0;
  policy.minStaleRecords = 0;
  policy.maxRings = 1;
  policy.maxBytes = 1024 * 1024;
  policy.maxElapsedMs = 1000;
  assert(disk.planSegmentMaintenance(policy).find("decisions") != std::string::npos);
  assert(disk.runSegmentMaintenance(policy).find("decisions") != std::string::npos);
  assert(disk.segmentStatus(0.0, 0).find("rings") != std::string::npos);
  assert(!disk.recoverSegmentMaintenance());
  assert(disk.createCheckpoint(checkpointRoot.string(), "cpp-1")
             .find(R"("verified":true)") != std::string::npos);
  const auto checkpointDir = checkpointRoot / "cpp-1";
  assert(koutendb::Db::checkpointStatus(checkpointDir.string())
             .find(R"("reason":"verified")") != std::string::npos);
  assert(koutendb::Db::listCheckpoints(checkpointRoot.string())
             .find(R"("count":1)") != std::string::npos);
  assert(koutendb::Db::checkpointMetrics(checkpointRoot.string(),
                                         koutendb::MetricsFormat::OpenMetrics)
             .find("# EOF") != std::string::npos);
  disk.close();

  koutendb::Db::restoreCheckpoint(checkpointDir.string(), restoredDir.string());
  auto restored = koutendb::Db::openDir(restoredDir.string(), openOptions);
  assert(restored.exists(mutableId));
  restored.remove(mutableId);
  assert(!restored.exists(mutableId));
  restored.close();
  std::filesystem::remove_all(dataDir);
  std::filesystem::remove_all(checkpointRoot);
  std::filesystem::remove_all(restoredDir);

  std::cout << "C++ driver OK\n";
  return 0;
}

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "koutendb.h"

namespace koutendb {

using Id = kouten_id;

enum class PayloadCodec {
  Raw = KOUTEN_CODEC_RAW,
  Json = KOUTEN_CODEC_JSON,
  Nif = KOUTEN_CODEC_NIF,
  Bif = KOUTEN_CODEC_BIF,
};

enum class MetricsFormat {
  KeyValue = KOUTEN_METRICS_KEY_VALUE,
  Prometheus = KOUTEN_METRICS_PROMETHEUS,
  OpenMetrics = KOUTEN_METRICS_OPENMETRICS,
};

struct OpenDirOptions {
  int nodes = 8;
  bool strongDurability = false;
  bool diskBacked = false;
};

struct SegmentMaintenancePolicy {
  double staleRatio = 0.25;
  int minStaleRecords = 256;
  int maxRings = 0;
  std::int64_t maxBytes = 0;
  std::int64_t maxElapsedMs = 0;
};

struct EncodedPayload {
  std::vector<std::uint8_t> payload;
  PayloadCodec codec = PayloadCodec::Raw;
};

struct Hit {
  Id id{};
  double score = 0.0;
  std::vector<std::uint8_t> payload;
};

struct RetrieveResult {
  std::vector<Hit> hits;
  int totalVectors = 0;
  int scanned = 0;
  int skippedVectors = 0;
  int returned = 0;
  int ringsTouched = 0;
  int payloadBytes = 0;
  int estimatedTokens = 0;
  int fanoutNodes = 0;
  double candidateReduction = 0.0;
};

class Error : public std::runtime_error {
 public:
  explicit Error(const std::string& message) : std::runtime_error(message) {}
};

inline int abiVersion() { return kouten_abi_version(); }

inline int codecCode(PayloadCodec codec) {
  return static_cast<int>(codec);
}

inline PayloadCodec codecFromCode(int codec) {
  switch (codec) {
    case KOUTEN_CODEC_JSON:
      return PayloadCodec::Json;
    case KOUTEN_CODEC_NIF:
      return PayloadCodec::Nif;
    case KOUTEN_CODEC_BIF:
      return PayloadCodec::Bif;
    case KOUTEN_CODEC_RAW:
    default:
      return PayloadCodec::Raw;
  }
}

inline std::string lastError(const char* fallback) {
  const char* err = kouten_last_error();
  if (err != nullptr && err[0] != '\0') {
    return std::string(err);
  }
  return std::string(fallback);
}

inline std::vector<std::uint8_t> copyBytes(const void* data, std::size_t len) {
  std::vector<std::uint8_t> out(len);
  if (len > 0 && data != nullptr) {
    std::memcpy(out.data(), data, len);
  }
  return out;
}

inline std::string takeText(void* ptr, std::size_t len, const char* fallback) {
  if (ptr == nullptr) {
    throw Error(lastError(fallback));
  }
  std::string out(static_cast<char*>(ptr), len);
  kouten_free(ptr);
  return out;
}

class Db {
 public:
  Db() = default;

  static Db open(int nodes = 8) {
    kouten_init();
    return Db(kouten_open(nodes));
  }

  static Db openDir(std::string_view dir, int nodes = 8) {
    kouten_init();
    return Db(kouten_open_dir(nodes, std::string(dir).c_str()));
  }

  static Db openDir(std::string_view dir, const OpenDirOptions& options) {
    kouten_init();
    std::string path(dir);
    return Db(kouten_open_dir_options(options.nodes, path.c_str(),
                                      options.strongDurability ? 1 : 0,
                                      options.diskBacked ? 1 : 0));
  }

  static Db connect(std::string_view peers) {
    kouten_init();
    return Db(kouten_connect(std::string(peers).c_str()));
  }

  static Db connectAuth(std::string_view peers,
                        std::string_view username = {},
                        std::string_view password = {},
                        std::string_view authToken = {},
                        std::string_view secretKey = {},
                        std::string_view galaxy = {}) {
    kouten_init();
    std::string p(peers);
    std::string u(username);
    std::string pw(password);
    std::string token(authToken);
    std::string secret(secretKey);
    std::string g(galaxy);
    return Db(kouten_connect_auth(p.c_str(), u.c_str(), pw.c_str(), token.c_str(),
                                 secret.c_str(), g.c_str()));
  }

  // Authenticated cluster connection with TLS. Enabling TLS requires an
  // KoutenDB core built with -d:ssl.
  //
  // A tlsCaFile verifies the server against a CA or self-signed certificate PEM
  // with verification left on, which is the right way to reach a server with a
  // private CA or self-signed certificate.
  //
  // tlsInsecureSkipVerify disables certificate verification entirely. The
  // connection is then encrypted but unauthenticated and trivially
  // impersonable, so it is for local smoke tests only — never a production
  // server. Prefer a tlsCaFile for self-signed certificates.
  static Db connectAuthTls(std::string_view peers,
                           std::string_view username = {},
                           std::string_view password = {},
                           std::string_view authToken = {},
                           std::string_view secretKey = {},
                           std::string_view galaxy = {},
                           std::string_view tlsCaFile = {},
                           std::string_view tlsServerName = {},
                           bool tlsInsecureSkipVerify = false) {
    kouten_init();
    std::string p(peers);
    std::string u(username);
    std::string pw(password);
    std::string token(authToken);
    std::string secret(secretKey);
    std::string g(galaxy);
    std::string caFile(tlsCaFile);
    std::string serverName(tlsServerName);
    return Db(kouten_connect_auth_tls(p.c_str(), u.c_str(), pw.c_str(),
                                        token.c_str(), secret.c_str(), g.c_str(),
                                        1, caFile.c_str(), serverName.c_str(),
                                        tlsInsecureSkipVerify ? 1 : 0));
  }

  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;

  Db(Db&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

  Db& operator=(Db&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  ~Db() { close(); }

  void close() noexcept {
    if (handle_ != nullptr) {
      kouten_close(handle_);
      handle_ = nullptr;
    }
  }

  double now() const { return kouten_now(checked()); }

  void advance(double dt) { kouten_advance(checked(), dt); }

  void configureRing(std::string_view ring, double period) {
    std::string r(ring);
    if (kouten_ring_configure(checked(), r.c_str(), period) != KOUTEN_OK) {
      throw Error(lastError("failed to configure ring"));
    }
  }

  void setGalaxyDescription(std::string_view description) {
    std::string d(description);
    if (kouten_set_galaxy_description(checked(), d.c_str()) != KOUTEN_OK) {
      throw Error(lastError("failed to set galaxy description"));
    }
  }

  void setRingDescription(std::string_view ring, std::string_view description) {
    std::string r(ring);
    std::string d(description);
    if (kouten_set_ring_description(checked(), r.c_str(), d.c_str()) != KOUTEN_OK) {
      throw Error(lastError("failed to set ring description"));
    }
  }

  Id put(std::string_view ring, std::string_view payload) {
    return put(ring, reinterpret_cast<const std::uint8_t*>(payload.data()),
               payload.size());
  }

  Id put(std::string_view ring, const std::vector<std::uint8_t>& payload) {
    return put(ring, payload.data(), payload.size());
  }

  Id put(std::string_view ring, const std::uint8_t* data, std::size_t len) {
    Id id{};
    std::string r(ring);
    if (kouten_put(checked(), r.c_str(), data, len, &id) != KOUTEN_OK) {
      throw Error(lastError("put failed"));
    }
    return id;
  }

  Id putCodec(std::string_view ring, std::string_view payload,
              PayloadCodec codec) {
    return putCodec(ring, reinterpret_cast<const std::uint8_t*>(payload.data()),
                    payload.size(), codec);
  }

  Id putCodec(std::string_view ring, const std::vector<std::uint8_t>& payload,
              PayloadCodec codec) {
    return putCodec(ring, payload.data(), payload.size(), codec);
  }

  Id putCodec(std::string_view ring, const std::uint8_t* data, std::size_t len,
              PayloadCodec codec) {
    Id id{};
    std::string r(ring);
    if (kouten_put_codec(checked(), r.c_str(), data, len, codecCode(codec),
                        &id) != KOUTEN_OK) {
      throw Error(lastError("putCodec failed"));
    }
    return id;
  }

  Id putJson(std::string_view ring, std::string_view json) {
    return putCodec(ring, json, PayloadCodec::Json);
  }

  Id putNif(std::string_view ring, std::string_view nif) {
    return putCodec(ring, nif, PayloadCodec::Nif);
  }

  Id putBif(std::string_view ring, const std::vector<std::uint8_t>& bif) {
    return putCodec(ring, bif, PayloadCodec::Bif);
  }

  Id putVec(std::string_view ring, std::string_view payload,
            const std::vector<float>& vec) {
    return putVec(ring, reinterpret_cast<const std::uint8_t*>(payload.data()),
                  payload.size(), vec);
  }

  Id putVec(std::string_view ring, const std::uint8_t* data, std::size_t len,
            const std::vector<float>& vec) {
    Id id{};
    std::string r(ring);
    if (kouten_put_vec(checked(), r.c_str(), data, len, vec.data(), vec.size(),
                      &id) != KOUTEN_OK) {
      throw Error(lastError("putVec failed"));
    }
    return id;
  }

  Id putVecCodec(std::string_view ring, std::string_view payload,
                 const std::vector<float>& vec, PayloadCodec codec) {
    return putVecCodec(ring,
                       reinterpret_cast<const std::uint8_t*>(payload.data()),
                       payload.size(), vec, codec);
  }

  Id putVecCodec(std::string_view ring, const std::uint8_t* data,
                 std::size_t len, const std::vector<float>& vec,
                 PayloadCodec codec) {
    Id id{};
    std::string r(ring);
    if (kouten_put_vec_codec(checked(), r.c_str(), data, len, codecCode(codec),
                            vec.data(), vec.size(), &id) != KOUTEN_OK) {
      throw Error(lastError("putVecCodec failed"));
    }
    return id;
  }

  Id putJsonVec(std::string_view ring, std::string_view json,
                const std::vector<float>& vec) {
    return putVecCodec(ring, json, vec, PayloadCodec::Json);
  }

  Id putNifVec(std::string_view ring, std::string_view nif,
               const std::vector<float>& vec) {
    return putVecCodec(ring, nif, vec, PayloadCodec::Nif);
  }

  Id putBifVec(std::string_view ring, const std::vector<std::uint8_t>& bif,
               const std::vector<float>& vec) {
    return putVecCodec(ring, bif.data(), bif.size(), vec, PayloadCodec::Bif);
  }

  std::optional<std::vector<std::uint8_t>> get(Id id) const {
    std::size_t len = 0;
    void* ptr = kouten_get(checked(), id, &len);
    if (ptr == nullptr) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> out = copyBytes(ptr, len);
    kouten_free(ptr);
    return out;
  }

  std::optional<std::string> getString(Id id) const {
    auto bytes = get(id);
    if (!bytes.has_value()) {
      return std::nullopt;
    }
    return std::string(bytes->begin(), bytes->end());
  }

  std::optional<EncodedPayload> getEncoded(Id id) const {
    std::size_t len = 0;
    int codec = KOUTEN_CODEC_RAW;
    void* ptr = kouten_get_codec(checked(), id, &len, &codec);
    if (ptr == nullptr) {
      return std::nullopt;
    }
    EncodedPayload out{copyBytes(ptr, len), codecFromCode(codec)};
    kouten_free(ptr);
    return out;
  }

  bool exists(Id id) const {
    int result = kouten_exists(checked(), id);
    if (result < 0) {
      throw Error(lastError("exists failed"));
    }
    return result != 0;
  }

  void update(Id id, std::string_view payload) {
    if (kouten_update(checked(), id, payload.data(), payload.size()) != KOUTEN_OK) {
      throw Error(lastError("update failed"));
    }
  }

  void updateCodec(Id id, std::string_view payload, PayloadCodec codec) {
    if (kouten_update_codec(checked(), id, payload.data(), payload.size(),
                            codecCode(codec)) != KOUTEN_OK) {
      throw Error(lastError("updateCodec failed"));
    }
  }

  void updateJson(Id id, std::string_view json) {
    updateCodec(id, json, PayloadCodec::Json);
  }

  void remove(Id id) {
    if (kouten_remove(checked(), id) != KOUTEN_OK) {
      throw Error(lastError("remove failed"));
    }
  }

  std::vector<std::optional<std::vector<std::uint8_t>>> batchGet(
      const std::vector<Id>& ids) const {
    kouten_batch_result* result = kouten_batch_get(checked(), ids.data(), ids.size());
    if (result == nullptr) {
      throw Error(lastError("batchGet failed"));
    }
    std::unique_ptr<kouten_batch_result, decltype(&kouten_batch_get_free)> guard(
        result, kouten_batch_get_free);

    std::vector<std::optional<std::vector<std::uint8_t>>> out;
    out.reserve(result->len);
    for (std::size_t i = 0; i < result->len; ++i) {
      const kouten_value& value = result->values[i];
      if (value.data == nullptr) {
        out.push_back(std::nullopt);
      } else {
        out.push_back(copyBytes(value.data, value.len));
      }
    }
    return out;
  }

  std::optional<std::vector<std::uint8_t>> query(Id id,
                                                std::string_view selection) const {
    std::string s(selection);
    std::size_t len = 0;
    void* ptr = kouten_query(checked(), id, s.c_str(), &len);
    if (ptr == nullptr) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> out = copyBytes(ptr, len);
    kouten_free(ptr);
    return out;
  }

  std::optional<std::string> queryString(Id id, std::string_view selection) const {
    auto bytes = query(id, selection);
    if (!bytes.has_value()) {
      return std::nullopt;
    }
    return std::string(bytes->begin(), bytes->end());
  }

  std::string readRingJson(std::string_view ring,
                           std::string_view filterJson = "{}",
                           std::string_view selection = {},
                           int limit = 100,
                           std::string_view cursor = {},
                           bool pagination = false,
                           int page = 1,
                           int pageLimit = 20,
                           std::string_view sortField = {},
                           bool sortDesc = true) const {
    std::string r(ring);
    std::string f(filterJson);
    std::string s(selection);
    std::string c(cursor);
    std::string sort(sortField);
    std::size_t len = 0;
    void* ptr = kouten_read_ring_json(checked(), r.c_str(), f.c_str(), s.c_str(),
                                     limit, c.c_str(), pagination ? 1 : 0,
                                     page, pageLimit, sort.c_str(),
                                     sortDesc ? 1 : 0, &len);
    if (ptr == nullptr) {
      throw Error(lastError("readRingJson failed"));
    }
    std::string out(static_cast<char*>(ptr), len);
    kouten_free(ptr);
    return out;
  }

  RetrieveResult retrieve(const std::vector<float>& vec,
                          std::string_view ring = {},
                          int budget = 10,
                          int topRings = 50,
                          int focus = 3) const {
    std::string r(ring);
    kouten_retrieve_result* result =
        kouten_retrieve(checked(), vec.data(), vec.size(), r.c_str(), budget,
                       topRings, focus);
    if (result == nullptr) {
      throw Error(lastError("retrieve failed"));
    }
    std::unique_ptr<kouten_retrieve_result, decltype(&kouten_retrieve_free)> guard(
        result, kouten_retrieve_free);

    RetrieveResult out;
    out.totalVectors = result->total_vectors;
    out.scanned = result->scanned;
    out.skippedVectors = result->skipped_vectors;
    out.returned = result->returned;
    out.ringsTouched = result->rings_touched;
    out.payloadBytes = result->payload_bytes;
    out.estimatedTokens = result->estimated_tokens;
    out.fanoutNodes = result->fanout_nodes;
    out.candidateReduction = result->candidate_reduction;
    out.hits.reserve(result->len);
    for (std::size_t i = 0; i < result->len; ++i) {
      const kouten_hit& hit = result->hits[i];
      out.hits.push_back(Hit{hit.id, hit.score,
                             copyBytes(hit.payload, hit.payload_len)});
    }
    return out;
  }

  std::string atlas(const std::vector<float>& queryVec = {},
                    int maxCentroidDims = 8) const {
    std::size_t len = 0;
    void* ptr = kouten_atlas(checked(), queryVec.data(), queryVec.size(),
                            maxCentroidDims, &len);
    if (ptr == nullptr) {
      throw Error(lastError("atlas failed"));
    }
    std::string out(static_cast<char*>(ptr), len);
    kouten_free(ptr);
    return out;
  }

  std::string metrics(MetricsFormat format = MetricsFormat::KeyValue) const {
    std::size_t len = 0;
    void* ptr = kouten_metrics_text(checked(), static_cast<int>(format), &len);
    return takeText(ptr, len, "metrics failed");
  }

  std::string segmentStatus(double staleRatio = 0.25,
                            int minStaleRecords = 256) const {
    std::size_t len = 0;
    void* ptr = kouten_segment_status_json(checked(), staleRatio,
                                           minStaleRecords, &len);
    return takeText(ptr, len, "segmentStatus failed");
  }

  std::string planSegmentMaintenance(
      const SegmentMaintenancePolicy& policy = {}) const {
    return segmentMaintenance(policy, false);
  }

  std::string runSegmentMaintenance(
      const SegmentMaintenancePolicy& policy = {}) const {
    return segmentMaintenance(policy, true);
  }

  std::string segmentMaintenanceStatus() const {
    std::size_t len = 0;
    void* ptr = kouten_segment_maintenance_status_json(checked(), &len);
    return takeText(ptr, len, "segmentMaintenanceStatus failed");
  }

  bool recoverSegmentMaintenance() {
    int recovered = 0;
    if (kouten_segment_maintenance_recover(checked(), &recovered) != KOUTEN_OK) {
      throw Error(lastError("segment maintenance recovery failed"));
    }
    return recovered != 0;
  }

  std::string createCheckpoint(std::string_view root = {},
                               std::string_view checkpointId = {}) const {
    std::string r(root);
    std::string id(checkpointId);
    std::size_t len = 0;
    void* ptr = kouten_checkpoint_create_json(
        checked(), r.empty() ? nullptr : r.c_str(),
        id.empty() ? nullptr : id.c_str(), &len);
    return takeText(ptr, len, "checkpoint creation failed");
  }

  static std::string checkpointStatus(std::string_view checkpointDir) {
    kouten_init();
    std::string path(checkpointDir);
    std::size_t len = 0;
    void* ptr = kouten_checkpoint_status_json(path.c_str(), &len);
    return takeText(ptr, len, "checkpoint status failed");
  }

  static std::string listCheckpoints(std::string_view root) {
    kouten_init();
    std::string path(root);
    std::size_t len = 0;
    void* ptr = kouten_checkpoint_list_json(path.c_str(), &len);
    return takeText(ptr, len, "checkpoint list failed");
  }

  static std::string cleanupCheckpoints(std::string_view root, int keep) {
    kouten_init();
    std::string path(root);
    std::size_t len = 0;
    void* ptr = kouten_checkpoint_cleanup_json(path.c_str(), keep, &len);
    return takeText(ptr, len, "checkpoint cleanup failed");
  }

  static std::string restoreCheckpoint(std::string_view checkpointDir,
                                       std::string_view dataDir,
                                       bool overwrite = false) {
    kouten_init();
    std::string checkpoint(checkpointDir);
    std::string data(dataDir);
    std::size_t len = 0;
    void* ptr = kouten_checkpoint_restore_json(
        checkpoint.c_str(), data.c_str(), overwrite ? 1 : 0, &len);
    return takeText(ptr, len, "checkpoint restore failed");
  }

  static std::string checkpointMetrics(
      std::string_view root,
      MetricsFormat format = MetricsFormat::KeyValue) {
    kouten_init();
    std::string path(root);
    std::size_t len = 0;
    void* ptr = kouten_checkpoint_metrics_text(
        path.c_str(), static_cast<int>(format), &len);
    return takeText(ptr, len, "checkpoint metrics failed");
  }

  int locate(Id id, double at = -1.0) const {
    return kouten_locate(checked(), id, at);
  }

  double nextVisit(Id id, int node) const {
    return kouten_next_visit(checked(), id, node);
  }

  double nextJoin(Id a, Id b) const {
    return kouten_next_join(checked(), a, b);
  }

 private:
  std::string segmentMaintenance(const SegmentMaintenancePolicy& policy,
                                 bool run) const {
    std::size_t len = 0;
    void* ptr = run
        ? kouten_segment_maintenance_run_json(
              checked(), policy.staleRatio, policy.minStaleRecords,
              policy.maxRings, policy.maxBytes, policy.maxElapsedMs, &len)
        : kouten_segment_maintenance_plan_json(
              checked(), policy.staleRatio, policy.minStaleRecords,
              policy.maxRings, policy.maxBytes, policy.maxElapsedMs, &len);
    return takeText(ptr, len, run ? "segment maintenance run failed"
                                  : "segment maintenance plan failed");
  }

  explicit Db(void* handle) : handle_(handle) {
    if (handle_ == nullptr) {
      throw Error(lastError("failed to open KoutenDB"));
    }
  }

  void* checked() const {
    if (handle_ == nullptr) {
      throw Error("KoutenDB handle is closed");
    }
    return handle_;
  }

  void* handle_ = nullptr;
};

}  // namespace koutendb

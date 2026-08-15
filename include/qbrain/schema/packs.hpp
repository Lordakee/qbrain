#pragma once
#include "qbrain/core/brain.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::schema {

class PackError final : public std::runtime_error {
 public:
  PackError(std::string code, std::string field, std::string message);

  const std::string& code() const noexcept { return code_; }
  const std::string& field() const noexcept { return field_; }

 private:
  std::string code_;
  std::string field_;
};

struct PackManifest {
  std::string id;
  std::string name;
  std::optional<std::string> version;
  std::vector<std::string> types;
  std::vector<std::string> dimensions;
  std::optional<std::vector<std::string>> phases;
};

struct PackInfo {
  std::string id;
  std::string origin;
  bool active = false;
};

struct LoadedPack {
  std::string id;
  std::string origin;
  PackManifest manifest;
};

struct ReloadPackResult {
  std::string id;
  bool changed = false;
};

struct SchemaTypeCount {
  std::string type;
  int64_t count = 0;
};

struct SchemaStatsResult {
  std::string source_id;
  std::string active_pack_id;
  int schema_version = 0;
  int64_t total_active_pages = 0;
  std::vector<SchemaTypeCount> type_counts;
  bool truncated = false;
};

std::optional<std::string> canonical_pack_id(std::string_view id);
std::string manifest_json(const PackManifest& manifest);

std::vector<PackInfo> list_packs(Brain& brain);
std::string active_pack_id(Brain& brain);
LoadedPack load_pack(Brain& brain,
                     const std::optional<std::string>& id = std::nullopt);
ReloadPackResult reload_pack(Brain& brain,
                             const std::optional<std::string>& id = std::nullopt);
SchemaStatsResult read_schema_stats(Brain& brain, const std::string& source_id,
                                    int limit);

// Compatibility APIs used by later historical nodes. N20 handlers use the
// typed interfaces above; reads never call the explicit materializer.
bool set_active_pack(Brain& brain, const std::string& id);
std::string load_pack_json(Brain& brain, const std::string& id = {});
void ensure_default_pack();
// N28: mutations JSON array of {op, type?} or {op, dimension?}
// Returns error string empty on success, else message. applied count via out_applied.
std::string apply_mutations(Brain& brain, const std::string& mutations_json, int* out_applied = nullptr);

}  // namespace qbrain::schema

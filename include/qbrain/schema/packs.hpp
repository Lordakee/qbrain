#pragma once
#include "qbrain/core/brain.hpp"
#include <string>
#include <vector>

namespace qbrain::schema {

struct PackInfo {
  std::string id;
  std::string path;
  bool active = false;
};

std::vector<PackInfo> list_packs(Brain& brain);
std::string active_pack_id(Brain& brain);
bool set_active_pack(Brain& brain, const std::string& id);
std::string load_pack_json(Brain& brain, const std::string& id = {});
void ensure_default_pack();
// N28: mutations JSON array of {op, type?} or {op, dimension?}
// Returns error string empty on success, else message. applied count via out_applied.
std::string apply_mutations(Brain& brain, const std::string& mutations_json, int* out_applied = nullptr);

}  // namespace qbrain::schema

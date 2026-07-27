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

}  // namespace qbrain::schema

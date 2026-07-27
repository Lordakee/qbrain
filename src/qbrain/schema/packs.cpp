#include "qbrain/schema/packs.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace qbrain::schema {
namespace {

fs::path packs_dir() {
  auto d = util::qbrain_root() / "schema-packs";
  util::ensure_dir(d);
  return d;
}

}  // namespace

void ensure_default_pack() {
  auto p = packs_dir() / "default.json";
  if (fs::exists(p)) return;
  std::ofstream out(p);
  out << R"({
  "id": "default",
  "name": "Qbrain default",
  "types": ["note", "timeline", "person", "concept"],
  "dimensions": ["topic", "entity", "time"],
  "phases": ["orphans", "extract_facts", "consolidate", "embed", "purge"]
})";
}

std::vector<PackInfo> list_packs(Brain& brain) {
  ensure_default_pack();
  auto active = active_pack_id(brain);
  std::vector<PackInfo> out;
  for (auto& e : fs::directory_iterator(packs_dir())) {
    if (!e.is_regular_file()) continue;
    if (e.path().extension() != ".json") continue;
    PackInfo pi;
    pi.id = e.path().stem().string();
    pi.path = util::path_to_utf8(e.path());
    pi.active = (pi.id == active);
    out.push_back(std::move(pi));
  }
  if (out.empty()) {
    PackInfo pi;
    pi.id = "default";
    pi.active = true;
    out.push_back(pi);
  }
  return out;
}

std::string active_pack_id(Brain& brain) {
  auto v = brain.get_config_value("schema.active_pack");
  if (v && !v->empty()) return *v;
  return "default";
}

bool set_active_pack(Brain& brain, const std::string& id) {
  ensure_default_pack();
  auto p = packs_dir() / (id + ".json");
  if (!fs::exists(p) && id != "default") return false;
  brain.save_config_value("schema.active_pack", id.empty() ? "default" : id);
  return true;
}

std::string load_pack_json(Brain& brain, const std::string& id) {
  ensure_default_pack();
  auto use = id.empty() ? active_pack_id(brain) : id;
  auto p = packs_dir() / (use + ".json");
  if (!fs::exists(p)) return "{}";
  std::ifstream in(p);
  std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return s.empty() ? "{}" : s;
}

}  // namespace qbrain::schema

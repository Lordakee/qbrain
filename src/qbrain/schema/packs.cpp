#include "qbrain/schema/packs.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

std::string apply_mutations(Brain& brain, const std::string& mutations_json, int* out_applied) {
  if (out_applied) *out_applied = 0;
  ensure_default_pack();
  auto id = active_pack_id(brain);
  if (id.find("..") != std::string::npos || id.find('/') != std::string::npos ||
      id.find('\\') != std::string::npos)
    return "invalid pack id";
  auto p = packs_dir() / (id + ".json");
  json pack;
  try {
    pack = json::parse(load_pack_json(brain, id));
  } catch (...) {
    pack = json::object();
  }
  if (!pack.contains("types") || !pack["types"].is_array()) pack["types"] = json::array();
  if (!pack.contains("dimensions") || !pack["dimensions"].is_array())
    pack["dimensions"] = json::array();

  json muts;
  try {
    muts = json::parse(mutations_json.empty() ? "[]" : mutations_json);
  } catch (...) {
    return "invalid mutations json";
  }
  if (!muts.is_array()) return "mutations must be array";

  int applied = 0;
  for (auto& m : muts) {
    if (!m.is_object()) continue;
    auto op = m.value("op", "");
    if (op == "add_type") {
      auto t = m.value("type", "");
      if (t.empty()) continue;
      bool found = false;
      for (auto& x : pack["types"])
        if (x.get<std::string>() == t) found = true;
      if (!found) {
        pack["types"].push_back(t);
        ++applied;
      }
    } else if (op == "add_dimension") {
      auto d = m.value("dimension", "");
      if (d.empty()) continue;
      bool found = false;
      for (auto& x : pack["dimensions"])
        if (x.get<std::string>() == d) found = true;
      if (!found) {
        pack["dimensions"].push_back(d);
        ++applied;
      }
    } else {
      return "unsupported op: " + op;
    }
  }

  try {
    if (fs::exists(p)) fs::copy_file(p, packs_dir() / (id + ".json.bak"),
                                     fs::copy_options::overwrite_existing);
    std::ofstream out(p);
    out << pack.dump(2);
  } catch (const std::exception& e) {
    return std::string("write failed: ") + e.what();
  }
  if (out_applied) *out_applied = applied;
  return {};
}

}  // namespace qbrain::schema

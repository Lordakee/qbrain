#include "qbrain/schema/lint.hpp"
#include "qbrain/schema/packs.hpp"
#include "qbrain/util/string_util.hpp"
#include <nlohmann/json.hpp>
#include <unordered_set>

using json = nlohmann::json;

namespace qbrain::schema {

std::vector<LintIssue> schema_lint(Brain& brain, int limit) {
  std::vector<LintIssue> out;
  if (limit <= 0) limit = 100;
  auto pages = brain.list_pages(500);
  for (auto& p : pages) {
    if (static_cast<int>(out.size()) >= limit) break;
    if (p.title.empty())
      out.push_back({"empty_title", p.slug, "title is empty"});
    if (p.body.empty())
      out.push_back({"empty_body", p.slug, "body is empty"});
    if (p.slug.empty() || p.slug.find("..") != std::string::npos)
      out.push_back({"bad_slug", p.slug, "invalid slug"});
    if (p.slug.find(' ') != std::string::npos)
      out.push_back({"slug_has_space", p.slug, "slug contains space"});
  }
  return out;
}

std::vector<GraphNode> schema_graph(Brain& brain) {
  std::vector<GraphNode> out;
  auto st = brain.db().prepare(
      "SELECT type, COUNT(*) FROM pages WHERE deleted_at IS NULL GROUP BY type");
  std::unordered_set<std::string> seen;
  while (st.step()) {
    GraphNode n;
    n.id = st.column_text(0);
    n.kind = "type";
    n.count = st.column_int(1);
    seen.insert(n.id);
    out.push_back(std::move(n));
  }
  try {
    auto j = json::parse(load_pack_json(brain));
    if (j.contains("types") && j["types"].is_array()) {
      for (auto& t : j["types"]) {
        auto id = t.get<std::string>();
        if (seen.count(id)) continue;
        out.push_back({id, "pack_type", 0});
      }
    }
  } catch (...) {
  }
  return out;
}

std::string schema_explain_type(Brain& brain, const std::string& type) {
  if (type.empty()) return "empty type";
  try {
    auto j = json::parse(load_pack_json(brain));
    if (j.contains("types") && j["types"].is_array()) {
      for (auto& t : j["types"]) {
        if (t.get<std::string>() == type)
          return "Pack type '" + type + "' from active schema pack.";
      }
    }
  } catch (...) {
  }
  auto st = brain.db().prepare(
      "SELECT COUNT(*) FROM pages WHERE type=? AND deleted_at IS NULL");
  st.bind_text(1, type);
  int64_t n = 0;
  if (st.step()) n = st.column_int(0);
  return "Ad-hoc type '" + type + "' used by " + std::to_string(n) + " page(s).";
}

std::vector<std::string> ontology_propose(Brain& brain, int limit) {
  std::vector<std::string> out;
  std::unordered_set<std::string> pack_types;
  try {
    auto j = json::parse(load_pack_json(brain));
    if (j.contains("types") && j["types"].is_array())
      for (auto& t : j["types"]) pack_types.insert(t.get<std::string>());
  } catch (...) {
  }
  auto st = brain.db().prepare(
      "SELECT type, COUNT(*) c FROM pages WHERE deleted_at IS NULL GROUP BY type ORDER BY c DESC");
  while (st.step()) {
    if (static_cast<int>(out.size()) >= limit) break;
    auto t = st.column_text(0);
    if (pack_types.count(t)) continue;
    if (t.empty()) continue;
    out.push_back(t);
  }
  return out;
}

std::vector<LintIssue> ontology_conflicts(Brain& brain, int limit) {
  std::vector<LintIssue> out;
  std::unordered_set<std::string> pack_types;
  try {
    auto j = json::parse(load_pack_json(brain));
    if (j.contains("types") && j["types"].is_array())
      for (auto& t : j["types"]) pack_types.insert(t.get<std::string>());
  } catch (...) {
  }
  if (pack_types.empty()) return out;
  auto st = brain.db().prepare(
      "SELECT type, COUNT(*) FROM pages WHERE deleted_at IS NULL GROUP BY type");
  while (st.step()) {
    if (static_cast<int>(out.size()) >= limit) break;
    auto t = st.column_text(0);
    if (t.empty()) continue;
    if (!pack_types.count(t)) {
      out.push_back({"unknown_type", t,
                     "type not in active pack (" + std::to_string(st.column_int(1)) + " pages)"});
    }
  }
  return out;
}

}  // namespace qbrain::schema

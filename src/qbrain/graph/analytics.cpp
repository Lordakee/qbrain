#include "qbrain/graph/analytics.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qbrain::graph {
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Heuristic opposite-ish predicate pairs (order independent).
bool predicates_conflict(const std::string& a_in, const std::string& b_in) {
  auto a = lower(a_in);
  auto b = lower(b_in);
  if (a == b) return false;
  static const std::pair<const char*, const char*> kPairs[] = {
      {"is", "is_not"},
      {"is", "isnt"},
      {"is", "isn't"},
      {"supports", "opposes"},
      {"likes", "dislikes"},
      {"has", "lacks"},
      {"titled", "not_titled"},
      {"titled", "untitled"},
      {"true", "false"},
      {"yes", "no"},
      {"works_at", "left"},
      {"employed_by", "former_employee_of"},
      {"located_in", "not_located_in"},
      {"member_of", "not_member_of"},
  };
  for (auto& p : kPairs) {
    if ((a == p.first && b == p.second) || (a == p.second && b == p.first)) return true;
  }
  // prefix not_ / no_ / anti_ vs base
  auto strip_neg = [](const std::string& x) -> std::string {
    if (x.rfind("not_", 0) == 0) return x.substr(4);
    if (x.rfind("no_", 0) == 0) return x.substr(3);
    if (x.rfind("anti_", 0) == 0) return x.substr(5);
    return {};
  };
  auto na = strip_neg(a);
  auto nb = strip_neg(b);
  if (!na.empty() && na == b) return true;
  if (!nb.empty() && nb == a) return true;
  return false;
}

}  // namespace

std::vector<Anomaly> find_anomalies(Brain& brain, int limit) {
  std::vector<Anomaly> out;
  if (limit < 1) return out;

  // 1) Links whose target page is missing or soft-deleted
  {
    auto st = brain.db().prepare(R"SQL(
SELECT l.from_slug, l.to_slug,
  (SELECT COUNT(*) FROM pages p WHERE p.slug = l.to_slug AND p.deleted_at IS NULL) AS live_cnt,
  (SELECT COUNT(*) FROM pages p WHERE p.slug = l.to_slug AND p.deleted_at IS NOT NULL) AS del_cnt
FROM links l
)SQL");
    while (st.step() && static_cast<int>(out.size()) < limit) {
      auto from = st.column_text(0);
      auto to = st.column_text(1);
      int64_t live = st.column_int(2);
      int64_t del = st.column_int(3);
      if (live > 0) continue;
      Anomaly a;
      a.slug = from;
      if (del > 0) {
        a.kind = "link_to_deleted_page";
        a.detail = "link target soft-deleted: " + to;
      } else {
        a.kind = "link_to_missing_page";
        a.detail = "link target missing: " + to;
      }
      out.push_back(std::move(a));
    }
  }

  // 2) High out-degree hubs
  if (static_cast<int>(out.size()) < limit) {
    auto st = brain.db().prepare(R"SQL(
SELECT from_slug, COUNT(*) AS c
FROM links
GROUP BY from_slug
HAVING c > 20
ORDER BY c DESC
)SQL");
    while (st.step() && static_cast<int>(out.size()) < limit) {
      Anomaly a;
      a.kind = "high_out_degree";
      a.slug = st.column_text(0);
      a.detail = "out_degree=" + std::to_string(st.column_int(1)) + " (>20)";
      out.push_back(std::move(a));
    }
  }

  if (static_cast<int>(out.size()) > limit) out.resize(static_cast<size_t>(limit));
  return out;
}

std::vector<Contradiction> find_contradictions(Brain& brain, int limit) {
  std::vector<Contradiction> out;
  if (limit < 1) return out;

  struct FactRow {
    int64_t id = 0;
    std::string entity;
    std::string predicate;
    std::string object;
  };
  std::vector<FactRow> facts;
  {
    auto st = brain.db().prepare(
        "SELECT id, entity_slug, predicate, object_text FROM facts WHERE active=1 ORDER BY entity_slug, id");
    while (st.step()) {
      FactRow f;
      f.id = st.column_int(0);
      f.entity = st.column_text(1);
      f.predicate = st.column_text(2);
      f.object = st.column_text(3);
      facts.push_back(std::move(f));
    }
  }

  // Group by entity
  std::unordered_map<std::string, std::vector<size_t>> by_entity;
  for (size_t i = 0; i < facts.size(); ++i) by_entity[facts[i].entity].push_back(i);

  std::unordered_set<std::string> seen;  // dedupe key

  for (auto& [entity, idxs] : by_entity) {
    if (static_cast<int>(out.size()) >= limit) break;
    for (size_t ai = 0; ai < idxs.size(); ++ai) {
      for (size_t bi = ai + 1; bi < idxs.size(); ++bi) {
        if (static_cast<int>(out.size()) >= limit) break;
        const auto& A = facts[idxs[ai]];
        const auto& B = facts[idxs[bi]];

        // same predicate, different object_text
        if (lower(A.predicate) == lower(B.predicate) && A.object != B.object) {
          std::ostringstream oss;
          oss << A.predicate << ": \"" << A.object << "\" vs \"" << B.object << "\"";
          std::string key = entity + "|same_pred|" + lower(A.predicate) + "|" + A.object + "|" + B.object;
          if (seen.insert(key).second) {
            Contradiction c;
            c.kind = "same_predicate_different_object";
            c.slug = entity;
            c.detail = oss.str();
            out.push_back(std::move(c));
          }
          continue;
        }

        // conflicting predicate pairs
        if (predicates_conflict(A.predicate, B.predicate)) {
          std::ostringstream oss;
          oss << A.predicate << "=\"" << A.object << "\" conflicts with " << B.predicate << "=\""
              << B.object << "\"";
          std::string key = entity + "|pair|" + lower(A.predicate) + "|" + lower(B.predicate) + "|" +
                            A.object + "|" + B.object;
          if (seen.insert(key).second) {
            Contradiction c;
            c.kind = "conflicting_predicates";
            c.slug = entity;
            c.detail = oss.str();
            out.push_back(std::move(c));
          }
        }
      }
    }
  }

  return out;
}

std::vector<Expert> find_experts(Brain& brain, int limit) {
  std::vector<Expert> out;
  if (limit < 1) return out;
  auto st = brain.db().prepare(R"SQL(
SELECT l.to_slug, COUNT(*) AS inbound
FROM links l
WHERE EXISTS (
  SELECT 1 FROM pages p WHERE p.slug = l.to_slug AND p.deleted_at IS NULL
)
GROUP BY l.to_slug
ORDER BY inbound DESC, l.to_slug ASC
LIMIT ?
)SQL");
  st.bind_int(1, limit);
  while (st.step()) {
    Expert e;
    e.slug = st.column_text(0);
    e.inbound_count = st.column_int(1);
    out.push_back(std::move(e));
  }
  return out;
}

}  // namespace qbrain::graph

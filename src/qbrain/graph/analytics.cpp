#include "qbrain/graph/analytics.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qbrain::graph {
namespace {

constexpr int kMaximumLimit = 200;
constexpr size_t kMaximumDetailBytes = 512;
constexpr std::string_view kTruncationMarker = "...[truncated]";
constexpr std::string_view kUtf8Replacement = "\xEF\xBF\xBD";

int clamp_limit(int limit) { return std::clamp(limit, 0, kMaximumLimit); }

int byte_compare(std::string_view lhs, std::string_view rhs) {
  const size_t common = std::min(lhs.size(), rhs.size());
  if (common != 0) {
    const int comparison = std::memcmp(lhs.data(), rhs.data(), common);
    if (comparison != 0) return comparison < 0 ? -1 : 1;
  }
  if (lhs.size() < rhs.size()) return -1;
  if (lhs.size() > rhs.size()) return 1;
  return 0;
}

std::string ascii_fold(std::string value) {
  for (char& c : value) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte >= 'A' && byte <= 'Z') c = static_cast<char>(byte - 'A' + 'a');
  }
  return value;
}

bool is_utf8_continuation(unsigned char byte) {
  return byte >= 0x80 && byte <= 0xBF;
}

size_t utf8_code_point_length(std::string_view value, size_t offset) {
  const auto byte_at = [&value](size_t index) {
    return static_cast<unsigned char>(value[index]);
  };
  const unsigned char first = byte_at(offset);
  const size_t remaining = value.size() - offset;
  if (first <= 0x7F) return 1;
  if (first >= 0xC2 && first <= 0xDF) {
    return remaining >= 2 && is_utf8_continuation(byte_at(offset + 1)) ? 2 : 0;
  }
  if (first == 0xE0) {
    return remaining >= 3 && byte_at(offset + 1) >= 0xA0 && byte_at(offset + 1) <= 0xBF &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if ((first >= 0xE1 && first <= 0xEC) || (first >= 0xEE && first <= 0xEF)) {
    return remaining >= 3 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if (first == 0xED) {
    return remaining >= 3 && byte_at(offset + 1) >= 0x80 && byte_at(offset + 1) <= 0x9F &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if (first == 0xF0) {
    return remaining >= 4 && byte_at(offset + 1) >= 0x90 && byte_at(offset + 1) <= 0xBF &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  }
  if (first >= 0xF1 && first <= 0xF3) {
    return remaining >= 4 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  }
  if (first == 0xF4) {
    return remaining >= 4 && byte_at(offset + 1) >= 0x80 && byte_at(offset + 1) <= 0x8F &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  }
  return 0;
}

void append_sanitized_utf8_unit(std::string& output, std::string_view value, size_t offset,
                                size_t code_point_length) {
  if (code_point_length == 0) {
    output.append(kUtf8Replacement);
  } else {
    output.append(value.data() + offset, code_point_length);
  }
}

std::string bounded_detail(std::string_view value) {
  size_t normalized_size = 0;
  bool needs_truncation = false;
  for (size_t offset = 0; offset < value.size();) {
    const size_t code_point_length = utf8_code_point_length(value, offset);
    const size_t unit_size = code_point_length == 0 ? kUtf8Replacement.size() : code_point_length;
    if (unit_size > kMaximumDetailBytes - normalized_size) {
      needs_truncation = true;
      break;
    }
    normalized_size += unit_size;
    offset += code_point_length == 0 ? 1 : code_point_length;
  }

  const size_t content_limit =
      needs_truncation ? kMaximumDetailBytes - kTruncationMarker.size() : kMaximumDetailBytes;
  std::string output;
  output.reserve(needs_truncation ? kMaximumDetailBytes : normalized_size);
  for (size_t offset = 0; offset < value.size();) {
    const size_t code_point_length = utf8_code_point_length(value, offset);
    const size_t unit_size = code_point_length == 0 ? kUtf8Replacement.size() : code_point_length;
    if (unit_size > content_limit - output.size()) break;
    append_sanitized_utf8_unit(output, value, offset, code_point_length);
    offset += code_point_length == 0 ? 1 : code_point_length;
  }
  if (needs_truncation) output.append(kTruncationMarker);
  return output;
}

// Inputs are already ASCII-folded. Matching remains deliberately syntactic.
bool predicates_conflict(const std::string& first, const std::string& second) {
  if (first == second) return false;
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kPairs = {{
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
  }};
  for (const auto& pair : kPairs) {
    if ((first == pair.first && second == pair.second) ||
        (first == pair.second && second == pair.first)) {
      return true;
    }
  }

  static constexpr std::array<std::string_view, 3> kNegatingPrefixes = {
      "not_", "no_", "anti_"};
  const auto has_negating_prefix = [&](std::string_view value) {
    return std::any_of(kNegatingPrefixes.begin(), kNegatingPrefixes.end(), [&](const auto prefix) {
      return value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
    });
  };
  const auto unprefixed_base = [&](std::string_view value) -> std::string_view {
    for (const auto prefix : kNegatingPrefixes) {
      if (value.size() <= prefix.size() || value.compare(0, prefix.size(), prefix) != 0) continue;
      const auto base = value.substr(prefix.size());
      if (!has_negating_prefix(base)) return base;
    }
    return {};
  };
  const auto first_base = unprefixed_base(first);
  const auto second_base = unprefixed_base(second);
  if (!first_base.empty() && first_base == second && !has_negating_prefix(second)) return true;
  if (!second_base.empty() && second_base == first && !has_negating_prefix(first)) return true;
  return false;
}

struct FactRow {
  int64_t id = 0;
  std::string entity;
  std::string folded_predicate;
  std::string object;
};

struct FactAtom {
  int64_t id = 0;
  std::string folded_predicate;
  std::string object;
};

bool fact_atom_less(const FactAtom& lhs, const FactAtom& rhs) {
  if (const int comparison = byte_compare(lhs.folded_predicate, rhs.folded_predicate);
      comparison != 0) {
    return comparison < 0;
  }
  if (const int comparison = byte_compare(lhs.object, rhs.object); comparison != 0) {
    return comparison < 0;
  }
  return lhs.id < rhs.id;
}

struct ContradictionCandidate {
  int kind_rank = 0;
  std::string entity;
  FactAtom first;
  FactAtom second;
};

bool candidate_less(const ContradictionCandidate& lhs, const ContradictionCandidate& rhs) {
  if (const int comparison = byte_compare(lhs.entity, rhs.entity); comparison != 0) {
    return comparison < 0;
  }
  if (lhs.kind_rank != rhs.kind_rank) return lhs.kind_rank < rhs.kind_rank;
  if (const int comparison = byte_compare(lhs.first.folded_predicate, rhs.first.folded_predicate);
      comparison != 0) {
    return comparison < 0;
  }
  if (const int comparison = byte_compare(lhs.second.folded_predicate, rhs.second.folded_predicate);
      comparison != 0) {
    return comparison < 0;
  }
  if (const int comparison = byte_compare(lhs.first.object, rhs.first.object); comparison != 0) {
    return comparison < 0;
  }
  if (const int comparison = byte_compare(lhs.second.object, rhs.second.object); comparison != 0) {
    return comparison < 0;
  }
  if (lhs.first.id != rhs.first.id) return lhs.first.id < rhs.first.id;
  return lhs.second.id < rhs.second.id;
}

bool same_candidate_key(const ContradictionCandidate& lhs, const ContradictionCandidate& rhs) {
  return lhs.kind_rank == rhs.kind_rank && lhs.entity == rhs.entity &&
         lhs.first.folded_predicate == rhs.first.folded_predicate &&
         lhs.second.folded_predicate == rhs.second.folded_predicate &&
         lhs.first.object == rhs.first.object && lhs.second.object == rhs.second.object;
}

std::string contradiction_detail(const ContradictionCandidate& candidate) {
  std::ostringstream output;
  if (candidate.kind_rank == 0) {
    output << candidate.first.folded_predicate << "=\"" << candidate.first.object << "\" vs "
           << candidate.second.folded_predicate << "=\"" << candidate.second.object << "\"";
  } else {
    output << candidate.first.folded_predicate << "=\"" << candidate.first.object
           << "\" conflicts with " << candidate.second.folded_predicate << "=\""
           << candidate.second.object << "\"";
  }
  return bounded_detail(output.str());
}

}  // namespace

std::vector<Anomaly> find_anomalies(Brain& brain, const std::string& source_id, int limit) {
  std::vector<Anomaly> out;
  const int effective_limit = clamp_limit(limit);
  if (effective_limit == 0) return out;

  // n38 (census: COLLATE BINARY): per-expression COLLATE BINARY removed from
  // the ordering/comparison arms of the analytics statements. On SQLite the
  // default collation is BINARY and the ordering columns carry column-level
  // COLLATE BINARY in the canonical schema (links.from_slug/to_slug,
  // facts.entity_slug/predicate/object_text), so ordering and the CASE-arm
  // comparisons are byte-identical; CTE/reference columns resolve to the
  // default BINARY collation on both backends (PG side: COLLATE "C" columns).
  auto st = brain.db().prepare(R"SQL(
WITH active_links AS (
  SELECT l.source_id, l.from_slug, l.to_slug
  FROM links AS l
  JOIN pages AS origin
    ON origin.source_id = l.source_id
   AND origin.slug = l.from_slug
   AND origin.deleted_at IS NULL
  WHERE l.source_id = ?
),
broken_targets AS (
  SELECT DISTINCT
    active.source_id,
    CASE WHEN EXISTS (
      SELECT 1 FROM pages AS deleted_target
      WHERE deleted_target.source_id = active.source_id
        AND deleted_target.slug = active.to_slug
        AND deleted_target.deleted_at IS NOT NULL
    ) THEN 0 ELSE 1 END AS kind_rank,
    CASE WHEN EXISTS (
      SELECT 1 FROM pages AS deleted_target
      WHERE deleted_target.source_id = active.source_id
        AND deleted_target.slug = active.to_slug
        AND deleted_target.deleted_at IS NOT NULL
    ) THEN 'link_to_deleted_page' ELSE 'link_to_missing_page' END AS kind,
    active.from_slug AS slug,
    active.to_slug AS target_slug,
    0 AS row_count
  FROM active_links AS active
  WHERE NOT EXISTS (
    SELECT 1 FROM pages AS live_target
    WHERE live_target.source_id = active.source_id
      AND live_target.slug = active.to_slug
      AND live_target.deleted_at IS NULL
  )
),
high_out_degree AS (
  SELECT source_id, 2 AS kind_rank, 'high_out_degree' AS kind,
         from_slug AS slug, '' AS target_slug, COUNT(*) AS row_count
  FROM active_links
  GROUP BY source_id, from_slug
  HAVING COUNT(*) > 20
)
SELECT source_id, kind, slug, target_slug, row_count
FROM (
  SELECT source_id, kind_rank, kind, slug, target_slug, row_count FROM broken_targets
  UNION ALL
  SELECT source_id, kind_rank, kind, slug, target_slug, row_count FROM high_out_degree
) AS candidates
ORDER BY candidates.kind_rank ASC, candidates.slug ASC,
         candidates.target_slug ASC, candidates.row_count DESC
LIMIT ?)SQL");
  st.bind_text(1, source_id);
  st.bind_int(2, effective_limit);
  while (st.step()) {
    Anomaly anomaly;
    anomaly.source_id = st.column_text(0);
    anomaly.kind = st.column_text(1);
    anomaly.slug = st.column_text(2);
    const std::string target_slug = st.column_text(3);
    const int64_t row_count = st.column_int(4);
    if (anomaly.kind == "link_to_deleted_page") {
      anomaly.detail = bounded_detail("link target soft-deleted: " + target_slug);
    } else if (anomaly.kind == "link_to_missing_page") {
      anomaly.detail = bounded_detail("link target missing: " + target_slug);
    } else {
      anomaly.detail = bounded_detail("out_degree=" + std::to_string(row_count) + " (>20)");
    }
    out.push_back(std::move(anomaly));
  }
  return out;
}

std::vector<Contradiction> find_contradictions(Brain& brain, const std::string& source_id,
                                                int limit) {
  std::vector<Contradiction> out;
  const int effective_limit = clamp_limit(limit);
  if (effective_limit == 0) return out;

  auto st = brain.db().prepare(R"SQL(
WITH eligible AS (
  SELECT f.id,
         f.entity_slug AS entity,
         lower(f.predicate) AS predicate,
         f.object_text AS object
  FROM facts AS f
  JOIN pages AS owner ON owner.id = f.page_id
  WHERE f.active = 1
    AND owner.source_id = ?1
    AND owner.deleted_at IS NULL
), raw_pairs AS (
  SELECT a.entity,
         CASE WHEN a.predicate = b.predicate THEN 0 ELSE 1 END AS kind_rank,
         a.predicate AS a_predicate, a.object AS a_object,
         b.predicate AS b_predicate, b.object AS b_object
  FROM eligible AS a
  JOIN eligible AS b
    ON b.entity = a.entity
   AND a.id < b.id
  WHERE (a.predicate = b.predicate AND a.object <> b.object)
     OR (a.predicate = 'is' AND b.predicate IN ('is_not','isnt','isn''t'))
     OR (b.predicate = 'is' AND a.predicate IN ('is_not','isnt','isn''t'))
     OR (a.predicate = 'supports' AND b.predicate = 'opposes')
     OR (a.predicate = 'opposes' AND b.predicate = 'supports')
     OR (a.predicate = 'likes' AND b.predicate = 'dislikes')
     OR (a.predicate = 'dislikes' AND b.predicate = 'likes')
     OR (a.predicate = 'has' AND b.predicate = 'lacks')
     OR (a.predicate = 'lacks' AND b.predicate = 'has')
     OR (a.predicate = 'titled' AND b.predicate IN ('not_titled','untitled'))
     OR (b.predicate = 'titled' AND a.predicate IN ('not_titled','untitled'))
     OR (a.predicate = 'true' AND b.predicate = 'false')
     OR (a.predicate = 'false' AND b.predicate = 'true')
     OR (a.predicate = 'yes' AND b.predicate = 'no')
     OR (a.predicate = 'no' AND b.predicate = 'yes')
     OR (a.predicate = 'works_at' AND b.predicate = 'left')
     OR (a.predicate = 'left' AND b.predicate = 'works_at')
     OR (a.predicate = 'employed_by' AND b.predicate = 'former_employee_of')
     OR (a.predicate = 'former_employee_of' AND b.predicate = 'employed_by')
     OR (a.predicate = 'located_in' AND b.predicate = 'not_located_in')
     OR (a.predicate = 'not_located_in' AND b.predicate = 'located_in')
     OR (a.predicate = 'member_of' AND b.predicate = 'not_member_of')
     OR (a.predicate = 'not_member_of' AND b.predicate = 'member_of')
     OR (substr(a.predicate, 1, 4) = 'not_' AND length(a.predicate) > 4
         AND substr(a.predicate, 5, 4) <> 'not_'
         AND substr(a.predicate, 5, 3) <> 'no_'
         AND substr(a.predicate, 5, 5) <> 'anti_'
         AND substr(a.predicate, 5) = b.predicate
         AND substr(b.predicate, 1, 4) <> 'not_' AND substr(b.predicate, 1, 3) <> 'no_'
         AND substr(b.predicate, 1, 5) <> 'anti_')
     OR (substr(b.predicate, 1, 4) = 'not_' AND length(b.predicate) > 4
         AND substr(b.predicate, 5, 4) <> 'not_'
         AND substr(b.predicate, 5, 3) <> 'no_'
         AND substr(b.predicate, 5, 5) <> 'anti_'
         AND substr(b.predicate, 5) = a.predicate
         AND substr(a.predicate, 1, 4) <> 'not_' AND substr(a.predicate, 1, 3) <> 'no_'
         AND substr(a.predicate, 1, 5) <> 'anti_')
     OR (substr(a.predicate, 1, 3) = 'no_' AND length(a.predicate) > 3
         AND substr(a.predicate, 4, 4) <> 'not_'
         AND substr(a.predicate, 4, 3) <> 'no_'
         AND substr(a.predicate, 4, 5) <> 'anti_'
         AND substr(a.predicate, 4) = b.predicate
         AND substr(b.predicate, 1, 4) <> 'not_' AND substr(b.predicate, 1, 3) <> 'no_'
         AND substr(b.predicate, 1, 5) <> 'anti_')
     OR (substr(b.predicate, 1, 3) = 'no_' AND length(b.predicate) > 3
         AND substr(b.predicate, 4, 4) <> 'not_'
         AND substr(b.predicate, 4, 3) <> 'no_'
         AND substr(b.predicate, 4, 5) <> 'anti_'
         AND substr(b.predicate, 4) = a.predicate
         AND substr(a.predicate, 1, 4) <> 'not_' AND substr(a.predicate, 1, 3) <> 'no_'
         AND substr(a.predicate, 1, 5) <> 'anti_')
     OR (substr(a.predicate, 1, 5) = 'anti_' AND length(a.predicate) > 5
         AND substr(a.predicate, 6, 4) <> 'not_'
         AND substr(a.predicate, 6, 3) <> 'no_'
         AND substr(a.predicate, 6, 5) <> 'anti_'
         AND substr(a.predicate, 6) = b.predicate
         AND substr(b.predicate, 1, 4) <> 'not_' AND substr(b.predicate, 1, 3) <> 'no_'
         AND substr(b.predicate, 1, 5) <> 'anti_')
     OR (substr(b.predicate, 1, 5) = 'anti_' AND length(b.predicate) > 5
         AND substr(b.predicate, 6, 4) <> 'not_'
         AND substr(b.predicate, 6, 3) <> 'no_'
         AND substr(b.predicate, 6, 5) <> 'anti_'
         AND substr(b.predicate, 6) = a.predicate
         AND substr(a.predicate, 1, 4) <> 'not_' AND substr(a.predicate, 1, 3) <> 'no_'
         AND substr(a.predicate, 1, 5) <> 'anti_')
), canonical AS (
  SELECT entity, kind_rank,
         CASE WHEN a_predicate < b_predicate
                   OR (a_predicate = b_predicate AND a_object <= b_object)
              THEN a_predicate ELSE b_predicate END AS first_predicate,
         CASE WHEN a_predicate < b_predicate
                   OR (a_predicate = b_predicate AND a_object <= b_object)
              THEN a_object ELSE b_object END AS first_object,
         CASE WHEN a_predicate < b_predicate
                   OR (a_predicate = b_predicate AND a_object <= b_object)
              THEN b_predicate ELSE a_predicate END AS second_predicate,
         CASE WHEN a_predicate < b_predicate
                   OR (a_predicate = b_predicate AND a_object <= b_object)
              THEN b_object ELSE a_object END AS second_object
  FROM raw_pairs
)
SELECT entity, kind_rank, first_predicate, first_object, second_predicate, second_object
FROM canonical
GROUP BY entity, kind_rank, first_predicate, first_object, second_predicate, second_object
ORDER BY entity ASC, kind_rank ASC,
         first_predicate ASC, second_predicate ASC,
         first_object ASC, second_object ASC
LIMIT ?2
)SQL");
  st.bind_text(1, source_id);
  st.bind_int(2, effective_limit);
  while (st.step()) {
    ContradictionCandidate candidate;
    candidate.entity = st.column_text(0);
    candidate.kind_rank = st.column_int(1);
    candidate.first = FactAtom{0, st.column_text(2), st.column_text(3)};
    candidate.second = FactAtom{0, st.column_text(4), st.column_text(5)};
    Contradiction contradiction;
    contradiction.source_id = source_id;
    contradiction.kind = candidate.kind_rank == 0 ? "same_predicate_different_object"
                                                  : "conflicting_predicates";
    contradiction.slug = candidate.entity;
    contradiction.detail = contradiction_detail(candidate);
    out.push_back(std::move(contradiction));
  }
  return out;
}

std::vector<Expert> find_experts(Brain& brain, const std::string& source_id, int limit) {
  std::vector<Expert> out;
  const int effective_limit = clamp_limit(limit);
  if (effective_limit == 0) return out;

  auto st = brain.db().prepare(R"SQL(
SELECT l.source_id, l.to_slug, COUNT(*) AS inbound
FROM links AS l
JOIN pages AS origin
  ON origin.source_id = l.source_id
 AND origin.slug = l.from_slug
 AND origin.deleted_at IS NULL
JOIN pages AS target
  ON target.source_id = l.source_id
 AND target.slug = l.to_slug
 AND target.deleted_at IS NULL
WHERE l.source_id = ?
GROUP BY l.source_id, l.to_slug
HAVING COUNT(*) > 0
ORDER BY inbound DESC, l.to_slug ASC
LIMIT ?
)SQL");
  st.bind_text(1, source_id);
  st.bind_int(2, effective_limit);
  while (st.step()) {
    Expert expert;
    expert.source_id = st.column_text(0);
    expert.slug = st.column_text(1);
    expert.inbound_count = st.column_int(2);
    out.push_back(std::move(expert));
  }
  return out;
}

}  // namespace qbrain::graph

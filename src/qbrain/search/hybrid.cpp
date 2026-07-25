#include "qbrain/search/hybrid.hpp"
#include "qbrain/search/rrf.hpp"
#include "qbrain/search/vector.hpp"
#include "qbrain/util/string_util.hpp"
#include <algorithm>
#include <unordered_map>

namespace qbrain::search {

static std::string fts_quote(const std::string& q) {
  // Join tokens with spaces; lowercase to neutralize FTS5 reserved words (AND/OR/NOT).
  auto parts = util::split(q, ' ');
  std::string out;
  for (auto& p : parts) {
    p = util::trim(p);
    if (p.empty()) continue;
    p = util::replace_all(p, "\"", "");
    p = util::to_lower(p);
    if (p.empty()) continue;
    if (!out.empty()) out += " ";
    out += "\"" + p + "\"";
  }
  return out.empty() ? "\"\"" : out;
}

std::vector<SearchHit> fts_search(Brain& brain, const std::string& query, int limit,
                                  const std::string& source_id) {
  std::vector<SearchHit> out;
  auto mq = fts_quote(query);
  std::string sql = R"SQL(
SELECT p.id, p.slug, p.title, p.type, snippet(pages_fts, 2, '', '', '…', 16),
       bm25(pages_fts) AS rank
FROM pages_fts
JOIN pages p ON p.id = pages_fts.rowid
WHERE pages_fts MATCH ? AND p.deleted_at IS NULL
)SQL";
  if (!source_id.empty()) sql += " AND p.source_id = ?";
  sql += " ORDER BY rank LIMIT ?";
  try {
    auto st = brain.db().prepare(sql);
    int idx = 1;
    st.bind_text(idx++, mq);
    if (!source_id.empty()) st.bind_text(idx++, source_id);
    st.bind_int(idx, limit);
    int rank = 1;
    while (st.step()) {
      SearchHit h;
      h.page_id = st.column_int(0);
      h.slug = st.column_text(1);
      h.title = st.column_text(2);
      h.type = st.column_text(3);
      h.snippet = st.column_text(4);
      h.fts_rank = static_cast<double>(rank);
      h.score = -st.column_double(5);
      out.push_back(std::move(h));
      ++rank;
    }
  } catch (...) {
    std::string sql2 =
        "SELECT id, slug, title, type, substr(body,1,160) FROM pages "
        "WHERE deleted_at IS NULL AND (title LIKE ? ESCAPE '\\' OR body LIKE ? ESCAPE '\\' "
        "OR slug LIKE ? ESCAPE '\\')";
    if (!source_id.empty()) sql2 += " AND source_id = ?";
    sql2 += " ORDER BY updated_at DESC LIMIT ?";
    auto st2 = brain.db().prepare(sql2);
    // Escape LIKE wildcards so user input is literal.
    std::string esc = query;
    esc = util::replace_all(esc, "\\", "\\\\");
    esc = util::replace_all(esc, "%", "\\%");
    esc = util::replace_all(esc, "_", "\\_");
    std::string like = "%" + esc + "%";
    int idx = 1;
    st2.bind_text(idx++, like);
    st2.bind_text(idx++, like);
    st2.bind_text(idx++, like);
    if (!source_id.empty()) st2.bind_text(idx++, source_id);
    st2.bind_int(idx, limit);
    int rank = 1;
    while (st2.step()) {
      SearchHit h;
      h.page_id = st2.column_int(0);
      h.slug = st2.column_text(1);
      h.title = st2.column_text(2);
      h.type = st2.column_text(3);
      h.snippet = st2.column_text(4);
      h.fts_rank = static_cast<double>(rank++);
      out.push_back(std::move(h));
    }
  }
  return out;
}

std::vector<SearchHit> vector_search(Brain& brain, const std::vector<float>& qemb, int limit,
                                     const std::string& source_id) {
  std::vector<SearchHit> out;
  if (qemb.empty()) return out;
  std::string sql =
      "SELECT c.page_id, p.slug, p.title, p.type, c.text, c.embedding "
      "FROM content_chunks c JOIN pages p ON p.id = c.page_id "
      "WHERE p.deleted_at IS NULL AND c.embedding IS NOT NULL";
  if (!source_id.empty()) sql += " AND p.source_id = ?";
  auto st = brain.db().prepare(sql);
  if (!source_id.empty()) st.bind_text(1, source_id);
  struct Cand {
    SearchHit h;
    double sim;
  };
  std::vector<Cand> cands;
  while (st.step()) {
    auto blob = st.column_blob(5);
    auto emb = unpack_f32(blob);
    double sim = cosine_similarity(qemb, emb);
    SearchHit h;
    h.page_id = st.column_int(0);
    h.slug = st.column_text(1);
    h.title = st.column_text(2);
    h.type = st.column_text(3);
    h.snippet = st.column_text(4).substr(0, 200);
    h.score = sim;
    cands.push_back({std::move(h), sim});
  }
  std::unordered_map<std::string, Cand> best;
  for (auto& c : cands) {
    auto it = best.find(c.h.slug);
    if (it == best.end() || c.sim > it->second.sim) best[c.h.slug] = std::move(c);
  }
  std::vector<Cand> uniq;
  uniq.reserve(best.size());
  for (auto& [_, c] : best) uniq.push_back(std::move(c));
  std::sort(uniq.begin(), uniq.end(),
            [](const Cand& a, const Cand& b) { return a.sim > b.sim; });
  if (static_cast<int>(uniq.size()) > limit) uniq.resize(static_cast<size_t>(limit));
  int rank = 1;
  for (auto& c : uniq) {
    c.h.vector_rank = static_cast<double>(rank++);
    c.h.score = c.sim;
    out.push_back(std::move(c.h));
  }
  return out;
}

std::vector<SearchHit> hybrid_search(Brain& brain, const std::string& query,
                                     const std::vector<float>* qemb, const HybridOpts& opts) {
  int cand = opts.limit * 3;
  bool use_vec = opts.use_vector;
  if (opts.mode == "conservative") {
    use_vec = false;
    cand = opts.limit * 2;
  } else if (opts.mode == "tokenmax") {
    cand = opts.limit * 5;
  }
  auto fts = fts_search(brain, query, cand, opts.source_id);
  std::vector<std::vector<SearchHit>> lists;
  lists.push_back(fts);
  if (use_vec && qemb && !qemb->empty()) {
    lists.push_back(vector_search(brain, *qemb, cand, opts.source_id));
  }
  auto fused = rrf_fusion(lists, opts.rrf_k);
  // N3 post-fusion: title / backlink boosts (lightweight)
  for (auto& h : fused) {
    auto ql = util::to_lower(query);
    auto tl = util::to_lower(h.title);
    if (!tl.empty() && tl.find(ql) != std::string::npos) h.score *= 1.25;
    else if (!ql.empty() && !tl.empty()) {
      // token overlap
      for (auto& tok : util::split(ql, ' ')) {
        if (tok.size() > 2 && tl.find(tok) != std::string::npos) {
          h.score *= 1.08;
          break;
        }
      }
    }
    auto backs = brain.get_links_to(h.slug, opts.source_id.empty() ? "default" : opts.source_id);
    if (!backs.empty()) h.score *= (1.0 + 0.05 * std::min<size_t>(backs.size(), 5));
  }
  std::sort(fused.begin(), fused.end(),
            [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
  // autocut: drop long tail if score gap large
  if (fused.size() >= 3) {
    double top = fused[0].score;
    size_t cut = fused.size();
    for (size_t i = 1; i < fused.size(); ++i) {
      if (top > 0 && fused[i].score < top * 0.35) {
        cut = i;
        break;
      }
    }
    if (cut < fused.size() && cut >= 1) fused.resize(cut);
  }
  if (static_cast<int>(fused.size()) > opts.limit)
    fused.resize(static_cast<size_t>(opts.limit));
  return fused;
}

}  // namespace qbrain::search

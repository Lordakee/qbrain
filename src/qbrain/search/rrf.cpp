#include "qbrain/search/rrf.hpp"
#include <algorithm>
#include <unordered_map>

namespace qbrain::search {

std::vector<SearchHit> rrf_fusion(const std::vector<std::vector<SearchHit>>& lists, int k) {
  struct Acc {
    SearchHit hit;
    double score = 0;
  };
  std::unordered_map<std::string, Acc> map;
  for (const auto& list : lists) {
    for (size_t rank = 0; rank < list.size(); ++rank) {
      const auto& h = list[rank];
      std::string key = h.slug.empty() ? std::to_string(h.page_id) : h.slug;
      auto& a = map[key];
      if (a.hit.slug.empty()) a.hit = h;
      a.score += 1.0 / (static_cast<double>(k) + static_cast<double>(rank + 1));
      // preserve arm ranks if present
      if (h.fts_rank > a.hit.fts_rank) a.hit.fts_rank = h.fts_rank;
      if (h.vector_rank > a.hit.vector_rank) a.hit.vector_rank = h.vector_rank;
    }
  }
  std::vector<SearchHit> out;
  out.reserve(map.size());
  for (auto& [_, a] : map) {
    a.hit.score = a.score;
    out.push_back(std::move(a.hit));
  }
  std::sort(out.begin(), out.end(),
            [](const SearchHit& x, const SearchHit& y) { return x.score > y.score; });
  return out;
}

}  // namespace qbrain::search

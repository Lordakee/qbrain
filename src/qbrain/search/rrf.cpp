#include "qbrain/search/rrf.hpp"
#include <algorithm>
#include <unordered_map>

namespace qbrain::search {

std::vector<SearchHit> rrf_fusion_weighted(const std::vector<std::vector<SearchHit>>& lists,
                                           const std::vector<double>& weights, int k) {
  struct Acc {
    SearchHit hit;
    double score = 0;
  };
  std::unordered_map<std::string, Acc> map;
  for (size_t li = 0; li < lists.size(); ++li) {
    double w = (li < weights.size()) ? weights[li] : 1.0;
    if (w <= 0) continue;
    const auto& list = lists[li];
    for (size_t rank = 0; rank < list.size(); ++rank) {
      const auto& h = list[rank];
      std::string key = h.slug.empty() ? std::to_string(h.page_id) : h.slug;
      auto& a = map[key];
      if (a.hit.slug.empty()) a.hit = h;
      a.score += w / (static_cast<double>(k) + static_cast<double>(rank + 1));
      if (h.fts_rank > 0 && (a.hit.fts_rank == 0 || h.fts_rank < a.hit.fts_rank))
        a.hit.fts_rank = h.fts_rank;
      if (h.vector_rank > 0 && (a.hit.vector_rank == 0 || h.vector_rank < a.hit.vector_rank))
        a.hit.vector_rank = h.vector_rank;
      if (a.hit.snippet.empty() && !h.snippet.empty()) a.hit.snippet = h.snippet;
      if (a.hit.title.empty() && !h.title.empty()) a.hit.title = h.title;
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

std::vector<SearchHit> rrf_fusion(const std::vector<std::vector<SearchHit>>& lists, int k) {
  return rrf_fusion_weighted(lists, {}, k);
}

}  // namespace qbrain::search

#pragma once
#include "qbrain/core/types.hpp"
#include <vector>

namespace qbrain::search {

// lists: each list ranked best-first; scores in hits may be ignored (rank-based)
std::vector<SearchHit> rrf_fusion(const std::vector<std::vector<SearchHit>>& lists,
                                  int k = 60);

// Weighted RRF: weights[i] multiplies contribution of lists[i] (default 1.0).
std::vector<SearchHit> rrf_fusion_weighted(const std::vector<std::vector<SearchHit>>& lists,
                                           const std::vector<double>& weights, int k = 60);

}  // namespace qbrain::search

#pragma once
#include "qbrain/core/types.hpp"
#include <vector>

namespace qbrain::search {

// lists: each list ranked best-first; scores in hits may be ignored (rank-based)
std::vector<SearchHit> rrf_fusion(const std::vector<std::vector<SearchHit>>& lists,
                                  int k = 60);

}  // namespace qbrain::search

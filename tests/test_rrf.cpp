#include "qbrain/search/rrf.hpp"
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_rrf() {
  using qbrain::SearchHit;
  using qbrain::search::rrf_fusion;
  std::vector<SearchHit> a = {{1, "a", "A"}, {2, "b", "B"}, {3, "c", "C"}};
  std::vector<SearchHit> b = {{2, "b", "B"}, {1, "a", "A"}, {4, "d", "D"}};
  auto fused = rrf_fusion({a, b}, 60);
  QB_CHECK(!fused.empty());
  // a and b should rank high
  QB_CHECK(fused[0].slug == "a" || fused[0].slug == "b");
  QB_CHECK(fused.size() == 4);
}

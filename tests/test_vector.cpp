#include "qbrain/search/vector.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_vector() {
  using namespace qbrain::search;
  std::vector<float> a = {1, 0, 0};
  std::vector<float> b = {1, 0, 0};
  std::vector<float> c = {0, 1, 0};
  QB_CHECK(std::abs(cosine_similarity(a, b) - 1.0) < 1e-6);
  QB_CHECK(std::abs(cosine_similarity(a, c)) < 1e-6);
  auto blob = pack_f32(a);
  auto back = unpack_f32(blob);
  QB_CHECK(back.size() == 3);
  QB_CHECK(back[0] == 1.f);
}

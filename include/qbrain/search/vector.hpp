#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace qbrain::search {

double cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);
std::vector<uint8_t> pack_f32(const std::vector<float>& v);
std::vector<float> unpack_f32(const std::vector<uint8_t>& blob);

}  // namespace qbrain::search

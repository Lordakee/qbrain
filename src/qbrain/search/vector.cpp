#include "qbrain/search/vector.hpp"
#include <cstring>

namespace qbrain::search {

double cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.empty() || a.size() != b.size()) return 0.0;
  double dot = 0, na = 0, nb = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += static_cast<double>(a[i]) * b[i];
    na += static_cast<double>(a[i]) * a[i];
    nb += static_cast<double>(b[i]) * b[i];
  }
  if (na <= 0 || nb <= 0) return 0.0;
  return dot / (std::sqrt(na) * std::sqrt(nb));
}

std::vector<uint8_t> pack_f32(const std::vector<float>& v) {
  std::vector<uint8_t> out(v.size() * sizeof(float));
  if (!v.empty()) std::memcpy(out.data(), v.data(), out.size());
  return out;
}

std::vector<float> unpack_f32(const std::vector<uint8_t>& blob) {
  if (blob.size() % sizeof(float) != 0) return {};
  size_t n = blob.size() / sizeof(float);
  std::vector<float> out(n);
  if (n) std::memcpy(out.data(), blob.data(), blob.size());
  return out;
}

}  // namespace qbrain::search

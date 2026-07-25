#pragma once
#include "qbrain/core/types.hpp"
#include <string>
#include <vector>

namespace qbrain::ai {

struct EmbedResult {
  bool ok = false;
  std::string error;
  std::vector<std::vector<float>> vectors;
  std::string model;
};

EmbedResult embed_texts(const Config& cfg, const std::vector<std::string>& texts);

}  // namespace qbrain::ai

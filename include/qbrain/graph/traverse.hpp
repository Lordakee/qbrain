#pragma once
#include "qbrain/core/brain.hpp"
#include "qbrain/core/types.hpp"
#include <string>
#include <vector>

namespace qbrain::graph {

struct Neighbor {
  std::string slug;
  std::string link_type;
  std::string direction;  // out | in
  int depth = 1;
};

std::vector<Neighbor> neighbors(Brain& brain, const std::string& slug, int depth = 1,
                                const std::string& source_id = "default");

}  // namespace qbrain::graph

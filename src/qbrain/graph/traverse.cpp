#include "qbrain/graph/traverse.hpp"
#include <queue>
#include <unordered_set>

namespace qbrain::graph {

std::vector<Neighbor> neighbors(Brain& brain, const std::string& slug, int depth) {
  std::vector<Neighbor> out;
  if (depth < 1) return out;
  std::unordered_set<std::string> visited;
  visited.insert(slug);
  std::queue<std::pair<std::string, int>> q;
  q.push({slug, 0});
  while (!q.empty()) {
    auto [cur, d] = q.front();
    q.pop();
    if (d >= depth) continue;
    for (const auto& l : brain.get_links_from(cur)) {
      Neighbor n{l.to_slug, l.link_type, "out", d + 1};
      out.push_back(n);
      if (!visited.count(l.to_slug)) {
        visited.insert(l.to_slug);
        q.push({l.to_slug, d + 1});
      }
    }
    for (const auto& l : brain.get_links_to(cur)) {
      Neighbor n{l.from_slug, l.link_type, "in", d + 1};
      out.push_back(n);
      if (!visited.count(l.from_slug)) {
        visited.insert(l.from_slug);
        q.push({l.from_slug, d + 1});
      }
    }
  }
  return out;
}

}  // namespace qbrain::graph

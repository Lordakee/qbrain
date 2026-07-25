#pragma once
#include "qbrain/core/types.hpp"
#include <string>
#include <vector>

namespace qbrain::graph {

std::vector<Link> extract_links(const std::string& source_id,
                                const std::string& from_slug,
                                const std::string& body);

}  // namespace qbrain::graph

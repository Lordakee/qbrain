#pragma once
#include <string>
#include <utility>

namespace qbrain::ingest {

// Returns {frontmatter_json, body}
std::pair<std::string, std::string> split_frontmatter(const std::string& text);
std::string title_from_body(const std::string& body, const std::string& fallback);
std::string slug_from_path(const std::string& relative_path);

}  // namespace qbrain::ingest

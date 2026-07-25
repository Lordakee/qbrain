#include "qbrain/ingest/markdown.hpp"
#include "qbrain/util/string_util.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace qbrain::ingest {

std::pair<std::string, std::string> split_frontmatter(const std::string& text) {
  if (!util::starts_with(text, "---")) return {"{}", text};
  auto nl = text.find('\n');
  if (nl == std::string::npos) return {"{}", text};
  // Require a line that is exactly "---" (optionally followed by newline).
  auto end = text.find("\n---\n", nl);
  size_t body_start = 0;
  std::string yaml;
  if (end != std::string::npos) {
    yaml = text.substr(nl + 1, end - (nl + 1));
    body_start = end + 5;  // past "\n---\n"
  } else if (text.size() >= 4 && text.compare(text.size() - 4, 4, "\n---") == 0) {
    end = text.size() - 4;
    yaml = text.substr(nl + 1, end - (nl + 1));
    body_start = text.size();
  } else {
    return {"{}", text};
  }
  // very light yaml -> json (key: value lines)
  json j = json::object();
  std::istringstream iss(yaml);
  std::string line;
  while (std::getline(iss, line)) {
    auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    auto k = util::trim(line.substr(0, pos));
    auto v = util::trim(line.substr(pos + 1));
    if (!v.empty() && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
      v = v.substr(1, v.size() - 2);
    if (!k.empty()) j[k] = v;
  }
  return {j.dump(), text.substr(body_start)};
}

std::string title_from_body(const std::string& body, const std::string& fallback) {
  std::istringstream iss(body);
  std::string line;
  while (std::getline(iss, line)) {
    line = util::trim(line);
    if (line.empty()) continue;
    if (util::starts_with(line, "#")) {
      size_t i = 0;
      while (i < line.size() && line[i] == '#') ++i;
      return util::trim(line.substr(i));
    }
    return line.substr(0, 120);
  }
  return fallback.empty() ? "untitled" : fallback;
}

std::string slug_from_path(const std::string& relative_path) {
  std::string s = relative_path;
  for (char& c : s) {
    if (c == '\\') c = '/';
  }
  if (util::ends_with(util::to_lower(s), ".md")) s = s.substr(0, s.size() - 3);
  if (util::ends_with(util::to_lower(s), ".markdown")) s = s.substr(0, s.size() - 9);
  // collapse
  while (!s.empty() && (s[0] == '/' || s[0] == '.')) s.erase(s.begin());
  return s.empty() ? "untitled" : s;
}

}  // namespace qbrain::ingest

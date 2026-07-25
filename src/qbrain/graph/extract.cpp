#include "qbrain/graph/extract.hpp"
#include "qbrain/util/string_util.hpp"
#include <regex>
#include <unordered_map>

namespace qbrain::graph {

std::vector<Link> extract_links(const std::string& source_id, const std::string& from_slug,
                                const std::string& body) {
  std::vector<Link> out;
  std::unordered_map<std::string, bool> seen;

  auto push = [&](std::string to, const std::string& type, const std::string& src,
                  const std::string& ctx) {
    to = util::trim(to);
    if (to.empty() || to == from_slug) return;
    // strip .md
    if (util::ends_with(to, ".md")) to = to.substr(0, to.size() - 3);
    // normalize path-like to slug
    for (char& c : to) {
      if (c == '\\') c = '/';
    }
    std::string key = to + "|" + type + "|" + src;
    if (seen[key]) return;
    seen[key] = true;
    Link l;
    l.source_id = source_id;
    l.from_slug = from_slug;
    l.to_slug = to;
    l.link_type = type;
    l.link_source = src;
    l.context = ctx.substr(0, 200);
    out.push_back(std::move(l));
  };

  // [[wikilink]] or [[wikilink|alias]]
  {
    static const std::regex re(R"(\[\[([^\]|#]+)(?:\|[^\]]+)?\]\])");
    auto begin = std::sregex_iterator(body.begin(), body.end(), re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
      push(it->str(1), "related", "wikilink", it->str(0));
    }
  }

  // [text](target.md) — skip http(s)
  {
    static const std::regex re(R"(\[[^\]]*\]\(([^)]+)\))");
    auto begin = std::sregex_iterator(body.begin(), body.end(), re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
      auto t = it->str(1);
      if (util::starts_with(t, "http://") || util::starts_with(t, "https://") ||
          util::starts_with(t, "mailto:"))
        continue;
      // drop anchors
      auto hash = t.find('#');
      if (hash != std::string::npos) t = t.substr(0, hash);
      push(t, "related", "markdown", it->str(0));
    }
  }

  return out;
}

}  // namespace qbrain::graph

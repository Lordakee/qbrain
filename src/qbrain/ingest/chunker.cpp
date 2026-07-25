#include "qbrain/ingest/chunker.hpp"
#include "qbrain/util/string_util.hpp"

namespace qbrain::ingest {

std::vector<std::string> chunk_markdown(const std::string& title, const std::string& body,
                                        const ChunkerOpts& opts) {
  std::vector<std::string> out;
  std::string head = title.empty() ? std::string{} : ("# " + title + "\n\n");
  std::string text = head + body;
  if (static_cast<int>(text.size()) <= opts.target_chars) {
    out.push_back(util::trim(text));
    if (out[0].empty()) out[0] = title.empty() ? "(empty)" : title;
    return out;
  }

  // split by blank lines into paragraphs
  std::vector<std::string> paras;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find("\n\n", i);
    if (j == std::string::npos) j = text.size();
    auto p = util::trim(text.substr(i, j - i));
    if (!p.empty()) paras.push_back(p);
    i = (j == text.size()) ? j : j + 2;
  }

  std::string cur;
  for (const auto& p : paras) {
    if (cur.empty()) {
      cur = p;
      continue;
    }
    if (static_cast<int>(cur.size() + 2 + p.size()) <= opts.target_chars) {
      cur += "\n\n" + p;
    } else {
      out.push_back(cur);
      // overlap tail
      if (opts.overlap > 0 && static_cast<int>(cur.size()) > opts.overlap) {
        cur = cur.substr(cur.size() - static_cast<size_t>(opts.overlap)) + "\n\n" + p;
      } else {
        cur = p;
      }
    }
  }
  if (!cur.empty()) out.push_back(cur);
  if (out.empty()) out.push_back(title.empty() ? "(empty)" : title);
  return out;
}

}  // namespace qbrain::ingest

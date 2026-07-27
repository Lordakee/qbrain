#include "qbrain/codeintel/scan.hpp"
#include "qbrain/util/string_util.hpp"
#include <cctype>
#include <cstring>
#include <functional>
#include <regex>
#include <string_view>
#include <unordered_set>

namespace qbrain::codeintel {
namespace {

std::string escape_re(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (char c : s) {
    if (std::strchr(R"(\.^$|?*+()[]{})", c)) out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// True if [pos, pos+len) is a whole identifier (not part of a longer name).
bool word_boundary_at(const std::string& line, size_t pos, size_t len) {
  if (pos > 0 && is_ident_char(line[pos - 1])) return false;
  if (pos + len < line.size() && is_ident_char(line[pos + len])) return false;
  return true;
}

std::string trim_snippet(const std::string& line, size_t max = 200) {
  auto t = util::trim(line);
  if (t.size() > max) t = t.substr(0, max);
  return t;
}

// Split body into lines preserving 1-based line numbers.
void for_each_line(const std::string& body,
                   const std::function<bool(int, const std::string&)>& fn) {
  int line_no = 1;
  size_t i = 0;
  while (i <= body.size()) {
    size_t j = body.find('\n', i);
    if (j == std::string::npos) j = body.size();
    std::string line = body.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!fn(line_no, line)) return;
    if (j == body.size()) break;
    i = j + 1;
    ++line_no;
  }
}

bool is_control_kw(std::string_view w) {
  return w == "return" || w == "if" || w == "else" || w == "for" || w == "while" ||
         w == "switch" || w == "case" || w == "sizeof" || w == "new" || w == "delete" ||
         w == "throw" || w == "co_return" || w == "co_await" || w == "co_yield" ||
         w == "using" || w == "typedef" || w == "goto";
}

bool looks_like_def(const std::string& line, const std::string& symbol) {
  auto t = util::trim(line);
  if (t.empty()) return false;
  if (util::starts_with(t, "//") || util::starts_with(t, "/*") || util::starts_with(t, "*") ||
      util::starts_with(t, "#"))
    return false;

  const std::string esc = escape_re(symbol);

  // class/struct/interface/enum/type/namespace Symbol
  {
    std::regex re(R"(\b(?:class|struct|interface|enum|type|namespace)\s+)" + esc + R"(\b)",
                  std::regex::ECMAScript);
    if (std::regex_search(line, re)) return true;
  }

  // function Symbol / async function Symbol / def Symbol
  {
    std::regex re(R"(\b(?:async\s+)?(?:function|def)\s+)" + esc + R"(\b)",
                  std::regex::ECMAScript);
    if (std::regex_search(line, re)) return true;
  }

  // const/let/var Symbol =  or  const Symbol:
  {
    std::regex re(R"(\b(?:const|let|var)\s+)" + esc + R"(\s*[=:])", std::regex::ECMAScript);
    if (std::regex_search(line, re)) return true;
  }

  // ReturnType Symbol( — type token must start with letter/_; reject control keywords
  {
    std::regex re(R"(\b([A-Za-z_][\w:<>*&\[\]]*)\s+)" + esc + R"(\s*\()",
                  std::regex::ECMAScript);
    std::smatch m;
    auto search_from = line.cbegin();
    while (std::regex_search(search_from, line.cend(), m, re)) {
      if (!is_control_kw(m[1].str())) return true;
      search_from = m[0].second;
    }
  }

  // Method/ctor: Symbol(args) {  or  Symbol(args) const {
  {
    std::regex re(R"(^\s*)" + esc + R"(\s*\([^;{]*\)\s*(?:const\s*)?(?:override\s*)?\{)",
                  std::regex::ECMAScript);
    if (std::regex_search(line, re)) return true;
  }

  // TS method shorthand: Symbol(args): Type
  {
    std::regex re(R"((?:^|[\s,{])\s*)" + esc + R"(\s*\([^;{]*\)\s*:)", std::regex::ECMAScript);
    if (std::regex_search(line, re)) return true;
  }

  return false;
}

bool looks_like_call(const std::string& line, const std::string& symbol) {
  size_t pos = 0;
  while (pos < line.size()) {
    auto found = line.find(symbol, pos);
    if (found == std::string::npos) return false;
    if (word_boundary_at(line, found, symbol.size())) {
      size_t after = found + symbol.size();
      while (after < line.size() &&
             std::isspace(static_cast<unsigned char>(line[after])))
        ++after;
      if (after < line.size() && line[after] == '(') return true;
    }
    pos = found + 1;
  }
  return false;
}

bool has_word_ref(const std::string& line, const std::string& symbol) {
  size_t pos = 0;
  while (pos < line.size()) {
    auto found = line.find(symbol, pos);
    if (found == std::string::npos) return false;
    if (word_boundary_at(line, found, symbol.size())) return true;
    pos = found + 1;
  }
  return false;
}

enum class Mode { Def, Ref, Call };

std::vector<Hit> scan(Brain& brain, const std::string& symbol, int limit, int page_limit,
                      Mode mode) {
  std::vector<Hit> out;
  if (symbol.empty() || limit <= 0) return out;
  // Reject pathological symbols (no regex injection / empty tokens)
  for (char c : symbol) {
    if (!(is_ident_char(c) || c == ':' || c == '~')) return out;
  }

  auto pages = brain.list_pages(page_limit <= 0 ? 500 : page_limit, "");
  for (auto& p : pages) {
    if (static_cast<int>(out.size()) >= limit) break;
    for_each_line(p.body, [&](int line_no, const std::string& line) -> bool {
      if (static_cast<int>(out.size()) >= limit) return false;
      bool hit = false;
      const char* kind = "ref";
      switch (mode) {
        case Mode::Def:
          hit = looks_like_def(line, symbol);
          kind = "def";
          break;
        case Mode::Call:
          hit = looks_like_call(line, symbol);
          kind = "call";
          break;
        case Mode::Ref:
          hit = has_word_ref(line, symbol);
          kind = "ref";
          break;
      }
      if (hit) {
        Hit h;
        h.slug = p.slug;
        h.line = line_no;
        h.snippet = trim_snippet(line);
        h.kind = kind;
        out.push_back(std::move(h));
      }
      return true;
    });
  }
  return out;
}

}  // namespace

std::vector<Hit> find_defs(Brain& brain, const std::string& symbol, int limit, int page_limit) {
  return scan(brain, symbol, limit, page_limit, Mode::Def);
}

std::vector<Hit> find_refs(Brain& brain, const std::string& symbol, int limit, int page_limit) {
  return scan(brain, symbol, limit, page_limit, Mode::Ref);
}

std::vector<Hit> find_callers(Brain& brain, const std::string& symbol, int limit,
                              int page_limit) {
  return scan(brain, symbol, limit, page_limit, Mode::Call);
}

namespace {

// Extract identifier tokens that appear with '(' on a line (callee candidates).
std::vector<std::string> call_tokens_on_line(const std::string& line) {
  std::vector<std::string> out;
  static const std::regex re(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()", std::regex::ECMAScript);
  auto begin = std::sregex_iterator(line.begin(), line.end(), re);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    auto name = (*it)[1].str();
    if (!is_control_kw(name)) out.push_back(name);
  }
  return out;
}

}  // namespace

std::vector<Hit> find_callees(Brain& brain, const std::string& symbol, int limit,
                              int page_limit) {
  std::vector<Hit> out;
  if (symbol.empty()) return out;
  auto defs = find_defs(brain, symbol, 20, page_limit);
  // Also scan pages that contain the def symbol for nearby call tokens
  auto pages = brain.list_pages(page_limit <= 0 ? 500 : page_limit, "");
  for (auto& p : pages) {
    if (static_cast<int>(out.size()) >= limit) break;
    bool in_scope = false;
    for (auto& d : defs)
      if (d.slug == p.slug) in_scope = true;
    if (!in_scope && p.body.find(symbol) == std::string::npos) continue;
    for_each_line(p.body, [&](int line_no, const std::string& line) -> bool {
      if (static_cast<int>(out.size()) >= limit) return false;
      // Prefer lines after a def of symbol, or any line in pages that define it
      bool near_def = looks_like_def(line, symbol);
      if (near_def) in_scope = true;
      if (!in_scope && !defs.empty()) return true;
      for (auto& tok : call_tokens_on_line(line)) {
        if (tok == symbol) continue;
        Hit h;
        h.slug = p.slug;
        h.line = line_no;
        h.snippet = trim_snippet(line);
        h.kind = "callee:" + tok;
        out.push_back(std::move(h));
        if (static_cast<int>(out.size()) >= limit) return false;
      }
      return true;
    });
  }
  return out;
}

std::vector<Hit> find_flow(Brain& brain, const std::string& symbol, int depth, int limit,
                           int page_limit) {
  std::vector<Hit> out;
  if (symbol.empty() || depth < 1) return out;
  std::vector<std::string> frontier = {symbol};
  std::unordered_set<std::string> seen;
  seen.insert(symbol);
  for (int d = 0; d < depth && static_cast<int>(out.size()) < limit; ++d) {
    std::vector<std::string> next;
    for (auto& sym : frontier) {
      auto callees = find_callees(brain, sym, limit, page_limit);
      for (auto& c : callees) {
        if (static_cast<int>(out.size()) >= limit) break;
        // kind is callee:Name
        std::string name = c.kind;
        if (name.rfind("callee:", 0) == 0) name = name.substr(7);
        c.kind = "flow:d" + std::to_string(d + 1) + ":" + name;
        out.push_back(c);
        if (!seen.count(name)) {
          seen.insert(name);
          next.push_back(name);
        }
      }
    }
    frontier = std::move(next);
  }
  return out;
}

std::vector<Hit> find_blast(Brain& brain, const std::string& symbol, int limit, int page_limit) {
  std::vector<Hit> out;
  auto add = [&](std::vector<Hit> part) {
    for (auto& h : part) {
      if (static_cast<int>(out.size()) >= limit) break;
      out.push_back(std::move(h));
    }
  };
  add(find_defs(brain, symbol, limit, page_limit));
  add(find_refs(brain, symbol, limit, page_limit));
  add(find_callers(brain, symbol, limit, page_limit));
  add(find_callees(brain, symbol, limit, page_limit));
  if (static_cast<int>(out.size()) > limit) out.resize(static_cast<size_t>(limit));
  return out;
}

void clear_traversal_cache() {
  // Stateless scanners — no cache to clear (API parity no-op).
}

}  // namespace qbrain::codeintel

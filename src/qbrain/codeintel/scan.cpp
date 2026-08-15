#include "qbrain/codeintel/scan.hpp"
#include "qbrain/util/string_util.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
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
  const auto u = static_cast<unsigned char>(c);
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
         (u >= '0' && u <= '9') || c == '_' || c == '$';
}

bool is_symbol_start(char c) {
  const auto u = static_cast<unsigned char>(c);
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || c == '_' || c == '$';
}

bool is_symbol_continue(char c) {
  return is_ident_char(c);
}

// True if [pos, pos+len) is a whole identifier (not part of a longer name).
bool word_boundary_at(const std::string& line, size_t pos, size_t len) {
  if (pos > 0 && is_ident_char(line[pos - 1])) return false;
  if (pos + len < line.size() && is_ident_char(line[pos + len])) return false;
  return true;
}

bool is_utf8_continuation(unsigned char byte) {
  return byte >= 0x80 && byte <= 0xBF;
}

size_t utf8_code_point_length(std::string_view value, size_t offset) {
  const auto byte_at = [&value](size_t index) {
    return static_cast<unsigned char>(value[index]);
  };
  const unsigned char first = byte_at(offset);
  const size_t remaining = value.size() - offset;
  if (first <= 0x7F) return 1;
  if (first >= 0xC2 && first <= 0xDF)
    return remaining >= 2 && is_utf8_continuation(byte_at(offset + 1)) ? 2 : 0;
  if (first == 0xE0)
    return remaining >= 3 && byte_at(offset + 1) >= 0xA0 && byte_at(offset + 1) <= 0xBF &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if ((first >= 0xE1 && first <= 0xEC) || (first >= 0xEE && first <= 0xEF))
    return remaining >= 3 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xED)
    return remaining >= 3 && byte_at(offset + 1) >= 0x80 && byte_at(offset + 1) <= 0x9F &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  if (first == 0xF0)
    return remaining >= 4 && byte_at(offset + 1) >= 0x90 && byte_at(offset + 1) <= 0xBF &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  if (first >= 0xF1 && first <= 0xF3)
    return remaining >= 4 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  if (first == 0xF4)
    return remaining >= 4 && byte_at(offset + 1) >= 0x80 && byte_at(offset + 1) <= 0x8F &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  return 0;
}

std::string bounded_utf8(std::string_view value, size_t maximum) {
  constexpr std::string_view replacement = "\xEF\xBF\xBD";
  std::string output;
  output.reserve(std::min(value.size(), maximum));
  for (size_t offset = 0; offset < value.size();) {
    const size_t length = utf8_code_point_length(value, offset);
    const size_t unit_size = length == 0 ? replacement.size() : length;
    if (unit_size > maximum - output.size()) break;
    if (length == 0)
      output.append(replacement);
    else
      output.append(value.data() + offset, length);
    offset += length == 0 ? 1 : length;
  }
  return output;
}

std::string trim_snippet(const std::string& line, size_t max = 200) {
  auto t = util::trim(line);
  return bounded_utf8(t, max);
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
         w == "do" || w == "switch" || w == "case" || w == "break" ||
         w == "continue" || w == "try" || w == "catch" || w == "sizeof" ||
         w == "alignof" || w == "alignas" || w == "decltype" || w == "noexcept" ||
         w == "typeid" || w == "static_assert" || w == "assert" || w == "asm" ||
         w == "new" || w == "delete" || w == "throw" || w == "co_return" ||
         w == "co_await" || w == "co_yield" || w == "await" || w == "yield" ||
         w == "requires" || w == "function" || w == "using" || w == "typedef" ||
         w == "goto";
}

class DefinitionMatcher {
 public:
  explicit DefinitionMatcher(std::string symbol)
      : symbol_(std::move(symbol)),
        named_type_(R"(\b(?:class|struct|interface|enum|type|namespace)\s+)" +
                        escape_re(symbol_) + R"((?:$|[^A-Za-z0-9_$]))",
                    std::regex::ECMAScript),
        named_function_(R"(\b(?:async\s+)?(?:function|def)\s+)" + escape_re(symbol_) +
                            R"((?:$|[^A-Za-z0-9_$]))",
                        std::regex::ECMAScript),
        named_value_(R"(\b(?:const|let|var)\s+)" + escape_re(symbol_) + R"(\s*[=:])",
                     std::regex::ECMAScript),
        typed_function_(R"(\b([A-Za-z_][\w:<>*&\[\]]*)\s+)" + escape_re(symbol_) +
                            R"(\s*\()",
                        std::regex::ECMAScript),
        method_(R"(^\s*)" + escape_re(symbol_) +
                    R"(\s*\([^;{]*\)\s*(?:const\s*)?(?:override\s*)?\{)",
                std::regex::ECMAScript),
        ts_method_(R"((?:^|[\s,{])\s*)" + escape_re(symbol_) +
                       R"(\s*\([^;{]*\)\s*:)",
                   std::regex::ECMAScript) {}

  const std::string& symbol() const { return symbol_; }

  std::vector<size_t> declaration_ends(const std::string& line) const {
    std::vector<size_t> ends;
    if (line.find(symbol_) == std::string::npos) return ends;
    const auto trimmed = util::trim(line);
    if (trimmed.empty() || util::starts_with(trimmed, "//") ||
        util::starts_with(trimmed, "/*") || util::starts_with(trimmed, "*") ||
        util::starts_with(trimmed, "#"))
      return ends;

    const auto append_match = [&](const std::smatch& match) {
      const auto match_begin =
          static_cast<size_t>(std::distance(line.cbegin(), match[0].first));
      const auto match_end =
          static_cast<size_t>(std::distance(line.cbegin(), match[0].second));
      const auto symbol_begin = line.find(symbol_, match_begin);
      if (symbol_begin == std::string::npos || symbol_begin >= match_end) return;
      ends.push_back(symbol_begin + symbol_.size());
    };

    std::smatch match;
    const auto collect = [&](const auto& expression, const auto& accept) {
      auto search_from = line.cbegin();
      while (std::regex_search(search_from, line.cend(), match, expression)) {
        if (accept(match)) append_match(match);
        if (match[0].second == search_from) {
          if (search_from == line.cend()) break;
          ++search_from;
        } else {
          search_from = match[0].second;
        }
      }
    };

    collect(named_type_, [](const std::smatch&) { return true; });
    collect(named_function_, [](const std::smatch&) { return true; });
    collect(named_value_, [](const std::smatch&) { return true; });

    collect(typed_function_, [](const std::smatch& candidate) {
      return !is_control_kw(candidate[1].str());
    });
    collect(method_, [](const std::smatch&) { return true; });
    collect(ts_method_, [](const std::smatch&) { return true; });

    std::sort(ends.begin(), ends.end());
    ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
    return ends;
  }

  bool matches(const std::string& line, size_t* declaration_end = nullptr) const {
    const auto ends = declaration_ends(line);
    if (ends.empty()) return false;
    if (declaration_end) *declaration_end = ends.front();
    return true;
  }

 private:
  std::string symbol_;
  std::regex named_type_;
  std::regex named_function_;
  std::regex named_value_;
  std::regex typed_function_;
  std::regex method_;
  std::regex ts_method_;
};

bool looks_like_def(const std::string& line, const std::string& symbol,
                    size_t* declaration_end = nullptr) {
  return DefinitionMatcher(symbol).matches(line, declaration_end);
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

using HitVisitor = std::function<bool(Hit&&)>;

void for_each_source_hit(Brain& brain, const std::string& source_id,
                         const std::string& symbol, int page_limit, Mode mode,
                         const HitVisitor& visitor) {
  auto pages = brain.list_pages_for_source(source_id, page_limit);
  const DefinitionMatcher definition_matcher(symbol);
  for (const auto& page : pages) {
    bool keep_scanning = true;
    for_each_line(page.body, [&](int line_no, const std::string& line) -> bool {
      bool hit = false;
      const char* kind = "ref";
      switch (mode) {
        case Mode::Def:
          hit = definition_matcher.matches(line);
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
      if (!hit) return true;

      Hit result;
      result.source_id = page.source_id;
      result.slug = page.slug;
      result.line = line_no;
      result.snippet = trim_snippet(line);
      result.kind = kind;
      keep_scanning = visitor(std::move(result));
      return keep_scanning;
    });
    if (!keep_scanning) return;
  }
}

std::vector<Hit> scan(Brain& brain, const std::string& symbol, int limit, int page_limit,
                      Mode mode) {
  std::vector<Hit> out;
  if (symbol.empty() || limit <= 0) return out;
  // Reject pathological symbols (no regex injection / empty tokens)
  for (char c : symbol) {
    if (!(is_ident_char(c) || c == ':' || c == '~')) return out;
  }

  auto pages = brain.list_pages(page_limit <= 0 ? 500 : page_limit, "");
  const DefinitionMatcher definition_matcher(symbol);
  for (auto& p : pages) {
    if (static_cast<int>(out.size()) >= limit) break;
    for_each_line(p.body, [&](int line_no, const std::string& line) -> bool {
      if (static_cast<int>(out.size()) >= limit) return false;
      bool hit = false;
      const char* kind = "ref";
      switch (mode) {
        case Mode::Def:
          hit = definition_matcher.matches(line);
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

std::vector<Hit> scan_source(Brain& brain, const std::string& source_id,
                             const std::string& symbol, int limit, int page_limit,
                             Mode mode) {
  std::vector<Hit> out;
  if (source_id.empty() || !is_valid_symbol(symbol)) return out;
  if (limit <= 0) limit = 1;
  if (page_limit <= 0) page_limit = 1;
  if (limit > 200) limit = 200;
  if (page_limit > 2000) page_limit = 2000;

  for_each_source_hit(brain, source_id, symbol, page_limit, mode, [&](Hit&& hit) {
    out.push_back(std::move(hit));
    return static_cast<int>(out.size()) < limit;
  });
  return out;
}

}  // namespace

bool is_valid_symbol(std::string_view symbol) {
  if (symbol.empty() || symbol.size() > 256) return false;
  size_t pos = 0;
  while (pos < symbol.size()) {
    if (symbol[pos] == '~') ++pos;
    if (pos >= symbol.size() || !is_symbol_start(symbol[pos])) return false;
    ++pos;
    while (pos < symbol.size() && is_symbol_continue(symbol[pos])) ++pos;
    if (pos == symbol.size()) return true;
    if (symbol[pos] != ':' || pos + 1 >= symbol.size() || symbol[pos + 1] != ':') return false;
    pos += 2;
  }
  return false;
}

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

std::vector<Hit> find_defs_in_source(Brain& brain, const std::string& source_id,
                                     const std::string& symbol, int limit, int page_limit) {
  return scan_source(brain, source_id, symbol, limit, page_limit, Mode::Def);
}

std::vector<Hit> find_refs_in_source(Brain& brain, const std::string& source_id,
                                     const std::string& symbol, int limit, int page_limit) {
  return scan_source(brain, source_id, symbol, limit, page_limit, Mode::Ref);
}

std::vector<Hit> find_callers_in_source(Brain& brain, const std::string& source_id,
                                        const std::string& symbol, int limit, int page_limit) {
  return scan_source(brain, source_id, symbol, limit, page_limit, Mode::Call);
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

constexpr int kN22MaximumResults = 200;
constexpr int kN22MaximumPages = 2000;
constexpr int kN22MaximumDepth = 8;
constexpr size_t kN22MaximumSymbolBytes = 256;
constexpr size_t kN22OpeningBraceLookahead = 10;
constexpr size_t kN22MaximumSlugBytes = 512;
constexpr size_t kN22MaximumPageBodyBytes = 16 * 1024;
constexpr size_t kN22MaximumSourceBytes = 8 * 1024 * 1024;
constexpr size_t kN22MaximumSourceLines = 16 * 1024;
constexpr size_t kN22MaximumScanWork = 4 * 1024 * 1024;

struct ScanWorkBudget {
  size_t remaining = kN22MaximumScanWork;

  void consume(size_t amount = 1) {
    if (amount > remaining) throw std::length_error("N22 source-text work budget exceeded");
    remaining -= amount;
  }
};

bool is_valid_utf8(std::string_view value) {
  for (size_t offset = 0; offset < value.size();) {
    const size_t length = utf8_code_point_length(value, offset);
    if (length == 0) return false;
    offset += length;
  }
  return true;
}

bool is_safe_n22_slug(std::string_view slug) {
  if (slug.empty() || slug.size() > kN22MaximumSlugBytes || !is_valid_utf8(slug)) return false;
  for (const unsigned char byte : slug) {
    if (byte < 0x20 || byte == 0x7f) return false;
  }
  return true;
}

int clamp_n22_bound(int value, int maximum) {
  return std::clamp(value, 1, maximum);
}

bool is_ascii_call_start(char value) {
  const auto byte = static_cast<unsigned char>(value);
  return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || value == '_';
}

bool is_ascii_call_continue(char value) {
  const auto byte = static_cast<unsigned char>(value);
  return is_ascii_call_start(value) || (byte >= '0' && byte <= '9');
}

std::vector<std::string> call_tokens_in_range(const std::string& line, size_t begin,
                                              size_t end) {
  std::vector<std::string> out;
  begin = std::min(begin, line.size());
  end = std::min(end, line.size());
  for (size_t pos = begin; pos < end;) {
    if (!is_ascii_call_start(line[pos]) ||
        (pos > 0 && is_ascii_call_continue(line[pos - 1]))) {
      ++pos;
      continue;
    }

    const size_t name_begin = pos++;
    while (pos < end && is_ascii_call_continue(line[pos])) ++pos;
    const std::string_view name(line.data() + name_begin, pos - name_begin);
    size_t after = pos;
    while (after < end && std::isspace(static_cast<unsigned char>(line[after]))) ++after;
    if (after < end && line[after] == '(' && name.size() <= kN22MaximumSymbolBytes &&
        !is_control_kw(name))
      out.emplace_back(name);
  }
  return out;
}

std::vector<std::string> physical_lines(const std::string& body) {
  std::vector<std::string> lines;
  for_each_line(body, [&](int, const std::string& line) {
    lines.push_back(line);
    return true;
  });
  return lines;
}

struct PreparedPage {
  std::string source_id;
  std::string slug;
  std::vector<std::string> lines;
};

std::vector<PreparedPage> prepare_pages(const std::vector<Page>& pages) {
  std::vector<PreparedPage> prepared;
  prepared.reserve(pages.size());
  size_t total_bytes = 0;
  size_t total_lines = 0;
  for (const auto& page : pages) {
    if (!is_safe_n22_slug(page.slug))
      throw std::length_error("N22 stored slug is outside the output contract");
    if (page.body.size() > kN22MaximumPageBodyBytes)
      throw std::length_error("N22 page body exceeds the scan budget");
    if (page.body.size() > kN22MaximumSourceBytes - total_bytes)
      throw std::length_error("N22 source text exceeds the scan budget");
    auto lines = physical_lines(page.body);
    if (lines.size() > kN22MaximumSourceLines - total_lines)
      throw std::length_error("N22 source line count exceeds the scan budget");
    total_bytes += page.body.size();
    total_lines += lines.size();
    prepared.push_back(PreparedPage{page.source_id, page.slug, std::move(lines)});
  }
  return prepared;
}

std::vector<PreparedPage> load_n22_pages(Brain& brain, const std::string& source_id,
                                         int page_limit) {
  return prepare_pages(brain.list_pages_for_source(source_id, page_limit));
}

bool looks_like_independent_definition_start(const std::string& line) {
  const auto trimmed = util::trim(line);
  if (trimmed.empty() || util::starts_with(trimmed, "//") ||
      util::starts_with(trimmed, "/*") || util::starts_with(trimmed, "*") ||
      util::starts_with(trimmed, "#"))
    return false;

  static const std::regex named_definition(
      R"(\b(?:class|struct|interface|enum|type|namespace|function|def)\s+[A-Za-z_][A-Za-z0-9_$]*)",
      std::regex::ECMAScript);
  if (std::regex_search(line, named_definition)) return true;

  for (const auto& candidate : call_tokens_in_range(line, 0, line.size())) {
    if (DefinitionMatcher(candidate).matches(line)) return true;
  }
  return false;
}

bool find_opening_brace(const std::vector<std::string>& lines, size_t definition_line,
                         size_t declaration_end, size_t& opening_line,
                         size_t& opening_column, ScanWorkBudget& budget) {
  if (definition_line >= lines.size()) return false;
  const size_t last_line =
      std::min(lines.size() - 1, definition_line + kN22OpeningBraceLookahead);
  for (size_t line_index = definition_line; line_index <= last_line; ++line_index) {
    if (line_index != definition_line &&
        looks_like_independent_definition_start(lines[line_index]))
      return false;
    const size_t search_from = line_index == definition_line ? declaration_end : 0;
    budget.consume(lines[line_index].size() - std::min(search_from, lines[line_index].size()) + 1);
    const size_t column = lines[line_index].find('{', search_from);
    const size_t declaration_terminator = lines[line_index].find(';', search_from);
    if (declaration_terminator != std::string::npos &&
        (column == std::string::npos || declaration_terminator < column))
      return false;
    if (column != std::string::npos) {
      opening_line = line_index;
      opening_column = column;
      return true;
    }
  }
  return false;
}

bool find_closing_brace(const std::vector<std::string>& lines, size_t opening_line,
                         size_t opening_column, size_t& closing_line,
                         size_t& closing_column, ScanWorkBudget& budget) {
  int depth = 1;
  for (size_t line_index = opening_line; line_index < lines.size(); ++line_index) {
    const auto& line = lines[line_index];
    const size_t begin = line_index == opening_line ? opening_column + 1 : 0;
    budget.consume(line.size() - std::min(begin, line.size()) + 1);
    for (size_t column = begin; column < line.size(); ++column) {
      if (line[column] == '{') {
        ++depth;
      } else if (line[column] == '}' && --depth == 0) {
        closing_line = line_index;
        closing_column = column;
        return true;
      }
    }
  }
  return false;
}

using CalleeOccurrenceKey =
    std::tuple<std::string, std::string, int, std::string>;
using BlastLineKey = std::tuple<std::string, std::string, int>;

bool for_each_definition_callee(const PreparedPage& prepared, size_t definition_line,
                                size_t declaration_end,
                                const HitVisitor& visitor, ScanWorkBudget& budget) {
  size_t opening_line = 0;
  size_t opening_column = 0;
  if (!find_opening_brace(prepared.lines, definition_line, declaration_end, opening_line,
                          opening_column, budget))
    return true;

  size_t closing_line = 0;
  size_t closing_column = 0;
  if (!find_closing_brace(prepared.lines, opening_line, opening_column, closing_line,
                          closing_column, budget))
    return true;

  for (size_t line_index = opening_line; line_index <= closing_line; ++line_index) {
    const auto& line = prepared.lines[line_index];
    const size_t begin = line_index == opening_line ? opening_column + 1 : 0;
    const size_t end = line_index == closing_line ? closing_column : line.size();
    for (const auto& target : call_tokens_in_range(line, begin, end)) {
      Hit hit;
      hit.source_id = prepared.source_id;
      hit.slug = prepared.slug;
      hit.line = static_cast<int>(line_index + 1);
      hit.snippet = trim_snippet(line);
      hit.kind = "callee:" + target;
      if (!visitor(std::move(hit))) return false;
    }
  }
  return true;
}

void for_each_callee_occurrence(const std::vector<PreparedPage>& pages,
                                 const DefinitionMatcher& matcher,
                                 const HitVisitor& visitor, ScanWorkBudget& budget) {
  for (const auto& page : pages) {
    for (size_t definition_line = 0; definition_line < page.lines.size(); ++definition_line) {
      budget.consume(page.lines[definition_line].size() + 1);
      const auto declaration_ends = matcher.declaration_ends(page.lines[definition_line]);
      for (const size_t declaration_end : declaration_ends) {
        if (!for_each_definition_callee(page, definition_line, declaration_end, visitor,
                                         budget))
          return;
      }
    }
  }
}

std::unordered_map<std::string, std::vector<Hit>> collect_frontier_callees(
    const std::vector<PreparedPage>& pages, const std::vector<std::string>& frontier,
    ScanWorkBudget& budget) {
  std::vector<DefinitionMatcher> matchers;
  matchers.reserve(frontier.size());
  for (const auto& symbol : frontier) matchers.emplace_back(symbol);

  std::unordered_map<std::string, std::vector<Hit>> results;
  std::unordered_map<std::string, std::unordered_set<std::string>> targets;
  for (const auto& symbol : frontier) {
    results.emplace(symbol, std::vector<Hit>{});
    targets.emplace(symbol, std::unordered_set<std::string>{});
  }

  for (const auto& page : pages) {
    for (size_t definition_line = 0; definition_line < page.lines.size(); ++definition_line) {
      for (const auto& matcher : matchers) {
        budget.consume(page.lines[definition_line].size() + 1);
        auto& hits = results.at(matcher.symbol());
        if (hits.size() >= static_cast<size_t>(kN22MaximumResults + 1)) continue;
        size_t declaration_end = 0;
        if (!matcher.matches(page.lines[definition_line], &declaration_end)) continue;
        auto& emitted_targets = targets.at(matcher.symbol());
        for_each_definition_callee(
            page, definition_line, declaration_end, [&](Hit&& hit) {
              constexpr std::string_view prefix = "callee:";
              const std::string target = hit.kind.substr(prefix.size());
              if (emitted_targets.insert(target).second) hits.push_back(std::move(hit));
              return hits.size() < static_cast<size_t>(kN22MaximumResults + 1);
            }, budget);
      }
    }
  }
  return results;
}

}  // namespace

std::vector<Hit> find_callees_in_source(Brain& brain, const std::string& source_id,
                                        const std::string& symbol, int limit,
                                        int page_limit) {
  std::vector<Hit> out;
  if (source_id.empty() || !is_valid_symbol(symbol)) return out;
  limit = clamp_n22_bound(limit, kN22MaximumResults);
  page_limit = clamp_n22_bound(page_limit, kN22MaximumPages);

  std::set<CalleeOccurrenceKey> emitted;
  const auto prepared = load_n22_pages(brain, source_id, page_limit);
  ScanWorkBudget budget;
  const DefinitionMatcher matcher(symbol);
  for_each_callee_occurrence(prepared, matcher, [&](Hit&& hit) {
    CalleeOccurrenceKey key{hit.source_id, hit.slug, hit.line, hit.kind};
    if (emitted.insert(std::move(key)).second) out.push_back(std::move(hit));
    return static_cast<int>(out.size()) < limit;
  }, budget);
  return out;
}

std::vector<Hit> find_flow_in_source(Brain& brain, const std::string& source_id,
                                     const std::string& symbol, int depth, int limit,
                                     int page_limit) {
  std::vector<Hit> out;
  if (source_id.empty() || !is_valid_symbol(symbol)) return out;
  depth = clamp_n22_bound(depth, kN22MaximumDepth);
  limit = clamp_n22_bound(limit, kN22MaximumResults);
  page_limit = clamp_n22_bound(page_limit, kN22MaximumPages);

  const auto prepared = load_n22_pages(brain, source_id, page_limit);
  ScanWorkBudget budget;
  std::vector<std::string> frontier{symbol};
  std::unordered_set<std::string> seen{symbol};
  for (int current_depth = 1;
       current_depth <= depth && !frontier.empty() && static_cast<int>(out.size()) < limit;
       ++current_depth) {
    std::vector<std::string> next;
    auto callees_by_parent = collect_frontier_callees(prepared, frontier, budget);
    for (const auto& parent : frontier) {
      for (auto& hit : callees_by_parent.at(parent)) {
        constexpr std::string_view prefix = "callee:";
        if (hit.kind.rfind(prefix, 0) != 0) continue;
        const std::string target = hit.kind.substr(prefix.size());
        if (!seen.insert(target).second) continue;

        hit.kind = "flow:d" + std::to_string(current_depth) + ":" + target;
        out.push_back(std::move(hit));
        next.push_back(target);
        if (static_cast<int>(out.size()) >= limit) break;
      }
      if (static_cast<int>(out.size()) >= limit) break;
    }
    frontier = std::move(next);
  }
  return out;
}

std::vector<Hit> find_blast_in_source(Brain& brain, const std::string& source_id,
                                      const std::string& symbol, int limit,
                                      int page_limit) {
  std::vector<Hit> out;
  if (source_id.empty() || !is_valid_symbol(symbol)) return out;
  limit = clamp_n22_bound(limit, kN22MaximumResults);
  page_limit = clamp_n22_bound(page_limit, kN22MaximumPages);

  std::set<BlastLineKey> emitted;
  const auto append_source_category = [&](std::vector<Hit> hits) {
    for (auto& hit : hits) {
      BlastLineKey key{hit.source_id, hit.slug, hit.line};
      if (emitted.insert(std::move(key)).second) out.push_back(std::move(hit));
      if (static_cast<int>(out.size()) >= limit) break;
    }
  };

  append_source_category(find_defs_in_source(brain, source_id, symbol, limit, page_limit));
  if (static_cast<int>(out.size()) < limit)
    append_source_category(find_refs_in_source(brain, source_id, symbol, limit, page_limit));
  if (static_cast<int>(out.size()) < limit)
    append_source_category(find_callers_in_source(brain, source_id, symbol, limit, page_limit));
  if (static_cast<int>(out.size()) < limit)
    append_source_category(find_callees_in_source(brain, source_id, symbol, limit, page_limit));
  return out;
}

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

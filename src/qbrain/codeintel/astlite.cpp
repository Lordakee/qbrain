#include "qbrain/codeintel/astlite.hpp"
#include <chrono>
#include <iterator>
#include <utility>

namespace qbrain::codeintel::astlite {
namespace {

// ---------------------------------------------------------------------------
// Character classification
// ---------------------------------------------------------------------------

bool is_ident_start(char c) {
  const auto u = static_cast<unsigned char>(c);
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || c == '_' || c == '$';
}

bool is_ident_continue(char c) {
  const auto u = static_cast<unsigned char>(c);
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
         (u >= '0' && u <= '9') || c == '_' || c == '$';
}

bool is_digit(char c) {
  const auto u = static_cast<unsigned char>(c);
  return u >= '0' && u <= '9';
}

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

// ---------------------------------------------------------------------------
// Keyword sets (no regex anywhere in astlite)
// ---------------------------------------------------------------------------

constexpr std::string_view kCppKeywords[] = {
    "alignas",     "alignof",    "and",       "and_eq",      "asm",
    "auto",        "bitand",     "bitor",     "bool",        "break",
    "case",        "catch",      "char",      "char16_t",    "char32_t",
    "char8_t",     "class",      "compl",     "concept",     "const",
    "const_cast",  "consteval",  "constexpr", "constinit",   "continue",
    "co_await",    "co_return",  "co_yield",  "decltype",    "default",
    "delete",      "do",         "double",    "dynamic_cast", "else",
    "enum",        "explicit",   "export",    "extern",      "false",
    "float",       "for",        "friend",    "goto",        "if",
    "inline",      "int",        "long",      "mutable",     "namespace",
    "new",         "noexcept",   "not",       "not_eq",      "nullptr",
    "operator",    "or",         "or_eq",     "private",     "protected",
    "public",      "register",   "reinterpret_cast",         "requires",
    "return",      "short",      "signed",    "sizeof",      "static",
    "static_assert", "static_cast", "final",  "struct",      "switch",
    "template",   "override",
    "this",        "thread_local", "throw",   "true",        "try",
    "typedef",     "typeid",     "typename",  "union",       "unsigned",
    "using",       "virtual",    "void",      "volatile",    "wchar_t",
    "while",       "xor",        "xor_eq",
};

// Contextual keywords that commonly appear as method names ("from", "of",
// "type") are deliberately NOT keywords here: losing a definition such as
// "static from(...)" is worse than a stray reference in an import clause.
constexpr std::string_view kTsKeywords[] = {
    "abstract",   "any",      "as",         "async",     "await",
    "boolean",    "break",    "case",       "catch",     "class",
    "const",      "continue", "constructor", "debugger", "declare",
    "default",    "delete",   "do",         "else",      "enum",
    "export",     "extends",  "false",      "finally",   "for",
    "function",   "get",      "if",         "implements", "import",
    "in",         "infer",    "instanceof", "interface", "is",
    "keyof",      "let",      "module",     "namespace", "new",
    "null",       "number",   "object",     "package",   "private",
    "protected",  "public",   "readonly",   "require",   "return",
    "set",        "static",   "string",     "super",     "switch",
    "symbol",     "this",     "throw",      "true",      "try",
    "typeof",     "undefined", "unknown",   "var",       "void",
    "while",      "with",     "yield",
};

bool in_keyword_set(std::string_view word, const std::string_view* set,
                    std::size_t count) {
  for (std::size_t i = 0; i < count; ++i)
    if (set[i] == word) return true;
  return false;
}

bool is_cpp_keyword(std::string_view word) {
  return in_keyword_set(word, kCppKeywords, std::size(kCppKeywords));
}

bool is_ts_keyword(std::string_view word) {
  return in_keyword_set(word, kTsKeywords, std::size(kTsKeywords));
}

bool is_keyword(Language lang, std::string_view word) {
  return lang == Language::Cpp ? is_cpp_keyword(word) : is_ts_keyword(word);
}

// Keywords that never introduce a definition at declaration scopes; an
// identifier followed by '(' after one of these is a call.
bool is_cpp_expression_keyword(std::string_view word) {
  return word == "new" || word == "delete" || word == "return" ||
         word == "throw" || word == "co_return" || word == "co_await" ||
         word == "co_yield" || word == "sizeof" || word == "alignof" ||
         word == "decltype" || word == "typeid" || word == "static_cast" ||
         word == "dynamic_cast" || word == "const_cast" ||
         word == "reinterpret_cast" || word == "noexcept" ||
         word == "static_assert" || word == "asm" || word == "goto" ||
         word == "break" || word == "continue" || word == "case";
}

bool is_ts_expression_keyword(std::string_view word) {
  return word == "return" || word == "throw" || word == "new" ||
         word == "delete" || word == "typeof" || word == "await" ||
         word == "case" || word == "in" || word == "instanceof";
}

// C++ keywords allowed between a parameter list and the body / semicolon of a
// confirmed candidate (qualifiers).
bool is_cpp_stay_keyword(std::string_view word) {
  return word == "const" || word == "noexcept" || word == "override" ||
         word == "final" || word == "throw" || word == "volatile";
}

// ---------------------------------------------------------------------------
// Pure lexeme skipping (shared by the main pass and the brace rescan).
// Every helper advances (pos, line, col) past one lexeme, staying inside the
// body. Unterminated lexemes stop at end of line (strings/chars) or EOF.
// ---------------------------------------------------------------------------

void pure_advance(std::string_view b, std::size_t& p, int& line, int& col) {
  if (b[p] == '\n') {
    ++line;
    col = 1;
  } else {
    ++col;
  }
  ++p;
}

void pure_skip_line_comment(std::string_view b, std::size_t& p, int& line,
                            int& col) {
  while (p < b.size() && b[p] != '\n') pure_advance(b, p, line, col);
}

void pure_skip_block_comment(std::string_view b, std::size_t& p, int& line,
                             int& col) {
  pure_advance(b, p, line, col);  // '/'
  pure_advance(b, p, line, col);  // '*'
  while (p < b.size()) {
    if (b[p] == '*' && p + 1 < b.size() && b[p + 1] == '/') {
      pure_advance(b, p, line, col);
      pure_advance(b, p, line, col);
      return;
    }
    pure_advance(b, p, line, col);
  }
}

void pure_skip_string(std::string_view b, std::size_t& p, int& line, int& col) {
  pure_advance(b, p, line, col);  // opening quote
  while (p < b.size()) {
    const char c = b[p];
    if (c == '\\') {
      pure_advance(b, p, line, col);
      if (p < b.size()) pure_advance(b, p, line, col);
      continue;
    }
    if (c == '"' || c == '\n') {
      if (c == '"') pure_advance(b, p, line, col);
      return;  // closed, or unterminated: stop at end of line
    }
    pure_advance(b, p, line, col);
  }
}

// Digit separators (1'000'000) look like unterminated char literals to a
// naive scanner; disambiguate on the immediate neighbour characters. A real
// separator always follows a digit of the current numeric literal, so the
// previous character must be a hex digit (this keeps L'w' a char literal).
bool is_digit_separator(std::string_view b, std::size_t p) {
  if (p == 0 || p + 1 >= b.size()) return false;
  const auto prev = static_cast<unsigned char>(b[p - 1]);
  const auto next = static_cast<unsigned char>(b[p + 1]);
  const bool prev_hex = (prev >= '0' && prev <= '9') ||
                        (prev >= 'a' && prev <= 'f') ||
                        (prev >= 'A' && prev <= 'F');
  const bool next_ok =
      is_ident_continue(static_cast<char>(next)) || next == '.';
  return prev_hex && next_ok;
}

void pure_skip_char(std::string_view b, std::size_t& p, int& line, int& col) {
  pure_advance(b, p, line, col);  // opening quote
  while (p < b.size()) {
    const char c = b[p];
    if (c == '\\') {
      pure_advance(b, p, line, col);
      if (p < b.size()) pure_advance(b, p, line, col);
      continue;
    }
    if (c == '\'' || c == '\n') {
      if (c == '\'') pure_advance(b, p, line, col);
      return;  // closed, or unterminated: stop at end of line
    }
    pure_advance(b, p, line, col);
  }
}

// Raw string: p at the opening quote of R"delim( ... )delim".
void pure_skip_raw_string(std::string_view b, std::size_t& p, int& line,
                          int& col) {
  pure_advance(b, p, line, col);  // '"'
  std::string delim;
  while (p < b.size() && b[p] != '(' && delim.size() < 16) {
    delim.push_back(b[p]);
    pure_advance(b, p, line, col);
  }
  if (p < b.size() && b[p] == '(') pure_advance(b, p, line, col);
  const std::string closer = ")" + delim + "\"";
  while (p < b.size()) {
    if (b[p] == ')') {
      std::size_t q = p;
      int l = line, c = col;
      bool match = true;
      for (const char want : closer) {
        if (q >= b.size() || b[q] != want) {
          match = false;
          break;
        }
        pure_advance(b, q, l, c);
      }
      if (match) {
        while (p < q) pure_advance(b, p, line, col);
        return;
      }
    }
    pure_advance(b, p, line, col);
  }
}

// TypeScript template literal with ${...} interpolation and nesting.
void pure_skip_template(std::string_view b, std::size_t& p, int& line, int& col,
                        int nesting) {
  pure_advance(b, p, line, col);  // opening backtick
  while (p < b.size()) {
    const char c = b[p];
    if (c == '\\') {
      pure_advance(b, p, line, col);
      if (p < b.size()) pure_advance(b, p, line, col);
      continue;
    }
    if (c == '`') {
      pure_advance(b, p, line, col);
      return;
    }
    if (c == '$' && p + 1 < b.size() && b[p + 1] == '{' && nesting < 16) {
      pure_advance(b, p, line, col);
      pure_advance(b, p, line, col);
      int braces = 1;
      while (p < b.size() && braces > 0) {
        const char d = b[p];
        if (d == '`') {
          pure_skip_template(b, p, line, col, nesting + 1);
        } else if (d == '{') {
          ++braces;
          pure_advance(b, p, line, col);
        } else if (d == '}') {
          --braces;
          pure_advance(b, p, line, col);
        } else if (d == '"') {
          pure_skip_string(b, p, line, col);
        } else if (d == '\'' && !is_digit_separator(b, p)) {
          pure_skip_char(b, p, line, col);
        } else {
          pure_advance(b, p, line, col);
        }
      }
      continue;
    }
    pure_advance(b, p, line, col);
  }
}

enum class SkipKind { None, Comment, Literal };

// Tries to consume one comment/string/char/template lexeme at p.
SkipKind pure_try_skip(std::string_view b, std::size_t& p, int& line, int& col,
                       Language lang) {
  const char c = b[p];
  if (c == '/' && p + 1 < b.size()) {
    if (b[p + 1] == '/') {
      pure_skip_line_comment(b, p, line, col);
      return SkipKind::Comment;
    }
    if (b[p + 1] == '*') {
      pure_skip_block_comment(b, p, line, col);
      return SkipKind::Comment;
    }
    return SkipKind::None;
  }
  if (c == '"') {
    pure_skip_string(b, p, line, col);
    return SkipKind::Literal;
  }
  if (c == '\'') {
    if (is_digit_separator(b, p)) {
      pure_advance(b, p, line, col);
      return SkipKind::Comment;  // transparent digit separator
    }
    pure_skip_char(b, p, line, col);
    return SkipKind::Literal;
  }
  if (c == '`' && lang == Language::TypeScript) {
    pure_skip_template(b, p, line, col, 0);
    return SkipKind::Literal;
  }
  return SkipKind::None;
}

// Bounded, comment/string-aware scan from just after an opening '{' to its
// matching '}'. Returns the 1-based line of the closing brace, or 0 when
// unbalanced. Mirrors the N22 find_closing_brace traversal in scan.cpp;
// per-definition use keeps the worst case bounded by depth(64) x size(2MiB),
// which the 50 ms time budget bounds.
int find_matching_brace_line(std::string_view b, std::size_t after_open_pos,
                             int open_line, Language lang) {
  std::size_t p = after_open_pos;
  int line = open_line;
  int col = 1;
  int depth = 1;
  while (p < b.size()) {
    const char c = b[p];
    if (c == '/' || c == '"' || c == '\'' || c == '`') {
      if (pure_try_skip(b, p, line, col, lang) != SkipKind::None) continue;
    }
    if (c == '{') {
      ++depth;
      pure_advance(b, p, line, col);
    } else if (c == '}') {
      --depth;
      pure_advance(b, p, line, col);
      if (depth == 0) return line;
    } else {
      pure_advance(b, p, line, col);
    }
  }
  return 0;
}

// Bounded lookahead: returns the next significant character at or after
// `from`, skipping whitespace and comments only (never strings). Returns 0
// when nothing significant is found within the bound.
char peek_significant_char(std::string_view b, std::size_t from, Language lang,
                           std::size_t bound = 4096) {
  std::size_t p = from;
  int line = 1, col = 1;
  const std::size_t limit = from > b.size() ? b.size() : from + bound;
  while (p < b.size() && p < limit) {
    const char c = b[p];
    if (is_space(c) || c == '\n') {
      pure_advance(b, p, line, col);
      continue;
    }
    if (pure_try_skip(b, p, line, col, lang) != SkipKind::None) continue;
    return c;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

constexpr std::size_t kNoDef = static_cast<std::size_t>(-1);
constexpr int kCandidateTokenBudget = 48;

struct Token {
  enum class Kind { None, Ident, Keyword, Punct };
  Kind kind = Kind::None;
  std::string_view text;
};

class Parser {
 public:
  Parser(std::string_view body, Language lang, SymbolTable& out)
      : body_(body), lang_(lang), out_(out) {
    out_.language = lang;
    start_ = std::chrono::steady_clock::now();
  }

  void Run();

 private:
  struct Scope {
    char kind;  // 'n' namespace/module, 'c' class/struct, 'f' function/block,
                // 'x' neutral (enum/interface/union body)
    std::string qual;
  };

  struct ClassPending {
    enum class State { None, AwaitName, Done };
    State state = State::None;
    std::string kind;  // "class" | "struct" (C++), "class" (TS)
    std::string name;
    int line = 0, col = 0;
    bool interface = false;  // TS interface: scope only, no definition
  };

  struct NamespacePending {
    bool active = false;
    std::vector<std::string> segments;
    std::vector<std::pair<int, int>> positions;  // line/col per segment
  };

  struct TsConst {
    enum class Phase {
      None,
      Name,       // saw const/let/var, waiting for the binding identifier
      AwaitEq,    // have the name, waiting for '=' (annotations may intervene)
      Value,      // after '=', classifying the initializer
      ArrowIdent, // initializer began with an identifier (single-param arrow?)
      ArrowParen, // initializer began with '(' (parameter list)
      ArrowTail   // parameter list closed, expecting '=>'
    };
    Phase phase = Phase::None;
    std::string name;
    int line = 0, col = 0;
    int paren_base = 0;
  };

  struct Candidate {
    bool active = false;
    bool need_paren = false;  // waiting for the '(' that follows the name
    bool post_param = false;  // parameter list closed, awaiting body / ';'
    bool cpp_ctor_init = false;
    bool cpp_trailing_return = false;
    bool ts_annotation = false;
    std::string name;
    std::string container;
    int line = 0, col = 0;
    int paren_base = 0;
    int tokens = 0;
  };

  // ---- basic helpers ------------------------------------------------------
  char InnermostScopeKind() const {
    return scopes_.empty() ? '\0' : scopes_.back().kind;
  }

  const std::string& InnermostQual() const {
    static const std::string kEmpty;
    return scopes_.empty() ? kEmpty : scopes_.back().qual;
  }

  std::string JoinQual(std::string_view a, std::string_view b) const {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    return std::string(a) + (lang_ == Language::Cpp ? "::" : ".") +
           std::string(b);
  }

  bool IsPunct(std::string_view text) const {
    return prev_.kind == Token::Kind::Punct && prev_.text == text;
  }

  void SetPrev(Token::Kind kind, std::string_view text) {
    prev_.kind = kind;
    prev_.text = text;
  }

  long long ElapsedMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start_)
        .count();
  }

  void CheckTimeout() {
    if (ElapsedMs() > kTimeBudgetMilliseconds) Degrade("timeout");
  }

  void Degrade(const char* reason) {
    out_.mode = ParseMode::Heuristic;
    if (out_.degraded_reason.empty()) out_.degraded_reason = reason;
    stopped_ = true;
  }

  bool EnsureSymbolBudget() {
    if (symbol_count_ >= kMaximumSymbolsPerFile) {
      Degrade("symbol-limit");
      return false;
    }
    return true;
  }

  std::size_t EmitDef(std::string_view name, std::string_view kind,
                      std::string_view container, int line, int col,
                      int body_end_line) {
    if (!EnsureSymbolBudget()) return kNoDef;
    SymbolDef d;
    d.name = std::string(name);
    d.kind = std::string(kind);
    d.container = std::string(container);
    d.line = line;
    d.col = col;
    d.body_end_line = body_end_line;
    out_.definitions.push_back(std::move(d));
    ++symbol_count_;
    return out_.definitions.size() - 1;
  }

  void RecordRef(std::string_view name, int line, int col) {
    if (!EnsureSymbolBudget()) return;
    SymbolRef r;
    r.name = std::string(name);
    r.line = line;
    r.col = col;
    out_.references.push_back(std::move(r));
    ++symbol_count_;
  }

  void RecordCall(std::string_view name, int line, int col) {
    if (!EnsureSymbolBudget()) return;
    CallSite c;
    c.name = std::string(name);
    c.line = line;
    c.col = col;
    out_.calls.push_back(std::move(c));
    ++symbol_count_;
  }

  // ---- candidate machinery ------------------------------------------------
  void MakeCandidate(std::string_view text, int line, int col) {
    cand_ = Candidate{};
    cand_.active = true;
    cand_.need_paren = true;
    cand_.line = line;
    cand_.col = col;
    if (lang_ == Language::Cpp && IsPunct("~"))
      cand_.name = "~" + std::string(text);
    else
      cand_.name = std::string(text);
    if (lang_ == Language::Cpp && IsPunct("::") &&
        qual_chain_.size() > text.size() + 1)
      cand_.container =
          qual_chain_.substr(0, qual_chain_.size() - text.size() - 2);
  }

  void ResolveCandidateCall() {
    if (!cand_.active) return;
    RecordCall(cand_.name, cand_.line, cand_.col);
    cand_ = Candidate{};
  }

  std::size_t ConfirmCandidateDef(bool declaration_only) {
    const bool method =
        InnermostScopeKind() == 'c' || !cand_.container.empty();
    const std::string container =
        cand_.container.empty() ? InnermostQual() : cand_.container;
    const std::size_t idx =
        EmitDef(cand_.name, method ? "method" : "function", container,
                cand_.line, cand_.col, declaration_only ? line_ : 0);
    cand_ = Candidate{};
    return idx;
  }

  void FeedCandidateToken() {
    if (!cand_.active || cand_.need_paren) return;
    ++cand_.tokens;
    if (cand_.tokens > kCandidateTokenBudget) ResolveCandidateCall();
  }

  // ---- scopes -------------------------------------------------------------
  void PushScope(char kind, const std::string& qual) {
    if (static_cast<int>(scopes_.size()) >= kMaximumNestingDepth) {
      Degrade("depth-limit");
      return;
    }
    scopes_.push_back(Scope{kind, qual});
  }

  // ---- pending cleanup ----------------------------------------------------
  void ResolveTsConstPlain() {
    if (lang_ != Language::TypeScript) return;
    if (ts_const_.phase == TsConst::Phase::None ||
        ts_const_.phase == TsConst::Phase::Name)
      return;
    if (!ts_const_.name.empty())
      RecordRef(ts_const_.name, ts_const_.line, ts_const_.col);
    ts_const_ = TsConst{};
  }

  void ClearLightPendings() {
    if (ns_pending_.active) {
      for (std::size_t i = 0; i < ns_pending_.segments.size(); ++i)
        RecordRef(ns_pending_.segments[i], ns_pending_.positions[i].first,
                  ns_pending_.positions[i].second);
      ns_pending_ = NamespacePending{};
    }
    class_pending_ = ClassPending{};
    enum_pending_ = false;
    extern_pending_ = false;
    operator_pending_ = false;
    ts_function_pending_ = false;
    pending_scope_kind_ = 0;
    pending_def_ = kNoDef;
    ResolveTsConstPlain();
  }

  // ---- token pipeline (defined below) --------------------------------------
  void MainAdvance();
  void SkipPreprocessorLine();
  void AfterSkipLineAccounting(int line_before);
  void SkipLiteralAtCursor();  // cursor already verified skippable
  std::string_view ScanIdent();
  void ScanNumber();
  std::string_view ScanPunct();
  void HandleIdent(std::string_view text, int line, int col);
  void HandleKeyword(std::string_view text, int line, int col);
  void HandlePunct(std::string_view text);
  void HandleCallOrDef(std::string_view text, int line, int col);
  void OnNumberLiteral();
  void OnLiteral();
  void OpenBrace();
  void CloseBrace();
  void OnSemicolon();
  void StartClassKeyword(std::string_view keyword);
  void ClassifyClassName();
  void SkipTemplateHeader();
  void FinalizeNamespaceDef();
  void EmitArrowDef();
  bool TsConstOnPunct(std::string_view text);
  bool TsConstOnKeyword(std::string_view text);
  void UpdateQualChain(std::string_view text);

  // ---- state --------------------------------------------------------------
  std::string_view body_;
  Language lang_;
  SymbolTable& out_;
  std::size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
  bool at_line_start_ = true;
  bool stopped_ = false;

  std::vector<Scope> scopes_;
  Token prev_;

  ClassPending class_pending_;
  NamespacePending ns_pending_;
  TsConst ts_const_;
  Candidate cand_;

  char pending_scope_kind_ = 0;
  std::string pending_scope_qual_;
  std::size_t pending_def_ = kNoDef;

  bool enum_pending_ = false;
  bool extern_pending_ = false;
  bool operator_pending_ = false;
  std::string operator_name_;
  std::string operator_container_;
  int operator_line_ = 0, operator_col_ = 0;
  bool operator_member_ = false;

  bool ts_function_pending_ = false;

  std::string qual_chain_;
  bool chain_continues_ = false;

  int paren_depth_ = 0;
  std::size_t symbol_count_ = 0;
  int lines_since_check_ = 0;
  std::chrono::steady_clock::time_point start_;
};

void Parser::MainAdvance() {
  if (body_[pos_] == '\n') {
    ++line_;
    col_ = 1;
    at_line_start_ = true;
    ++lines_since_check_;
    if (lines_since_check_ >= kTimeSampleLineInterval) {
      lines_since_check_ = 0;
      CheckTimeout();
    }
  } else {
    ++col_;
  }
  ++pos_;
}

void Parser::AfterSkipLineAccounting(int line_before) {
  const int lines = line_ - line_before;
  if (lines <= 0) return;
  lines_since_check_ += lines;
  while (lines_since_check_ >= kTimeSampleLineInterval) {
    lines_since_check_ -= kTimeSampleLineInterval;
    CheckTimeout();
    if (stopped_) return;
  }
}

void Parser::SkipLiteralAtCursor() {
  const int line_before = line_;
  std::size_t p = pos_;
  int l = line_, k = col_;
  pure_try_skip(body_, p, l, k, lang_);  // pre-verified skippable
  pos_ = p;
  line_ = l;
  col_ = k;
  AfterSkipLineAccounting(line_before);
}

void Parser::SkipPreprocessorLine() {
  // '#' is at pos_, at line start. Skip to end of line; a trailing backslash
  // continues the directive onto the next physical line.
  while (pos_ < body_.size() && body_[pos_] != '\n') MainAdvance();
  while (pos_ < body_.size() && body_[pos_] == '\n' && pos_ > 0 &&
         body_[pos_ - 1] == '\\') {
    MainAdvance();  // newline
    while (pos_ < body_.size() && body_[pos_] != '\n') MainAdvance();
  }
}

std::string_view Parser::ScanIdent() {
  const std::size_t begin = pos_;
  while (pos_ < body_.size() && is_ident_continue(body_[pos_])) MainAdvance();
  return std::string_view(body_.data() + begin, pos_ - begin);
}

void Parser::ScanNumber() {
  const auto scan_digits = [&] {
    while (pos_ < body_.size() &&
           (is_ident_continue(body_[pos_]) || body_[pos_] == '.'))
      MainAdvance();
  };
  scan_digits();
  while (pos_ + 1 < body_.size() && body_[pos_] == '\'' &&
         is_ident_continue(body_[pos_ + 1])) {
    MainAdvance();  // digit separator
    scan_digits();
  }
}

std::string_view Parser::ScanPunct() {
  const char c = body_[pos_];
  std::size_t len = 1;
  if (c == ':' && pos_ + 1 < body_.size() && body_[pos_ + 1] == ':') len = 2;
  if (c == '-' && pos_ + 1 < body_.size() && body_[pos_ + 1] == '>') len = 2;
  if (c == '=' && pos_ + 1 < body_.size() && body_[pos_ + 1] == '>') len = 2;
  const std::string_view text(body_.data() + pos_, len);
  for (std::size_t i = 0; i < len; ++i) MainAdvance();
  return text;
}

void Parser::Run() {
  if (body_.size() > kMaximumBodyBytes) {
    Degrade("size-limit");
    return;
  }
  if (body_.size() >= 3 && static_cast<unsigned char>(body_[0]) == 0xEF &&
      static_cast<unsigned char>(body_[1]) == 0xBB &&
      static_cast<unsigned char>(body_[2]) == 0xBF)
    pos_ = 3;  // UTF-8 BOM

  while (pos_ < body_.size() && !stopped_) {
    const char c = body_[pos_];
    if (c == '\n' || is_space(c)) {
      MainAdvance();
      continue;
    }
    const bool was_line_start = at_line_start_;
    at_line_start_ = false;
    if (c == '#' && lang_ == Language::Cpp && was_line_start) {
      SkipPreprocessorLine();
      continue;
    }
    {
      const int line_before = line_;
      std::size_t p = pos_;
      int l = line_, k = col_;
      const SkipKind kind = pure_try_skip(body_, p, l, k, lang_);
      if (kind != SkipKind::None) {
        pos_ = p;
        line_ = l;
        col_ = k;
        AfterSkipLineAccounting(line_before);
        if (stopped_) break;
        if (kind == SkipKind::Literal) OnLiteral();
        continue;
      }
    }
    if (is_ident_start(c)) {
      const int line = line_, col = col_;
      const std::string_view text = ScanIdent();
      HandleIdent(text, line, col);
      continue;
    }
    if (is_digit(c)) {
      ScanNumber();
      OnNumberLiteral();
      continue;
    }
    const std::string_view text = ScanPunct();
    HandlePunct(text);
  }

  if (!stopped_) {
    // EOF: resolve everything still pending deterministically.
    if (cand_.active) ResolveCandidateCall();
    ClearLightPendings();
  }
}

// ---------------------------------------------------------------------------
// Token handlers
// ---------------------------------------------------------------------------

void Parser::UpdateQualChain(std::string_view text) {
  if (IsPunct("::") && !qual_chain_.empty() && chain_continues_) {
    qual_chain_ += "::";
    qual_chain_.append(text);
  } else {
    qual_chain_.assign(text);
  }
  chain_continues_ = true;
}

void Parser::HandleIdent(std::string_view text, int line, int col) {
  // 1. String / encoding prefixes (C++) and tagged templates (TS).
  if (pos_ < body_.size()) {
    const char next = body_[pos_];
    if (lang_ == Language::Cpp && next == '"') {
      if (text == "R" || text == "LR" || text == "uR" || text == "UR" ||
          text == "u8R") {
        SetPrev(Token::Kind::Ident, text);
        const int line_before = line_;
        std::size_t p = pos_;
        int l = line_, k = col_;
        pure_skip_raw_string(body_, p, l, k);
        pos_ = p;
        line_ = l;
        col_ = k;
        AfterSkipLineAccounting(line_before);
        return;
      }
      if (text == "u" || text == "U" || text == "L" || text == "u8") {
        SetPrev(Token::Kind::Ident, text);
        SkipLiteralAtCursor();
        return;
      }
    }
    if (lang_ == Language::Cpp && next == '\'' && text == "L") {
      // Wide character literal prefix: L'w'
      SetPrev(Token::Kind::Ident, text);
      SkipLiteralAtCursor();
      return;
    }
    if (lang_ == Language::TypeScript && next == '`') {
      RecordCall(text, line, col);  // tagged template literal
      SetPrev(Token::Kind::Ident, text);
      SkipLiteralAtCursor();
      return;
    }
  }

  // 2. C++ operator-name assembly for conversion operators (operator Foo()).
  if (lang_ == Language::Cpp && operator_pending_ &&
      operator_name_ == "operator" && operator_name_.size() < 12) {
    operator_name_ += " ";
    operator_name_.append(text);
    SetPrev(Token::Kind::Ident, text);
    return;
  }

  // 3. Keywords.
  if (is_keyword(lang_, text)) {
    HandleKeyword(text, line, col);
    return;
  }

  // 4. C++ namespace chain collection.
  if (ns_pending_.active) {
    ns_pending_.segments.emplace_back(text);
    ns_pending_.positions.emplace_back(line, col);
    SetPrev(Token::Kind::Ident, text);
    return;
  }

  // 5. Class/interface name capture (C++ class/struct, TS class/interface).
  if (class_pending_.state == ClassPending::State::AwaitName) {
    class_pending_.name.assign(text);
    class_pending_.line = line;
    class_pending_.col = col;
    class_pending_.state = ClassPending::State::Done;
    ClassifyClassName();
    SetPrev(Token::Kind::Ident, text);
    return;
  }

  // 6. TypeScript const/let/var binding name and single-param arrow value.
  if (lang_ == Language::TypeScript) {
    if (ts_const_.phase == TsConst::Phase::Name) {
      ts_const_.name.assign(text);
      ts_const_.line = line;
      ts_const_.col = col;
      ts_const_.phase = TsConst::Phase::AwaitEq;
      SetPrev(Token::Kind::Ident, text);
      return;
    }
    if (ts_const_.phase == TsConst::Phase::Value) {
      ts_const_.phase = TsConst::Phase::ArrowIdent;
      // Fall through: the identifier itself is also recorded as a reference.
    }
  }

  // 7. Candidate bookkeeping.
  if (cand_.active && !cand_.need_paren) {
    if (!cand_.post_param || cand_.ts_annotation || cand_.cpp_ctor_init ||
        cand_.cpp_trailing_return) {
      FeedCandidateToken();  // params / annotations / initializers / return types pass
    } else {
      ResolveCandidateCall();
    }
  }

  // 8. Qualified container chain (C++ out-of-line definitions).
  if (lang_ == Language::Cpp) UpdateQualChain(text);

  // 9. Identifier followed by '(' -> call or definition candidate.
  const char nsig = peek_significant_char(body_, pos_, lang_);
  if (nsig == '(') {
    HandleCallOrDef(text, line, col);
    SetPrev(Token::Kind::Ident, text);
    return;
  }
  if (lang_ == Language::Cpp && nsig == '<') {
    // Bounded template-call check: ident<...>( ... ) is a call, not a ref.
    std::size_t p = pos_;
    int l = line_, k = col_;
    int depth = 0;
    bool balanced = false;
    const std::size_t limit = pos_ + 256;
    while (p < body_.size() && p < limit) {
      const char d = body_[p];
      if (d == '/' || d == '"' || d == '\'' || d == '`') {
        if (pure_try_skip(body_, p, l, k, lang_) != SkipKind::None) continue;
      }
      if (d == '<') {
        ++depth;
        pure_advance(body_, p, l, k);
      } else if (d == '>') {
        --depth;
        pure_advance(body_, p, l, k);
        if (depth == 0) {
          balanced = true;
          break;
        }
      } else {
        pure_advance(body_, p, l, k);
      }
    }
    if (balanced && peek_significant_char(body_, p, lang_) == '(') {
      RecordCall(text, line, col);
      chain_continues_ = false;
      SetPrev(Token::Kind::Ident, text);
      return;
    }
  }
  RecordRef(text, line, col);
  SetPrev(Token::Kind::Ident, text);
}

bool Parser::TsConstOnKeyword(std::string_view text) {
  switch (ts_const_.phase) {
    case TsConst::Phase::None:
      return false;
    case TsConst::Phase::Name:
      // e.g. "const enum ..." — the keyword cannot be a binding name.
      ts_const_ = TsConst{};
      return false;
    case TsConst::Phase::AwaitEq:
      return false;  // type annotation keywords pass; keep waiting for '='
    case TsConst::Phase::Value:
      if (text == "async") return true;  // async arrow
      if (text == "function") {
        // const f = function (...) { ... }
        const std::size_t idx =
            EmitDef(ts_const_.name, "function", InnermostQual(), ts_const_.line,
                    ts_const_.col, line_);
        if (idx != kNoDef) {
          pending_scope_kind_ = 'f';
          pending_scope_qual_ = JoinQual(InnermostQual(), ts_const_.name);
          pending_def_ = idx;
        }
        ts_const_ = TsConst{};
        return true;  // consumed: do not arm ts_function_pending_
      }
      ResolveTsConstPlain();
      return false;
    case TsConst::Phase::ArrowIdent:
    case TsConst::Phase::ArrowTail:
      ResolveTsConstPlain();
      return false;
    case TsConst::Phase::ArrowParen:
      // Type annotations inside the parameter list use keywords; keep waiting.
      return false;
  }
  return false;
}

void Parser::HandleKeyword(std::string_view text, int line, int col) {
  // TypeScript const-value classification runs first.
  bool consumed_by_ts_const = false;
  if (lang_ == Language::TypeScript) consumed_by_ts_const = TsConstOnKeyword(text);

  // C++ operator-name assembly: following keywords extend the name
  // (conversion operators such as "operator bool").
  if (operator_pending_) {
    if (lang_ == Language::Cpp && operator_name_ == "operator" &&
        operator_name_.size() < 12) {
      operator_name_ += " ";
      operator_name_ += std::string(text);
    }
    SetPrev(Token::Kind::Keyword, text);
    return;
  }

  if (lang_ == Language::Cpp) {
    if (text == "namespace") {
      ns_pending_ = NamespacePending{};
      ns_pending_.active = true;
    } else if (text == "class" || text == "struct") {
      if (prev_.kind == Token::Kind::Keyword && prev_.text == "enum") {
        enum_pending_ = true;  // enum class / enum struct: neutral scope
      } else {
        StartClassKeyword(text);
      }
    } else if (text == "enum" || text == "union") {
      enum_pending_ = true;  // neutral scope, no definition (plan patterns)
    } else if (text == "template") {
      SkipTemplateHeader();
      SetPrev(Token::Kind::Keyword, text);
      return;
    } else if (text == "operator") {
      operator_pending_ = true;
      operator_name_ = "operator";
      operator_line_ = line;
      operator_col_ = col;
      operator_member_ = IsPunct(".") || IsPunct("->");
      // Out-of-line definitions: int Foo::operator+(...) — capture the
      // qualifier chain that precedes the keyword.
      operator_container_.clear();
      if (IsPunct("::") && !qual_chain_.empty()) operator_container_ = qual_chain_;
    } else if (text == "extern") {
      extern_pending_ = true;
    }
  } else {
    if (!consumed_by_ts_const && text == "function") {
      ts_function_pending_ = true;
    } else if (text == "const" || text == "let" || text == "var") {
      if (ts_const_.phase == TsConst::Phase::None) {
        ts_const_ = TsConst{};
        ts_const_.phase = TsConst::Phase::Name;
      }
    } else if (text == "class") {
      StartClassKeyword(text);
    } else if (text == "interface") {
      class_pending_ = ClassPending{};
      class_pending_.state = ClassPending::State::AwaitName;
      class_pending_.kind = "interface";
      class_pending_.interface = true;
    } else if (text == "enum") {
      enum_pending_ = true;
    } else if (text == "constructor") {
      if (peek_significant_char(body_, pos_, lang_) == '(') {
        const std::size_t idx = EmitDef("constructor", "method", InnermostQual(),
                                        line, col, line_);
        if (idx != kNoDef) {
          pending_scope_kind_ = 'f';
          pending_scope_qual_ = JoinQual(InnermostQual(), "constructor");
          pending_def_ = idx;
        }
      }
    }
  }

  // Candidate bookkeeping.
  if (cand_.active && !cand_.need_paren) {
    if (cand_.post_param &&
        (lang_ == Language::TypeScript || cand_.ts_annotation ||
         cand_.cpp_ctor_init || cand_.cpp_trailing_return ||
         is_cpp_stay_keyword(text))) {
      // TS: annotation tokens stay armed; C++: qualifier keywords and
      // trailing-return types stay armed.
      FeedCandidateToken();
    } else if (cand_.post_param) {
      ResolveCandidateCall();
    } else {
      FeedCandidateToken();
    }
  }

  chain_continues_ = false;
  SetPrev(Token::Kind::Keyword, text);
}

bool Parser::TsConstOnPunct(std::string_view text) {
  if (ts_const_.phase == TsConst::Phase::None ||
      ts_const_.phase == TsConst::Phase::Name)
    return false;
  // '=>' is handled by the arrow emission in HandlePunct.
  if (text == "=>" &&
      (ts_const_.phase == TsConst::Phase::Value ||
       ts_const_.phase == TsConst::Phase::ArrowIdent ||
       ts_const_.phase == TsConst::Phase::ArrowTail))
    return false;
  switch (ts_const_.phase) {
    case TsConst::Phase::AwaitEq:
      if (text == "=") {
        ts_const_.phase = TsConst::Phase::Value;
        return true;
      }
      if (text == ":") return true;  // type annotation, keep waiting
      ResolveTsConstPlain();
      return false;
    case TsConst::Phase::Value:
      if (text == "(") {
        ts_const_.phase = TsConst::Phase::ArrowParen;
        ts_const_.paren_base = paren_depth_ + 1;  // HandlePunct increments next
        return true;
      }
      ResolveTsConstPlain();
      return false;
    case TsConst::Phase::ArrowIdent:
    case TsConst::Phase::ArrowTail:
      ResolveTsConstPlain();
      return false;
    case TsConst::Phase::ArrowParen:
      // Parameter lists contain ':', ',', '?', '=' (defaults); only a statement
      // terminator can resolve the tracker while the list is open.
      if (text == ";") ResolveTsConstPlain();
      return false;
    case TsConst::Phase::None:
    case TsConst::Phase::Name:
      return false;
  }
  return false;
}

void Parser::OnNumberLiteral() {
  if (lang_ == Language::TypeScript) ResolveTsConstPlain();
  SetPrev(Token::Kind::Punct, "#num");
}

void Parser::OnLiteral() {
  if (lang_ == Language::TypeScript) ResolveTsConstPlain();
}

void Parser::EmitArrowDef() {
  const std::size_t idx = EmitDef(ts_const_.name, "arrow", InnermostQual(),
                                  ts_const_.line, ts_const_.col, line_);
  if (idx != kNoDef) {
    pending_scope_kind_ = 'f';
    pending_scope_qual_ = JoinQual(InnermostQual(), ts_const_.name);
    pending_def_ = idx;
  }
  ts_const_ = TsConst{};
}

void Parser::HandleCallOrDef(std::string_view text, int line, int col) {
  // A new ident+( replaces an awaiting candidate only once the candidate's
  // own parameter list has closed without a body; while the list is open
  // (default-argument calls) or in ctor member-initializers / annotations /
  // trailing returns, nested identifiers are legitimate calls and must not
  // resolve the pending candidate.
  if (cand_.active && cand_.post_param && !cand_.cpp_ctor_init &&
      !cand_.ts_annotation && !cand_.cpp_trailing_return)
    ResolveCandidateCall();

  // C++ constructor member-initializer names: Foo() : x_(1), y_(2) { }
  if (lang_ == Language::Cpp && cand_.active && cand_.cpp_ctor_init &&
      (IsPunct(":") || IsPunct(","))) {
    RecordRef(text, line, col);  // member name, not a call
    chain_continues_ = false;
    return;
  }

  const char scope_kind = InnermostScopeKind();

  // Neutral scopes (enum / interface / union bodies): identifiers are plain
  // references; no call or definition extraction.
  if (scope_kind == 'x') {
    RecordRef(text, line, col);
    chain_continues_ = false;
    return;
  }

  if (lang_ == Language::Cpp) {
    if (scope_kind == 'f') {
      RecordCall(text, line, col);
      chain_continues_ = false;
      return;
    }
    bool whitelisted = false;
    if (prev_.kind == Token::Kind::Ident || prev_.kind == Token::Kind::None)
      whitelisted = true;
    else if (prev_.kind == Token::Kind::Keyword)
      whitelisted = !is_cpp_expression_keyword(prev_.text);
    else if (prev_.kind == Token::Kind::Punct)
      whitelisted =
          prev_.text == "::" || prev_.text == ";" || prev_.text == "{" ||
          prev_.text == "}" || prev_.text == ":" || prev_.text == ">" ||
          prev_.text == "]" || prev_.text == "~" || prev_.text == "*" ||
          prev_.text == "&" || prev_.text == ")";
    if (whitelisted) {
      MakeCandidate(text, line, col);
    } else {
      RecordCall(text, line, col);
      chain_continues_ = false;
    }
    return;
  }

  // TypeScript.
  if (ts_function_pending_) {
    ts_function_pending_ = false;
    const std::size_t idx =
        EmitDef(text, "function", InnermostQual(), line, col, line_);
    if (idx != kNoDef) {
      pending_scope_kind_ = 'f';
      pending_scope_qual_ = JoinQual(InnermostQual(), text);
      pending_def_ = idx;
    }
    return;
  }
  if (scope_kind == 'c') {
    bool whitelisted = false;
    if (prev_.kind == Token::Kind::Ident || prev_.kind == Token::Kind::None)
      whitelisted = true;
    else if (prev_.kind == Token::Kind::Keyword)
      whitelisted = !is_ts_expression_keyword(prev_.text);
    else if (prev_.kind == Token::Kind::Punct)
      whitelisted = prev_.text == ";" || prev_.text == "{" ||
                    prev_.text == "}" || prev_.text == "*" ||
                    prev_.text == ")" || prev_.text == "#";
    if (whitelisted) {
      MakeCandidate(text, line, col);
    } else {
      RecordCall(text, line, col);
      chain_continues_ = false;
    }
    return;
  }
  RecordCall(text, line, col);
  chain_continues_ = false;
}

void Parser::HandlePunct(std::string_view text) {
  // Whether the parameter list was already closed when this token arrived (the
  // ')' that closes the list sets post_param and must not resolve it).
  const bool was_post_param = cand_.active && cand_.post_param;
  // TypeScript const-value classification sees punctuation first.
  if (lang_ == Language::TypeScript) TsConstOnPunct(text);

  if (text == "(") {
    ++paren_depth_;
    if (cand_.active && cand_.need_paren) {
      cand_.paren_base = paren_depth_;
      cand_.need_paren = false;
    }
    if (lang_ == Language::Cpp && operator_pending_) {
      if (operator_name_ == "operator" &&
          peek_significant_char(body_, pos_, lang_, 64) == ')') {
        // operator() spelling: consume "()" as part of the name.
        operator_name_ += "()";
        if (pos_ < body_.size() && body_[pos_] == ')') {
          --paren_depth_;  // the ')' matched this '('
          MainAdvance();
        }
        SetPrev(Token::Kind::Punct, "()");
        return;  // still operator_pending_: the parameter '(' comes next
      }
      operator_pending_ = false;
      if (operator_member_) {
        RecordCall(operator_name_, operator_line_, operator_col_);
      } else {
        MakeCandidate(operator_name_, operator_line_, operator_col_);
        if (cand_.active) {
          if (!operator_container_.empty()) cand_.container = operator_container_;
          cand_.paren_base = paren_depth_;
          cand_.need_paren = false;
        }
      }
    }
  } else if (text == ")") {
    if (paren_depth_ > 0) --paren_depth_;
    if (cand_.active && !cand_.need_paren && !cand_.post_param &&
        paren_depth_ + 1 == cand_.paren_base)
      cand_.post_param = true;
    if (lang_ == Language::TypeScript &&
        ts_const_.phase == TsConst::Phase::ArrowParen &&
        paren_depth_ + 1 == ts_const_.paren_base)
      ts_const_.phase = TsConst::Phase::ArrowTail;
  } else if (text == "{") {
    OpenBrace();
    return;
  } else if (text == "}") {
    CloseBrace();
    return;
  } else if (text == ";") {
    OnSemicolon();
    return;
  } else if (text == ":") {
    if (cand_.active && cand_.post_param) {
      if (lang_ == Language::Cpp)
        cand_.cpp_ctor_init = true;
      else
        cand_.ts_annotation = true;
      FeedCandidateToken();
      SetPrev(Token::Kind::Punct, text);
      return;
    }
  } else if (text == "=") {
    if (cand_.active && cand_.post_param && lang_ == Language::Cpp) {
      ConfirmCandidateDef(true);  // pure-virtual / =default / =delete
      SetPrev(Token::Kind::Punct, text);
      return;
    }
  } else if (text == "=>") {
    if (lang_ == Language::TypeScript &&
        (ts_const_.phase == TsConst::Phase::Value ||
         ts_const_.phase == TsConst::Phase::ArrowIdent ||
         ts_const_.phase == TsConst::Phase::ArrowTail)) {
      EmitArrowDef();
      SetPrev(Token::Kind::Punct, text);
      return;
    }
  } else if (text == "->") {
    if (cand_.active && cand_.post_param && lang_ == Language::Cpp) {
      cand_.cpp_trailing_return = true;  // trailing return type follows
      FeedCandidateToken();
      SetPrev(Token::Kind::Punct, text);
      return;
    }
  }

  // Generic candidate feeding / resolution.
  if (cand_.active && !cand_.need_paren) {
    if (was_post_param && !cand_.ts_annotation && !cand_.cpp_ctor_init &&
        !cand_.cpp_trailing_return) {
      ResolveCandidateCall();
    } else {
      FeedCandidateToken();
    }
  }

  // C++ operator-name punctuation assembly.
  if (lang_ == Language::Cpp && operator_pending_) {
    if (text == ";" || text == "{" || text == "}")
      operator_pending_ = false;
    else if (operator_name_.size() < 12)
      operator_name_ += std::string(text);
  }

  chain_continues_ = (text == "::");
  SetPrev(Token::Kind::Punct, text);
}

void Parser::StartClassKeyword(std::string_view keyword) {
  class_pending_ = ClassPending{};
  class_pending_.state = ClassPending::State::AwaitName;
  class_pending_.kind.assign(keyword);
  // Anonymous aggregate: "class {" opens a scope without a definition.
  if (peek_significant_char(body_, pos_, lang_) == '{') {
    class_pending_.state = ClassPending::State::Done;
    pending_scope_kind_ = 'c';
    pending_scope_qual_ = InnermostQual();
  }
}

void Parser::ClassifyClassName() {
  // Bounded lookahead after the class name: '{' or ':' starts the definition
  // (':' introduces base clauses); ';' directly is a forward declaration (no
  // definition, no reference); ';' after an identifier is an elaborated usage
  // (name recorded as a reference); '('/'=' etc. is also an elaborated usage.
  const std::string& name = class_pending_.name;
  std::size_t p = pos_;
  int l = line_, k = col_;
  const std::size_t limit = pos_ + 512;
  bool decided = false;
  bool saw_ident = false;
  while (p < body_.size() && p < limit) {
    const char c = body_[p];
    if (is_space(c) || c == '\n') {
      pure_advance(body_, p, l, k);
      continue;
    }
    if (c == '/' || c == '"' || c == '\'' || c == '`') {
      if (pure_try_skip(body_, p, l, k, lang_) != SkipKind::None) continue;
    }
    if (c == '<') {
      // Template argument list attached to the class name.
      int depth = 0;
      while (p < body_.size() && p < limit) {
        const char d = body_[p];
        if (d == '/' || d == '"' || d == '\'' || d == '`') {
          if (pure_try_skip(body_, p, l, k, lang_) != SkipKind::None) continue;
        }
        if (d == '<') ++depth;
        else if (d == '>') {
          --depth;
          if (depth == 0) break;
        }
        pure_advance(body_, p, l, k);
      }
      if (p < body_.size()) pure_advance(body_, p, l, k);
      continue;
    }
    if (c == '{' || c == ':') {
      decided = true;
      if (!class_pending_.interface) {
        const std::size_t idx =
            EmitDef(name, class_pending_.kind, InnermostQual(),
                    class_pending_.line, class_pending_.col, 0);
        if (idx != kNoDef) pending_def_ = idx;
      }
      pending_scope_kind_ = class_pending_.interface ? 'x' : 'c';
      pending_scope_qual_ =
          class_pending_.interface ? InnermostQual() : JoinQual(InnermostQual(), name);
      break;
    }
    if (c == ';') {
      decided = true;
      if (saw_ident || class_pending_.interface)
        RecordRef(name, class_pending_.line, class_pending_.col);
      break;  // forward declaration: no definition
    }
    if (is_ident_start(c) || is_digit(c)) {
      saw_ident = true;  // final / extends / variable declarators / ...
      while (p < body_.size() && is_ident_continue(body_[p]))
        pure_advance(body_, p, l, k);
      continue;
    }
    if (c == '(' || c == '=' || c == ',' || c == '[') {
      decided = true;
      RecordRef(name, class_pending_.line, class_pending_.col);
      break;
    }
    pure_advance(body_, p, l, k);
  }
  if (!decided) RecordRef(name, class_pending_.line, class_pending_.col);
}

void Parser::SkipTemplateHeader() {
  // Skip balanced <...> after the 'template' keyword (strings/comments aware).
  std::size_t p = pos_;
  if (peek_significant_char(body_, p, lang_, 64) != '<') return;
  int l = line_, k = col_;
  while (p < body_.size() && (is_space(body_[p]) || body_[p] == '\n'))
    pure_advance(body_, p, l, k);
  const std::size_t limit = p + 16384;
  int depth = 0;
  while (p < body_.size() && p < limit) {
    const char c = body_[p];
    if (c == '/' || c == '"' || c == '\'' || c == '`') {
      if (pure_try_skip(body_, p, l, k, lang_) != SkipKind::None) continue;
    }
    if (c == '<') {
      ++depth;
      pure_advance(body_, p, l, k);
    } else if (c == '>') {
      --depth;
      pure_advance(body_, p, l, k);
      if (depth == 0) break;
    } else if (c == ';') {
      break;  // malformed: give up, resume normal scanning
    } else {
      pure_advance(body_, p, l, k);
    }
  }
  const int line_before = line_;
  pos_ = p;
  line_ = l;
  col_ = k;
  AfterSkipLineAccounting(line_before);
}

void Parser::FinalizeNamespaceDef() {
  if (ns_pending_.segments.empty()) {
    pending_scope_kind_ = 'n';
    pending_scope_qual_ = InnermostQual();
    ns_pending_ = NamespacePending{};
    return;
  }
  const std::string& last = ns_pending_.segments.back();
  std::string prior;
  for (std::size_t i = 0; i + 1 < ns_pending_.segments.size(); ++i) {
    if (!prior.empty()) prior += "::";
    prior += ns_pending_.segments[i];
  }
  const std::string container = JoinQual(InnermostQual(), prior);
  const std::size_t idx =
      EmitDef(last, "namespace", container, ns_pending_.positions.back().first,
              ns_pending_.positions.back().second, 0);
  if (idx != kNoDef) pending_def_ = idx;
  pending_scope_kind_ = 'n';
  pending_scope_qual_ = JoinQual(container, last);
  ns_pending_ = NamespacePending{};
}

void Parser::OpenBrace() {
  // Namespace definitions resolve at the brace.
  if (ns_pending_.active) FinalizeNamespaceDef();

  char kind = 0;
  std::string qual;
  std::size_t def_index = kNoDef;
  if (pending_scope_kind_ != 0) {
    kind = pending_scope_kind_;
    qual = pending_scope_qual_;
    def_index = pending_def_;
    pending_scope_kind_ = 0;
    pending_scope_qual_.clear();
    pending_def_ = kNoDef;
    enum_pending_ = false;
  } else if (cand_.active && cand_.post_param &&
             !(cand_.cpp_ctor_init && prev_.kind == Token::Kind::Ident)) {
    // Function/method body brace (except a member brace-initializer such as
    // "Foo() : x_{1} {", where the identifier directly precedes '{').
    const std::size_t idx = ConfirmCandidateDef(false);
    if (idx != kNoDef) {
      kind = 'f';
      qual = JoinQual(out_.definitions[idx].container, out_.definitions[idx].name);
      def_index = idx;
    }
    enum_pending_ = false;
  } else if (enum_pending_) {
    kind = 'x';
    qual = InnermostQual();
    enum_pending_ = false;
  } else if (extern_pending_) {
    kind = 'n';
    qual = InnermostQual();
    extern_pending_ = false;
  } else {
    // Default block: lambdas/blocks after value contexts behave like function
    // bodies; braces at declaration scope stay declaration-like.
    if (!scopes_.empty() && scopes_.back().kind == 'f')
      kind = 'f';
    else if (IsPunct(")"))
      kind = 'f';
    else
      kind = 'n';
    qual = InnermostQual();
  }
  chain_continues_ = false;

  PushScope(kind, qual);
  if (stopped_) return;

  if (def_index != kNoDef) {
    const int end = find_matching_brace_line(body_, pos_, line_, lang_);
    if (end != 0) out_.definitions[def_index].body_end_line = end;
  }
  SetPrev(Token::Kind::Punct, "{");
}

void Parser::CloseBrace() {
  if (!scopes_.empty()) scopes_.pop_back();
  if (cand_.active) ResolveCandidateCall();
  ClearLightPendings();
  chain_continues_ = false;
  SetPrev(Token::Kind::Punct, "}");
}

void Parser::OnSemicolon() {
  if (cand_.active) {
    if (cand_.post_param)
      ConfirmCandidateDef(true);
    else
      ResolveCandidateCall();
  }
  ClearLightPendings();
  chain_continues_ = false;
  SetPrev(Token::Kind::Punct, ";");
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SymbolTable parse_content(std::string_view body, Language language) {
  SymbolTable table;
  Parser parser(body, language, table);
  parser.Run();
  return table;
}

// ---------------------------------------------------------------------------
// Deterministic JSON serialization
// ---------------------------------------------------------------------------

namespace {

void json_append_escaped(std::string& out, std::string_view value) {
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          const char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
          out += hex[static_cast<unsigned char>(c) & 0xF];
        } else {
          out += c;
        }
    }
  }
}

void json_append_string(std::string& out, std::string_view value) {
  out += '"';
  json_append_escaped(out, value);
  out += '"';
}

const char* parse_mode_string(ParseMode mode) {
  return mode == ParseMode::Structured ? "structured" : "heuristic";
}

const char* language_string(Language language) {
  return language == Language::Cpp ? "cpp" : "typescript";
}

}  // namespace

std::string to_json(const SymbolTable& table) {
  std::string out;
  out.reserve(256 + table.definitions.size() * 96 +
              table.references.size() * 48 + table.calls.size() * 48);
  out += "{\n";
  out += "  \"language\": ";
  json_append_string(out, language_string(table.language));
  out += ",\n  \"mode\": ";
  json_append_string(out, parse_mode_string(table.mode));
  out += ",\n  \"degraded_reason\": ";
  json_append_string(out, table.degraded_reason);
  out += ",\n  \"symbols\": {\n    \"definitions\": [";
  if (!table.definitions.empty()) {
    out += "\n";
    for (std::size_t i = 0; i < table.definitions.size(); ++i) {
      const SymbolDef& d = table.definitions[i];
      out += "      {\"name\": ";
      json_append_string(out, d.name);
      out += ", \"kind\": ";
      json_append_string(out, d.kind);
      out += ", \"container\": ";
      json_append_string(out, d.container);
      out += ", \"line\": " + std::to_string(d.line);
      out += ", \"col\": " + std::to_string(d.col);
      out += ", \"body_end_line\": " + std::to_string(d.body_end_line);
      out += "}";
      if (i + 1 < table.definitions.size()) out += ",";
      out += "\n";
    }
    out += "    ";
  }
  out += "],\n    \"references\": [";
  if (!table.references.empty()) {
    out += "\n";
    for (std::size_t i = 0; i < table.references.size(); ++i) {
      const SymbolRef& r = table.references[i];
      out += "      {\"name\": ";
      json_append_string(out, r.name);
      out += ", \"line\": " + std::to_string(r.line);
      out += ", \"col\": " + std::to_string(r.col);
      out += "}";
      if (i + 1 < table.references.size()) out += ",";
      out += "\n";
    }
    out += "    ";
  }
  out += "],\n    \"calls\": [";
  if (!table.calls.empty()) {
    out += "\n";
    for (std::size_t i = 0; i < table.calls.size(); ++i) {
      const CallSite& c = table.calls[i];
      out += "      {\"name\": ";
      json_append_string(out, c.name);
      out += ", \"line\": " + std::to_string(c.line);
      out += ", \"col\": " + std::to_string(c.col);
      out += "}";
      if (i + 1 < table.calls.size()) out += ",";
      out += "\n";
    }
    out += "    ";
  }
  out += "]\n  }\n}\n";
  return out;
}

}  // namespace qbrain::codeintel::astlite

#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace qbrain::codeintel::astlite {

// N32 D1: bounded structured parser core (C++ / TypeScript).
// Content-in API only: no filesystem access, no path parameters, no regex.

enum class Language { Cpp, TypeScript };

// Structured = the bounded parse completed within every hard limit.
// Heuristic  = a limit was exceeded; the caller should fall back to the legacy
//              regex path and report mode "heuristic" (see degraded_reason).
enum class ParseMode { Structured, Heuristic };

struct SymbolDef {
  std::string name;  // unqualified name ("~Foo", "operator==" keep their spelling)
  std::string kind;  // namespace|class|struct|function|method|arrow
  std::string container;  // qualified enclosing scope; "" at file scope
  int line = 0;           // 1-based line of the name
  int col = 0;            // 1-based byte column of the name
  int body_end_line = 0;  // line of the closing brace; for declarations, the
                          // declaration line; 0 = unknown (unbalanced input)
};

struct SymbolRef {
  std::string name;
  int line = 0;
  int col = 0;
};

struct CallSite {
  std::string name;  // callee identifier immediately preceding '('
  int line = 0;
  int col = 0;
};

struct SymbolTable {
  Language language = Language::Cpp;
  ParseMode mode = ParseMode::Structured;
  // "" | "size-limit" | "depth-limit" | "symbol-limit" | "timeout"
  std::string degraded_reason;
  std::vector<SymbolDef> definitions;
  std::vector<SymbolRef> references;
  std::vector<CallSite> calls;
};

// Hard bounds (plan N32 D1).
constexpr std::size_t kMaximumBodyBytes = 2u * 1024u * 1024u;  // 2 MiB
constexpr int kMaximumNestingDepth = 64;
constexpr std::size_t kMaximumSymbolsPerFile = 10000;
constexpr long long kTimeBudgetMilliseconds = 50;
constexpr int kTimeSampleLineInterval = 1000;

// Parses source text into a per-file symbol table (definitions, references,
// call sites). Bounded: bodies over kMaximumBodyBytes degrade immediately;
// nesting over kMaximumNestingDepth, recorded symbols over
// kMaximumSymbolsPerFile, or wall time over kTimeBudgetMilliseconds (sampled
// every kTimeSampleLineInterval lines via std::chrono::steady_clock) stop the
// parse and return the partial table with mode Heuristic and a
// degraded_reason. Always terminates; never throws; no allocation beyond the
// returned table.
SymbolTable parse_content(std::string_view body, Language language);

// Deterministic JSON serialization of a table (fixtures / tests / ops).
// Byte-identical across runs for identical tables.
std::string to_json(const SymbolTable& table);

}  // namespace qbrain::codeintel::astlite

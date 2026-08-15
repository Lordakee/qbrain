// tests/test_n32.cpp — N32 scan integration tests (subagent B: D2/D2b/D4).
//
// Covers the structured (astlite) scan path integrated behind the six code ops
// (code_def/code_refs/code_callers/code_callees/code_flow/code_blast):
//   - mode-field correctness (structured for clean C++/TS code-file pages;
//     heuristic for extension-less pages and degraded parses, with the
//     degraded_reason surfaced in the output)
//   - golden def/caller/callee/flow rows for embedded C++ and TS pages
//   - degradation: >64 nesting .cpp page degrades to heuristic with a
//     non-empty degraded_reason (N22 >16 KiB page budget preserved as
//     resource_limit error, not a crash)
//   - malformed/binary/invalid-UTF-8 page bodies: bounded, process alive
//   - determinism: two runs produce byte-identical json and text (including
//     the mode trailer)
//   - path isolation at the integration layer: unregistered/invalid/unallowed
//     source_id is rejected before any page content is read (zero page-table
//     reads observed via the SQLite authorizer)
//   - golden comparison against the N32-A fixture directory
//     (tests/fixtures/astlite): every fixture parses twice byte-identically and
//     matches its <stem>.json sidecar exactly (astlite::to_json golden lock);
//     each fixture is also ingested as a page and run through the six ops to
//     verify mode/degraded_reason propagation end-to-end (structured fixtures
//     report "structured", depth/timeout fixtures report "heuristic" with the
//     golden reason; oversize fixtures hit the preserved N22 page-size budget
//     as a bounded resource_limit error).
//
// Embedded structured goldens below pin the integration contract; if the
// astlite parser's extraction granularity differs, reconcile here at merge.

#include "qbrain/codeintel/astlite.hpp"
#include "qbrain/codeintel/scan.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

#define QB_CHECK(cond)                                                          \
  do {                                                                          \
    if (!(cond))                                                                \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +  \
                               __FILE__ + ":" + std::to_string(__LINE__));      \
  } while (0)

// --- n32: structured/heuristic scan integration ---

namespace {

struct Row {
  std::string slug;
  int line = 0;
  std::string kind;
  std::string snippet;
};

std::string trailer_field(const std::string& text, const std::string& key) {
  const std::string prefix = key + ": ";
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
  }
  return {};
}

std::string mode_of(const qbrain::ops::OpResult& result) {
  const auto mode = trailer_field(result.text, "mode");
  if (mode.empty())
    throw std::runtime_error("mode trailer missing in op text output");
  return mode;
}

void require_mode(const qbrain::ops::OpResult& result, const std::string& expected) {
  QB_CHECK(result.ok);
  QB_CHECK(mode_of(result) == expected);
}

void require_no_degradation(const qbrain::ops::OpResult& result) {
  QB_CHECK(trailer_field(result.text, "degraded_reason").empty());
}

std::string degraded_reason_of(const qbrain::ops::OpResult& result) {
  return trailer_field(result.text, "degraded_reason");
}

std::vector<Row> rows_of(const qbrain::ops::OpResult& result) {
  QB_CHECK(result.ok);
  const auto parsed = json::parse(result.json);
  QB_CHECK(parsed.is_array());
  std::vector<Row> rows;
  for (const auto& row : parsed) {
    QB_CHECK(row.is_object() && row.size() == 5);
    QB_CHECK(row.contains("source_id") && row.contains("slug") &&
             row.contains("line") && row.contains("snippet") &&
             row.contains("kind"));
    Row out;
    out.slug = row["slug"].get<std::string>();
    out.line = row["line"].get<int>();
    out.kind = row["kind"].get<std::string>();
    out.snippet = row["snippet"].get<std::string>();
    QB_CHECK(out.line >= 1);
    QB_CHECK(out.snippet.size() <= 200);
    rows.push_back(std::move(out));
  }
  return rows;
}

void require_rows(const qbrain::ops::OpResult& result,
                  std::initializer_list<std::pair<int, std::string>> expected) {
  const auto rows = rows_of(result);
  std::string actual;
  for (const auto& row : rows)
    actual += "(" + std::to_string(row.line) + "," + row.kind + ")";
  std::string wanted;
  for (const auto& row : expected)
    wanted += "(" + std::to_string(row.first) + "," + row.second + ")";
  if (rows.size() != expected.size() || [&] {
        size_t index = 0;
        for (const auto& want : expected) {
          if (rows[index].line != want.first || rows[index].kind != want.second)
            return true;
          ++index;
        }
        return false;
      }()) {
    throw std::runtime_error("row mismatch: actual " + actual + " expected " + wanted);
  }
}

qbrain::ops::OpResult call_code_op(qbrain::Brain& brain, const std::string& name,
                                   const std::string& source,
                                   const std::string& symbol) {
  return qbrain::test_support::call_op(
      brain, name,
      {{"source_id", source}, {"symbol", symbol}, {"limit", "200"}, {"page_limit", "50"}});
}

json require_error(const qbrain::ops::OpResult& result, const std::string& code) {
  QB_CHECK(!result.ok);
  QB_CHECK(result.exit_code != 0);
  const auto payload = json::parse(result.json);
  QB_CHECK(payload.contains("error"));
  QB_CHECK(payload["error"]["code"] == code);
  return payload["error"];
}

// Counts reads of page-bearing tables so rejection paths can be proven to run
// before any page content is retrieved.
class PageReadObserver {
 public:
  explicit PageReadObserver(qbrain::Brain& brain)
      : database_(brain.db().handle()) {
    QB_CHECK(sqlite3_set_authorizer(database_, &PageReadObserver::authorize, this) ==
             SQLITE_OK);
  }

  ~PageReadObserver() { sqlite3_set_authorizer(database_, nullptr, nullptr); }

  int page_reads() const { return page_reads_; }

 private:
  static int authorize(void* context, int action, const char* table, const char*,
                       const char*, const char*) {
    if (action != SQLITE_READ || !table) return SQLITE_OK;
    const std::string name(table);
    if (name == "pages" || name == "content_chunks" || name == "links" ||
        name.rfind("pages_fts", 0) == 0) {
      ++static_cast<PageReadObserver*>(context)->page_reads_;
    }
    return SQLITE_OK;
  }

  sqlite3* database_ = nullptr;
  int page_reads_ = 0;
};

bool is_code_fixture_extension(const std::string& extension) {
  static const char* known[] = {".cpp", ".hpp", ".cc", ".h", ".ts", ".tsx"};
  std::string lower;
  for (char c : extension) lower += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
  for (const char* candidate : known)
    if (lower == candidate) return true;
  return false;
}

fs::path n32_fixture_dir() {
  if (const char* override_path = std::getenv("QBRAIN_N32_FIXTURES")) {
    const fs::path provided(override_path);
    if (!fs::is_directory(provided))
      throw std::runtime_error("QBRAIN_N32_FIXTURES is not a directory");
    return provided;
  }
  const fs::path candidates[] = {"tests/fixtures/astlite", "../tests/fixtures/astlite",
                                 "../../tests/fixtures/astlite",
                                 "../../../tests/fixtures/astlite"};
  for (const auto& candidate : candidates)
    if (fs::is_directory(candidate)) return candidate;
  throw std::runtime_error(
      "N32 fixtures directory missing (expected tests/fixtures/astlite)");
}

std::string read_binary_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read fixture: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

fs::path expected_sidecar_for(const fs::path& fixture) {
  const std::string filename = fixture.filename().string();
  const std::string stem = fixture.stem().string();
  const fs::path parent = fixture.parent_path();
  const fs::path candidates[] = {parent / (filename + ".json"),
                                 parent / (stem + ".expected.json"),
                                 parent / (stem + ".json")};
  for (const auto& candidate : candidates)
    if (fs::is_regular_file(candidate)) return candidate;
  throw std::runtime_error("fixture has no expected JSON sidecar: " + filename);
}

qbrain::codeintel::astlite::Language fixture_language(const fs::path& fixture) {
  std::string extension;
  for (char c : fixture.extension().string())
    extension += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
  return (extension == ".ts" || extension == ".tsx")
             ? qbrain::codeintel::astlite::Language::TypeScript
             : qbrain::codeintel::astlite::Language::Cpp;
}

void run_fixture_golden(qbrain::Brain& brain, int fixture_index,
                        const fs::path& fixture) {
  const std::string name = fixture.filename().string();
  const std::string body = read_binary_file(fixture);
  const auto language = fixture_language(fixture);

  // 1. parser golden + determinism: two parses serialize byte-identically and
  //    match the sidecar structure exactly (every symbol, line and column).
  //    Byte-level determinism is asserted on to_json output; the sidecar is
  //    compared as parsed JSON so whitespace/newline conventions cannot mask
  //    or fake a match.
  const std::string first =
      qbrain::codeintel::astlite::to_json(
          qbrain::codeintel::astlite::parse_content(body, language));
  const std::string second =
      qbrain::codeintel::astlite::to_json(
          qbrain::codeintel::astlite::parse_content(body, language));
  QB_CHECK(first == second);
  const fs::path sidecar = expected_sidecar_for(fixture);
  const std::string sidecar_body = read_binary_file(sidecar);
  const json golden = json::parse(sidecar_body);
  if (json::parse(first) != golden) {
    std::ofstream dump(fs::temp_directory_path() / "n32_fixture_actual.json",
                       std::ios::binary);
    dump << first;
    throw std::runtime_error("fixture golden mismatch: " + name);
  }
  const std::string golden_mode = golden.value("mode", std::string("structured"));
  const std::string golden_reason = golden.value("degraded_reason", std::string());

  // 2. ops-layer integration: ingest the fixture as a page and run all six
  //    code ops on its first definition name. The mode trailer and degradation
  //    reason must match the parser golden; pages over the preserved N22
  //    16 KiB budget fail the callees/flow/blast path with a bounded
  //    resource_limit error instead of scanning.
  const auto& definitions = golden["symbols"]["definitions"];
  QB_CHECK(definitions.is_array() && !definitions.empty());
  const std::string symbol = definitions[0].value("name", std::string());
  QB_CHECK(!symbol.empty());
  const std::string source = "n32_fx_" + std::to_string(fixture_index);
  qbrain::test_support::put_page(brain, source, name, body);

  for (const auto& op : {"code_def", "code_refs", "code_callers", "code_callees",
                         "code_flow", "code_blast"}) {
    const bool n22_budget_op = std::string(op) != "code_def" &&
                               std::string(op) != "code_refs" &&
                               std::string(op) != "code_callers";
    qbrain::ops::OpResult result;
    if (std::string(op) == "code_flow") {
      result = qbrain::test_support::call_op(
          brain, op,
          {{"source_id", source}, {"entry_point", symbol}, {"depth", "2"},
           {"limit", "200"}, {"page_limit", "50"}});
    } else {
      result = call_code_op(brain, op, source, symbol);
    }
    if (body.size() > 16 * 1024 && n22_budget_op) {
      require_error(result, "resource_limit");
      continue;
    }
    QB_CHECK(result.ok);
    require_mode(result, golden_mode);
    if (!golden_reason.empty()) {
      QB_CHECK(degraded_reason_of(result) == golden_reason);
    } else if (golden_mode == "structured") {
      require_no_degradation(result);
    }
    rows_of(result);  // row-shape and bounds check
  }
}

}  // namespace

void test_n32_scan_integration() {
  const auto workdir = fs::temp_directory_path() / "qbrain_test_n32";
  std::error_code cleanup_error;
  fs::remove_all(workdir, cleanup_error);
  fs::create_directories(workdir);
  const auto database_path = workdir / "brain.db";

  qbrain::Brain brain("n32_scan");
  brain.open_at(qbrain::util::path_to_utf8(database_path));

  // ---- embedded golden pages --------------------------------------------
  const std::string cpp_golden =
      "namespace demo {\n"
      "class Widget {\n"
      " public:\n"
      "  void spin() {\n"
      "    helper(1);\n"
      "    helper(2);\n"
      "  }\n"
      "};\n"
      "int helper(int v) {\n"
      "  return v * 2;\n"
      "}\n"
      "void run() {\n"
      "  Widget w;\n"
      "  w.spin();\n"
      "  helper(3);\n"
      "}\n"
      "}\n";
  qbrain::test_support::put_page(brain, "n32_cpp", "n32/gold/sample.cpp", cpp_golden);

  const std::string ts_golden =
      "export function alpha(x: number): number {\n"
      "  return beta(x) + 1;\n"
      "}\n"
      "function beta(x: number): number {\n"
      "  return x * 2;\n"
      "}\n"
      "export const gamma = () => alpha(3);\n";
  qbrain::test_support::put_page(brain, "n32_ts", "n32/gold/app.ts", ts_golden);

  // ---- mode matrix + structured golden rows (C++) -----------------------
  {
    const auto defs = call_code_op(brain, "code_def", "n32_cpp", "helper");
    require_mode(defs, "structured");
    require_no_degradation(defs);
    require_rows(defs, {{9, "def"}});
    QB_CHECK(rows_of(defs)[0].snippet == "int helper(int v) {");

    const auto callers = call_code_op(brain, "code_callers", "n32_cpp", "helper");
    require_mode(callers, "structured");
    require_rows(callers, {{5, "call"}, {6, "call"}, {15, "call"}});

    const auto callees = call_code_op(brain, "code_callees", "n32_cpp", "spin");
    require_mode(callees, "structured");
    require_rows(callees, {{5, "callee:helper"}, {6, "callee:helper"}});

    const auto flow = qbrain::test_support::call_op(
        brain, "code_flow",
        {{"source_id", "n32_cpp"}, {"entry_point", "run"}, {"depth", "2"},
         {"limit", "200"}, {"page_limit", "50"}});
    require_mode(flow, "structured");
    require_rows(flow, {{14, "flow:d1:spin"}, {15, "flow:d1:helper"}});

    // refs come from the structured occurrence union (definition names,
    // references, call sites) — comment/string mentions are NOT refs here,
    // unlike the regex fallback
    const auto refs = call_code_op(brain, "code_refs", "n32_cpp", "helper");
    require_mode(refs, "structured");
    require_no_degradation(refs);
    require_rows(refs, {{5, "ref"}, {6, "ref"}, {9, "ref"}, {15, "ref"}});

    // blast composes four structured categories (line-deduplicated)
    const auto blast = call_code_op(brain, "code_blast", "n32_cpp", "helper");
    require_mode(blast, "structured");
    require_rows(blast, {{9, "def"}, {5, "ref"}, {6, "ref"}, {15, "ref"}});
  }

  // ---- structured golden rows (TypeScript) ------------------------------
  {
    const auto defs = call_code_op(brain, "code_def", "n32_ts", "alpha");
    require_mode(defs, "structured");
    require_rows(defs, {{1, "def"}});
    QB_CHECK(rows_of(defs)[0].snippet == "export function alpha(x: number): number {");

    QB_CHECK(rows_of(call_code_op(brain, "code_def", "n32_ts", "beta"))[0].line == 4);
    QB_CHECK(rows_of(call_code_op(brain, "code_def", "n32_ts", "gamma"))[0].line == 7);

    const auto callers = call_code_op(brain, "code_callers", "n32_ts", "beta");
    require_mode(callers, "structured");
    require_rows(callers, {{2, "call"}});

    const auto callees = call_code_op(brain, "code_callees", "n32_ts", "gamma");
    require_mode(callees, "structured");
    require_rows(callees, {{7, "callee:alpha"}});

    const auto refs = call_code_op(brain, "code_refs", "n32_ts", "beta");
    require_mode(refs, "structured");
    require_rows(refs, {{2, "ref"}, {4, "ref"}});
  }

  // ---- language gate: every code extension routes to the structured path --
  {
    qbrain::test_support::put_page(brain, "n32_ext", "n32/v/a.hpp",
                                   "int f_hpp() { return 0; }\n");
    qbrain::test_support::put_page(brain, "n32_ext", "n32/v/b.cc",
                                   "int f_cc() { return 0; }\n");
    qbrain::test_support::put_page(brain, "n32_ext", "n32/v/c.h",
                                   "int f_h() { return 0; }\n");
    qbrain::test_support::put_page(brain, "n32_ext", "n32/v/d.tsx",
                                   "export function f_tsx(): void {}\n");
    qbrain::test_support::put_page(brain, "n32_ext", "n32/v/E.CPP",
                                   "int f_upper() { return 0; }\n");
    // A dot that is not a code extension must NOT enable the structured path
    // (own source so it cannot downgrade the structured assertions above).
    qbrain::test_support::put_page(brain, "n32_plain", "n32/v/notes.md",
                                   "int f_md() { return 0; }\n");

    for (const auto& symbol : {"f_hpp", "f_cc", "f_h", "f_tsx", "f_upper"}) {
      const auto defs = call_code_op(brain, "code_def", "n32_ext", symbol);
      require_mode(defs, "structured");
      QB_CHECK(!rows_of(defs).empty());
    }
    const auto md_defs = call_code_op(brain, "code_def", "n32_plain", "f_md");
    require_mode(md_defs, "heuristic");
    require_rows(md_defs, {{1, "def"}});
  }

  // ---- mixed and empty sources ------------------------------------------
  {
    qbrain::test_support::put_page(brain, "n32_mixed", "n32/m/code.cpp",
                                   "int mixed_fn() { return 1; }\n");
    qbrain::test_support::put_page(brain, "n32_mixed", "n32/m/notes",
                                   "int plain_fn() { return 2; }\n");
    const auto defs = call_code_op(brain, "code_def", "n32_mixed", "mixed_fn");
    require_mode(defs, "heuristic");  // any regex-fallback page downgrades the label
    require_rows(defs, {{1, "def"}});
    QB_CHECK(!rows_of(call_code_op(brain, "code_def", "n32_mixed", "plain_fn"))
                  .empty());

    brain.ensure_source("n32_empty");
    const auto empty = call_code_op(brain, "code_def", "n32_empty", "anything");
    require_mode(empty, "heuristic");
    require_no_degradation(empty);
    QB_CHECK(rows_of(empty).empty());
    QB_CHECK(empty.text.find("(no matches)\n") == 0);
  }

  // ---- degradation: nesting depth over the astlite bound (64) ------------
  {
    std::string deep = "void deep() {\n";
    for (int level = 0; level < 80; ++level)
      deep += "  { int v" + std::to_string(level) + " = " +
              std::to_string(level) + ";\n";
    for (int level = 0; level < 80; ++level) deep += "  }\n";
    deep += "}\n";
    qbrain::test_support::put_page(brain, "n32_deep", "n32/deep.cpp", deep);

    const auto defs = call_code_op(brain, "code_def", "n32_deep", "deep");
    require_mode(defs, "heuristic");
    QB_CHECK(!degraded_reason_of(defs).empty());
    require_rows(defs, {{1, "def"}});  // regex fallback still finds the definition
  }

  // ---- malformed / binary / invalid UTF-8 bodies: bounded, alive ----------
  {
    std::string junk;
    junk.push_back('\x00');
    junk += "\x01\x02\xFF\xC0\x80\xFE\xFD";
    junk += "int junk() { junk(";
    junk.append(512, '\x80');
    junk += "\xFF\xFE(\x80";
    junk.append(64, 'x');
    junk += "\x00";
    qbrain::test_support::put_page(brain, "n32_junk", "n32/junk.cpp", junk);

    for (const auto& op : {"code_def", "code_refs", "code_callers", "code_callees",
                           "code_flow", "code_blast"}) {
      const auto result = call_code_op(brain, op, "n32_junk", "junk");
      QB_CHECK(result.ok);  // no crash, no hang, bounded output
      const auto mode = mode_of(result);
      QB_CHECK(mode == "structured" || mode == "heuristic");
      const auto rows = rows_of(result);
      QB_CHECK(rows.size() <= 200);
      for (const auto& row : rows) QB_CHECK(row.snippet.size() <= 200);
    }
  }

  // ---- N22 page-size budget preserved (resource_limit, not a crash) ------
  {
    std::string big(17 * 1024, 'x');
    big = "// n32 oversize page\n" + big;
    qbrain::test_support::put_page(brain, "n32_big", "n32/big.cpp", big);

    const auto callees = call_code_op(brain, "code_callees", "n32_big", "big");
    require_error(callees, "resource_limit");

    // The defs path has no N22 page budget and must stay bounded and alive.
    const auto defs = call_code_op(brain, "code_def", "n32_big", "big");
    QB_CHECK(defs.ok);
    const auto mode = mode_of(defs);
    QB_CHECK(mode == "structured" || mode == "heuristic");
  }

  // ---- determinism: two runs are byte-identical --------------------------
  {
    for (const auto& op : {"code_def", "code_refs", "code_callers", "code_callees",
                           "code_flow", "code_blast"}) {
      const auto first = call_code_op(brain, op, "n32_cpp", "helper");
      const auto second = call_code_op(brain, op, "n32_cpp", "helper");
      QB_CHECK(first.json == second.json);
      QB_CHECK(first.text == second.text);
    }
    const auto deep_first = call_code_op(brain, "code_def", "n32_deep", "deep");
    const auto deep_second = call_code_op(brain, "code_def", "n32_deep", "deep");
    QB_CHECK(deep_first.json == deep_second.json);
    QB_CHECK(deep_first.text == deep_second.text);
  }

  // ---- path isolation: rejection happens before page content retrieval ---
  {
    brain.ensure_source("n32_other");
    PageReadObserver observer(brain);

    const auto missing = call_code_op(brain, "code_def", "n32_ghost", "helper");
    require_error(missing, "source_not_found");
    QB_CHECK(observer.page_reads() == 0);

    const auto invalid = qbrain::test_support::call_op(
        brain, "code_def",
        {{"source_id", "bad id!"}, {"symbol", "helper"}});
    require_error(invalid, "invalid_source");
    QB_CHECK(observer.page_reads() == 0);

    // Remote callers need the source allow-list; denial happens before reads.
    const auto remote_denied = qbrain::test_support::call_op(
        brain, "code_def", {{"source_id", "n32_other"}, {"symbol", "helper"}}, true);
    require_error(remote_denied, "source_not_allowed");
    QB_CHECK(observer.page_reads() == 0);

    brain.save_config_value("mcp.allowed_sources", "n32_other");
    const auto remote_allowed = qbrain::test_support::call_op(
        brain, "code_def", {{"source_id", "n32_other"}, {"symbol", "helper"}}, true);
    QB_CHECK(remote_allowed.ok);
    QB_CHECK(mode_of(remote_allowed) == "heuristic");  // empty source: heuristic
  }

  // ---- N32-A fixture goldens (tests/fixtures/astlite) --------------------
  {
    const fs::path fixture_dir = n32_fixture_dir();
    std::vector<fs::path> fixtures;
    for (const auto& entry : fs::directory_iterator(fixture_dir)) {
      if (!entry.is_regular_file()) continue;
      if (is_code_fixture_extension(entry.path().extension().string()))
        fixtures.push_back(entry.path());
    }
    std::sort(fixtures.begin(), fixtures.end(),
              [](const fs::path& a, const fs::path& b) {
                return a.filename().string() < b.filename().string();
              });
    QB_CHECK(!fixtures.empty());  // D3: at least the golden + limit fixtures
    for (size_t index = 0; index < fixtures.size(); ++index)
      run_fixture_golden(brain, static_cast<int>(index), fixtures[index]);
  }

  brain.close();
  fs::remove_all(workdir, cleanup_error);
}

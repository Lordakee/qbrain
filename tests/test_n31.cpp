// tests/test_n31.cpp — N31 registry/MCP contract closure tests.
// Sections are owned by parallel subagents; keep them separated:
//   // --- n31-a: counts/mapping ---   (subagent A: D1/D2/D3; appends above)
//   // --- n31-c: negatives ---        (subagent C: D6; appends below)

#include "qbrain/core/brain.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#ifndef QB_CHECK
#define QB_CHECK(cond)                                                          \
  do {                                                                          \
    if (!(cond)) {                                                              \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond + " @ " +  \
                               __FILE__ + ":" + std::to_string(__LINE__));      \
    }                                                                           \
  } while (0)
#endif

// --- n31-a: counts/mapping ---

// D1/D2/D3 (subagent A). Four-way reconciliation between
//   (1) the runtime registry,
//   (2) docs/nodes/n31-evidence/OPS-INVENTORY.json (generated),
//   (3) docs/OPS-PARITY-LEDGER.md (upstream 104 + extensions 3 tables),
//   (4) the PRE-GATE frozen count N=108 (source register_one count).
// Determinism note: the byte-identical regeneration of OPS-INVENTORY.json is
// proven by scripts/gen-ops-inventory.ps1 -VerifyDeterminism and recorded in
// docs/nodes/n31-evidence/MAPPING-CLOSURE.md (two consecutive runs, equal
// SHA-256). The inventory is additionally asserted name-sorted here so any
// nondeterministic serialization shows up as a suite failure.
//
// The env-gated block below is the ONLY runtime hook: when
// QBRAIN_N31_EXPORT_REGISTRY points at a writable path, the frozen registry
// (name/scope/local_only/description/input schema per op) is exported there
// as JSON for scripts/gen-ops-inventory.ps1. No production code is involved.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

// N frozen at the N31 pre-gate (PRE-GATE.json source_register_one_count).
constexpr int kN31FrozenRegistryCount = 108;
// Ledger upstream/extension implemented rows frozen at the N31 pre-gate.
constexpr int kN31LedgerUpstreamRows = 104;
constexpr int kN31LedgerExtensionRows = 4;  // N31 merge: list_job_messages reconciled into extensions table

class N31aScopedEnv {
 public:
  N31aScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* previous = std::getenv(name)) previous_ = previous;
    if (_putenv_s(name, value.c_str()) != 0) {
      throw std::runtime_error("failed to set environment variable");
    }
  }
  ~N31aScopedEnv() {
    _putenv_s(name_, previous_ ? previous_->c_str() : "");
  }
  N31aScopedEnv(const N31aScopedEnv&) = delete;
  N31aScopedEnv& operator=(const N31aScopedEnv&) = delete;

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

// Local (CLI-semantics) registry call used by the D3 gap-op assertions below.
qbrain::ops::OpResult n31a_call(qbrain::Brain& brain, const std::string& name,
                                std::unordered_map<std::string, std::string> args = {}) {
  qbrain::ops::OpContext ctx;
  ctx.brain = &brain;
  ctx.args = std::move(args);
  return qbrain::ops::global_registry().call(name, ctx);
}

std::string n31a_read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("fixture read failed: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Walks up from the working directory (build\cl under the canonical test
// invocation) until docs/OPS-PARITY-LEDGER.md is visible; the repo root.
std::filesystem::path n31a_repo_root() {
  std::filesystem::path root = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    if (std::filesystem::exists(root / "docs" / "OPS-PARITY-LEDGER.md")) return root;
    if (!root.has_parent_path() || root.parent_path() == root) break;
    root = root.parent_path();
  }
  throw std::runtime_error(
      "repo root not found from working directory (run qbrain_tests.exe from the repository)");
}

const char* n31a_scope_name(qbrain::ops::Scope scope) {
  switch (scope) {
    case qbrain::ops::Scope::Read:
      return "read";
    case qbrain::ops::Scope::Write:
      return "write";
    case qbrain::ops::Scope::Admin:
      return "admin";
  }
  return "?";
}

std::set<std::string> n31a_ledger_rows(const std::string& markdown, bool extensions) {
  // Upstream table rows: "| op | **implemented** | ..."; extension rows:
  // "| op | implemented ...". Rows are attributed by table section; an op id
  // is a single lowercase token and the status cell must start with
  // "implemented" (bold-wrapped or plain).
  std::set<std::string> names;
  bool in_section = false;
  bool in_extensions = false;
  size_t cursor = 0;
  while (cursor < markdown.size()) {
    const size_t newline = markdown.find('\n', cursor);
    const std::string line =
        markdown.substr(cursor, newline == std::string::npos ? std::string::npos
                                                             : newline - cursor);
    if (line.rfind("## ", 0) == 0) in_section = false;  // any heading closes
    if (line.rfind("| upstream_op |", 0) == 0) {
      in_section = true;
      in_extensions = false;
    } else if (line.rfind("## Qbrain extensions", 0) == 0) {
      in_section = true;
      in_extensions = true;
    }
    if (in_section && in_extensions == extensions && !line.empty() && line[0] == '|') {
      std::vector<std::string> cells;
      size_t pos = 1;
      while (pos < line.size()) {
        const size_t next = line.find('|', pos);
        if (next == std::string::npos) break;
        std::string cell = line.substr(pos, next - pos);
        const size_t begin = cell.find_first_not_of(' ');
        const size_t end = cell.find_last_not_of(' ');
        cell = begin == std::string::npos ? "" : cell.substr(begin, end - begin + 1);
        cells.push_back(cell);
        pos = next + 1;
      }
      const bool id_token = cells.size() >= 2 && !cells[0].empty() &&
                            cells[0].find('*') == std::string::npos &&
                            cells[0].find('-') == std::string::npos &&
                            cells[0].find(' ') == std::string::npos &&
                            cells[0] != "upstream_op" && cells[0] != "op";
      const bool implemented = cells.size() >= 2 &&
                               (cells[1].rfind("**implemented**", 0) == 0 ||
                                cells[1].rfind("implemented", 0) == 0);
      if (id_token && implemented) names.insert(cells[0]);
    }
    if (newline == std::string::npos) break;
    cursor = newline + 1;
  }
  return names;
}

}  // namespace

void test_n31_a_counts_mapping() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  // ---- (1) runtime registry side of the four-way reconciliation ----
  const auto ops = qbrain::ops::global_registry().list();
  QB_CHECK(static_cast<int>(ops.size()) == kN31FrozenRegistryCount);
  std::vector<std::string> runtime_names;
  for (const auto* op : ops) {
    runtime_names.push_back(op->name);
    QB_CHECK(op->scope == qbrain::ops::Scope::Read ||
             op->scope == qbrain::ops::Scope::Write ||
             op->scope == qbrain::ops::Scope::Admin);
  }
  QB_CHECK(std::is_sorted(runtime_names.begin(), runtime_names.end()));

  // ---- env-gated runtime export (input for gen-ops-inventory.ps1) ----
  if (const char* export_path = std::getenv("QBRAIN_N31_EXPORT_REGISTRY")) {
    json export_doc = json::object();
    export_doc["ops"] = json::array();
    for (const auto* op : ops) {
      export_doc["ops"].push_back({
          {"name", op->name},
          {"scope", n31a_scope_name(op->scope)},
          {"local_only", op->local_only},
          {"description", op->description},
          {"input_schema_json", op->input_schema_json},
      });
    }
    std::ofstream out(fs::path(export_path), std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("QBRAIN_N31_EXPORT_REGISTRY path not writable");
    out << export_doc.dump() << "\n";
    out.close();
    if (!out) throw std::runtime_error("registry export write failed");
    std::cout << "[INFO] n31-a: registry export written (" << ops.size() << " ops)\n";
  }

  // ---- (2)+(3)+(4) generated inventory vs ledger vs frozen counts ----
  const fs::path root = n31a_repo_root();
  const fs::path inventory_path = root / "docs" / "nodes" / "n31-evidence" / "OPS-INVENTORY.json";
  QB_CHECK(fs::exists(inventory_path));
  const auto inventory = json::parse(n31a_read_file(inventory_path));

  QB_CHECK(inventory.contains("ops") && inventory["ops"].is_array());
  const auto& rows = inventory["ops"];
  QB_CHECK(static_cast<int>(rows.size()) == kN31FrozenRegistryCount);
  QB_CHECK(inventory["counts"]["registry_ops"] == kN31FrozenRegistryCount);
  QB_CHECK(inventory["counts"]["inventory_rows"] == kN31FrozenRegistryCount);
  QB_CHECK(inventory["counts"]["ledger_upstream"] == kN31LedgerUpstreamRows);
  QB_CHECK(inventory["counts"]["ledger_extension"] == kN31LedgerExtensionRows);

  std::set<std::string> inventory_names;
  std::set<std::string> inventory_upstream;
  std::set<std::string> inventory_extension;
  std::set<std::string> inventory_no_ledger;
  std::string previous_name;
  for (const auto& row : rows) {
    QB_CHECK(row.contains("name") && row["name"].is_string());
    const auto name = row["name"].get<std::string>();
    QB_CHECK(inventory_names.insert(name).second);  // unique + sorted order
    QB_CHECK(name > previous_name);
    previous_name = name;
    const auto* runtime_op = qbrain::ops::global_registry().find(name);
    QB_CHECK(runtime_op != nullptr);
    QB_CHECK(row["scope"].get<std::string>() == n31a_scope_name(runtime_op->scope));
    QB_CHECK(row["local_only"] == runtime_op->local_only);
    QB_CHECK(row.contains("description_summary") && row["description_summary"].is_string());
    QB_CHECK(row.contains("has_input_schema") && row["has_input_schema"].is_boolean());
    const auto ledger = row["ledger"].get<std::string>();
    QB_CHECK(ledger == "upstream" || ledger == "extension" || ledger == "no-ledger-diff");
    if (ledger == "upstream") inventory_upstream.insert(name);
    if (ledger == "extension") inventory_extension.insert(name);
    if (ledger == "no-ledger-diff") inventory_no_ledger.insert(name);
    // D3 mapping completeness: every registered op carries at least one
    // extracted primary-path test mapping.
    QB_CHECK(row.contains("tests") && row["tests"].is_array() && !row["tests"].empty());
    for (const auto& test : row["tests"]) {
      QB_CHECK(test["file"].is_string() && test["case"].is_string());
      QB_CHECK(test["file"].get<std::string>().find('/') == std::string::npos);
      QB_CHECK(test["file"].get<std::string>().find('\\') == std::string::npos);
    }
  }

  // Registry and inventory name sets must coincide exactly.
  QB_CHECK(inventory_names == std::set<std::string>(runtime_names.begin(), runtime_names.end()));

  // Ledger markdown is parsed live: both tables must map onto inventory rows.
  const auto ledger_markdown = n31a_read_file(root / "docs" / "OPS-PARITY-LEDGER.md");
  const auto ledger_upstream = n31a_ledger_rows(ledger_markdown, false);
  const auto ledger_extension = n31a_ledger_rows(ledger_markdown, true);
  QB_CHECK(static_cast<int>(ledger_upstream.size()) == kN31LedgerUpstreamRows);
  QB_CHECK(static_cast<int>(ledger_extension.size()) == kN31LedgerExtensionRows);
  QB_CHECK(ledger_upstream == inventory_upstream);
  QB_CHECK(ledger_extension == inventory_extension);

  // extensions_or_diff completeness: exactly the registry ops that have no
  // ledger row, each named with a non-empty reason.
  std::set<std::string> without_ledger_row;
  for (const auto& name : runtime_names) {
    if (ledger_upstream.count(name) == 0 && ledger_extension.count(name) == 0) {
      without_ledger_row.insert(name);
    }
  }
  QB_CHECK(inventory_no_ledger == without_ledger_row);
  QB_CHECK(inventory.contains("extensions_or_diff") &&
           inventory["extensions_or_diff"].is_array());
  std::set<std::string> diff_names;
  for (const auto& entry : inventory["extensions_or_diff"]) {
    QB_CHECK(entry["name"].is_string());
    QB_CHECK(entry["reason"].is_string() && !entry["reason"].get<std::string>().empty());
    QB_CHECK(diff_names.insert(entry["name"].get<std::string>()).second);
  }
  QB_CHECK(diff_names == without_ledger_row);

  // ---- D3: primary-path assertions for the pre-D3 zero-mapping gap ops ----
  // Gap set before D3 (30 ops, recorded in MAPPING-CLOSURE.md): add_link,
  // add_tag, advisor, file_upload, find_orphans, get_calibration_profile,
  // get_chunks, get_links, get_page, get_stats, get_tags,
  // list_brain_skillpack, list_brains, list_pages, ontology_conflicts,
  // ontology_propose, put_raw_data, remove_link, remove_tag, restore_page,
  // revert_version, run_skillopt, schema_explain_type, schema_graph,
  // schema_review_orphans, sources_list, takes_calibration, takes_scorecard,
  // takes_search, whoami. Each is called below with minimal valid arguments
  // on a temp brain; %LOCALAPPDATA% is redirected so nothing touches
  // %LOCALAPPDATA%\Qbrain.
  {
    const fs::path dir = fs::temp_directory_path() / "qbrain_n31_a";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N31aScopedEnv local_app_data("LOCALAPPDATA", local_root);

    qbrain::Brain brain("n31_a_counts");
    brain.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
    brain.ensure_source("default");

    // Fixture pages (put_page/delete_page already carry their own mappings).
    auto put = [&](const std::string& slug, const std::string& body) {
      auto r = n31a_call(brain, "put_page", {{"slug", slug}, {"body", body}});
      QB_CHECK(r.ok);
      return r;
    };
    put("n31-a/gap", "n31-a primary body alpha for chunking");
    put("n31-a/gap2", "n31-a secondary body");

    {
      auto r = n31a_call(brain, "get_stats");
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json)["pages"] >= 2);
    }
    {
      auto r = n31a_call(brain, "get_page", {{"slug", "n31-a/gap"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json)["slug"] == "n31-a/gap");
    }
    {
      auto r = n31a_call(brain, "list_pages", {{"limit", "50"}});
      QB_CHECK(r.ok);
      const auto rows_out = json::parse(r.json);
      QB_CHECK(rows_out.is_array() && rows_out.size() >= 2);
    }
    {
      auto r = n31a_call(brain, "get_chunks", {{"slug", "n31-a/gap"}});
      QB_CHECK(r.ok);
      const auto chunks = json::parse(r.json);
      QB_CHECK(chunks.is_array() && !chunks.empty());
      QB_CHECK(chunks[0]["text"].get<std::string>().find("alpha") != std::string::npos);
    }
    {
      auto r = n31a_call(brain, "add_tag", {{"slug", "n31-a/gap"}, {"tag", "n31-tag"}});
      QB_CHECK(r.ok);
      auto tags = n31a_call(brain, "get_tags", {{"slug", "n31-a/gap"}});
      QB_CHECK(tags.ok);
      QB_CHECK(tags.json.find("n31-tag") != std::string::npos);
      auto removed = n31a_call(brain, "remove_tag", {{"slug", "n31-a/gap"}, {"tag", "n31-tag"}});
      QB_CHECK(removed.ok);
    }
    {
      auto r = n31a_call(brain, "add_link",
                         {{"from", "n31-a/gap"}, {"to", "n31-a/gap2"}, {"link_type", "related"}});
      QB_CHECK(r.ok);
      auto links = n31a_call(brain, "get_links", {{"slug", "n31-a/gap"}, {"depth", "1"}});
      QB_CHECK(links.ok);
      const auto neighbors = json::parse(links.json);
      QB_CHECK(neighbors.is_array() && !neighbors.empty());
      auto removed = n31a_call(brain, "remove_link", {{"from", "n31-a/gap"}, {"to", "n31-a/gap2"}});
      QB_CHECK(removed.ok);
    }
    {
      auto r = n31a_call(brain, "find_orphans", {{"limit", "100"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "advisor", {{"question", "n31 gap advisor probe"}});
      QB_CHECK(r.ok);
      QB_CHECK(r.text.find("Based on") == 0);  // heuristic advice, chat fail-open
    }
    {
      auto r = n31a_call(brain, "whoami");
      QB_CHECK(r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["transport"] == "cli");
      QB_CHECK(payload["remote"] == false);
      QB_CHECK(payload["brain"] == "n31_a_counts");
    }
    {
      auto r = n31a_call(brain, "sources_list");
      QB_CHECK(r.ok);
      QB_CHECK(r.json.find("default") != std::string::npos);
    }
    {
      auto r = n31a_call(brain, "list_brains");
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      // restore_page primary path: soft-delete then restore.
      put("n31-a/restore", "restore me");
      auto deleted = n31a_call(brain, "delete_page", {{"slug", "n31-a/restore"}});
      QB_CHECK(deleted.ok);
      auto r = n31a_call(brain, "restore_page", {{"slug", "n31-a/restore"}});
      QB_CHECK(r.ok);
      QB_CHECK(r.text.find("restored") != std::string::npos);
    }
    {
      // revert_version primary path: an update snapshots the prior body into
      // page_versions; revert to that first version.
      put("n31-a/gap", "n31-a revised body beta");
      const auto versions = brain.list_versions("n31-a/gap", "default");
      QB_CHECK(versions.size() >= 1);
      auto r = n31a_call(brain, "revert_version",
                         {{"slug", "n31-a/gap"},
                          {"version_id", std::to_string(versions.front().id)}});
      QB_CHECK(r.ok);
      QB_CHECK(r.text.find("reverted") != std::string::npos);
    }
    {
      auto r = n31a_call(brain, "takes_search", {{"query", "n31"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "takes_scorecard");
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "takes_calibration", {{"limit", "10"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json)["promoted_from_facts"] == 0);
    }
    {
      auto r = n31a_call(brain, "get_calibration_profile");
      QB_CHECK(r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["version"] == 1);
      QB_CHECK(payload["note"] == "stub profile");
    }
    {
      auto r = n31a_call(brain, "schema_graph");
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "schema_explain_type", {{"type", "note"}});
      QB_CHECK(r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["type"] == "note");
      QB_CHECK(payload["explain"].is_string());
    }
    {
      auto r = n31a_call(brain, "schema_review_orphans", {{"limit", "100"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "ontology_propose", {{"limit", "10"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "ontology_conflicts", {{"limit", "10"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json).is_array());
    }
    {
      auto r = n31a_call(brain, "list_brain_skillpack");
      QB_CHECK(r.ok);
      QB_CHECK(!r.json.empty());
    }
    {
      auto r = n31a_call(brain, "run_skillopt");
      QB_CHECK(r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["mode"] == "no-mutate");
      QB_CHECK(payload["skills"].is_array());
    }
    {
      auto r = n31a_call(brain, "put_raw_data", {{"key", "n31-gap"}, {"content", "hello"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json)["key"] == "n31-gap");
    }
    {
      const fs::path src = dir / "n31a_gap_upload.txt";
      {
        std::ofstream out(src, std::ios::binary);
        out << "n31-a gap upload body\n";
      }
      auto r = n31a_call(brain, "file_upload",
                         {{"path", qbrain::util::path_to_utf8(src)}, {"name", "n31a_gap.txt"}});
      QB_CHECK(r.ok);
      QB_CHECK(json::parse(r.json)["id"] > 0);
    }

    brain.close();
    fs::remove_all(dir);
  }
}

// --- n31-c: negatives ---

// D6 (plan disposition P1-2): input-contract negatives for ops that ALREADY
// have validation (MCP typed-map schema in server.cpp and/or
// validate_allowed_args in handlers.cpp), limited to the qualifying domains
// jobs / schema / chronicle+misc. pages/search/files are explicitly out of
// scope (no existing validation points). Every negative is driven through
// qbrain::mcp::handle_rpc_body (the MCP typed-map path) on a temp-dir brain;
// a few handler-level cases additionally use direct registry calls (CLI
// semantics) to prove both layers agree. No %LOCALAPPDATA%\Qbrain writes.

namespace {

json n31c_tool_call(qbrain::Brain& brain, const qbrain::mcp::ServeOptions& opts,
                    const std::string& op, const std::string& arguments, int id) {
  const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                              ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + op +
                              "\",\"arguments\":" + arguments + "}}";
  return json::parse(qbrain::mcp::handle_rpc_body(brain, opts, request));
}

// Extracts the structured payload embedded in content[0].text and asserts the
// MCP envelope really carried isError=true.
json n31c_error_payload(const json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(response["result"].contains("isError"));
  QB_CHECK(response["result"]["isError"] == true);
  QB_CHECK(response["result"]["content"].is_array());
  QB_CHECK(!response["result"]["content"].empty());
  const auto text = response["result"]["content"][0]["text"].get<std::string>();
  return json::parse(text);
}

void n31c_expect_error(const json& response, const char* code, const char* field) {
  const auto payload = n31c_error_payload(response);
  QB_CHECK(payload.contains("error"));
  QB_CHECK(payload["error"]["code"] == code);
  QB_CHECK(payload["error"]["field"] == field);
  QB_CHECK(payload["error"]["message"].is_string());
}

void n31c_expect_invalid_argument(const json& response, const char* field) {
  n31c_expect_error(response, "invalid_argument", field);
}

}  // namespace

// N31 merge-fix (subagent A, 2026-08-15): `if constexpr` outside a template
// fully checks the discarded branch, so subagent C's original inline
// dual-state block did not compile while Registry::add still returns void
// (subagent B's bool-returning add had not landed). The dispatch moved into
// this template helper, where every add-expression goes through RegistryT
// and is type-dependent, so the discarded branch is not instantiated in the
// transitional state. Both branches keep subagent C's assertions verbatim.
namespace {
template <typename RegistryT>
void n31c_duplicate_registration_defense(RegistryT& local, RegistryT& global,
                                         qbrain::ops::Operation probe,
                                         qbrain::ops::Operation imposter) {
  if constexpr (std::is_same_v<decltype(local.add(probe)), bool>) {
    // Guard landed: full AA4 assertions on the local registry.
    QB_CHECK(local.add(probe) == true);
    QB_CHECK(local.add(imposter) == false);
    const auto* kept = local.find("n31_duplicate_probe");
    QB_CHECK(kept != nullptr);
    QB_CHECK(kept->description == "n31 original probe");
    QB_CHECK(kept->scope == qbrain::ops::Scope::Read);
    qbrain::ops::OpContext ctx;
    const auto called = local.call("n31_duplicate_probe", ctx);
    QB_CHECK(called.ok);
    QB_CHECK(called.text == "original");

    // And on the live global registry: re-adding an existing builtin name is
    // rejected and leaves the original (and the op count) untouched.
    const auto* builtin = global.find("chronicle_on_this_day");
    QB_CHECK(builtin != nullptr);
    qbrain::ops::Operation duplicate = *builtin;
    duplicate.description = "n31 imposter must not replace the builtin";
    const auto count_before = global.list().size();
    QB_CHECK(global.add(std::move(duplicate)) == false);
    QB_CHECK(global.find("chronicle_on_this_day")->description == builtin->description);
    QB_CHECK(global.list().size() == count_before);
    std::cout << "[INFO] n31-c: duplicate-registration defense active (bool add)\n";
  } else {
    // Transitional: subagent B's bool add() duplicate guard had NOT landed
    // when this section was written (registry.hpp still declares void add).
    // The merged suite must exercise the branch above; here we only keep the
    // file compiling mid-merge and record the pending state.
    local.add(probe);
    local.add(imposter);
    QB_CHECK(local.find("n31_duplicate_probe") != nullptr);
    std::cout << "[INFO] n31-c: registry bool-add guard NOT merged yet; AA4 "
                 "assertions deferred to the merged suite\n";
  }
}

}  // namespace

void test_n31_c_negatives() {
  namespace fs = std::filesystem;
  qbrain::ops::register_builtin_ops();

  const fs::path dir = fs::temp_directory_path() / "qbrain_n31_c";
  fs::remove_all(dir);
  fs::create_directories(dir);

  qbrain::Brain brain("n31_c_negatives");
  brain.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
  brain.ensure_source("default");

  qbrain::mcp::ServeOptions opts;  // stdio MCP: local pipe, write opt-in below
  int rpc_id = 0;
  std::cout << "[INFO] n31-c: negatives for jobs(replay_job, send_job_message, "
               "list_job_messages), schema(schema_stats, ontology_get, "
               "reload_schema_pack), chronicle/misc(chronicle_on_this_day, "
               "chronicle_backfill, log_ingest, add_timeline_entry)\n";

  // Controls: valid calls must NOT error, so each negative below fails for the
  // asserted reason (unknown field / wrong type / illegal reference), not
  // because the op is generally broken.
  {
    const auto control =
        n31c_tool_call(brain, opts, "chronicle_on_this_day", R"({"limit":1})", ++rpc_id);
    QB_CHECK(control["result"]["isError"] == false);
    const auto pack = n31c_tool_call(brain, opts, "ontology_get", R"({"id":"default"})",
                                     ++rpc_id);
    QB_CHECK(pack["result"]["isError"] == false);
  }

  // ---- jobs domain -------------------------------------------------------
  // Validation points: MCP typed-map schema (replay_job/send_job_message/
  // list_job_messages in server.cpp) + validate_allowed_args + parse_job_id_alias
  // in handlers.cpp.
  {
    // replay_job (Write): unknown field via the MCP typed-map path. The typed
    // validation runs before the registry write gate, so this is rejected as
    // invalid_argument even while allow_write is still false.
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "replay_job",
                       R"({"job_id":1,"bogus_field":true})", ++rpc_id),
        "bogus_field");
    // wrong type: string where unsigned integer job id is required
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "replay_job", R"({"job_id":"one"})", ++rpc_id),
        "job_id");

    // send_job_message (Write): wrong type (integer sender) + unknown field
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "send_job_message",
                       R"({"job_id":1,"sender":42})", ++rpc_id),
        "sender");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "send_job_message",
                       R"({"job_id":1,"bogus":1})", ++rpc_id),
        "bogus");

    // list_job_messages (Read): unknown field + wrong type + illegal reference
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "list_job_messages",
                       R"({"job_id":1,"zzz":"q"})", ++rpc_id),
        "zzz");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "list_job_messages", R"({"job_id":"seven"})",
                       ++rpc_id),
        "job_id");
    // job 424242 cannot exist on this fresh brain: documented not_found code
    n31c_expect_error(
        n31c_tool_call(brain, opts, "list_job_messages", R"({"job_id":424242})", ++rpc_id),
        "not_found", "job_id");

    // Handler layer agreement (CLI semantics, no typed map): the op's own
    // validate_allowed_args rejects the unknown field with the same code.
    {
      qbrain::ops::OpContext ctx;
      ctx.brain = &brain;
      ctx.args = {{"job_id", "1"}, {"unexpected_job_arg", "x"}};
      const auto r = qbrain::ops::global_registry().call("replay_job", ctx);
      QB_CHECK(!r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["error"]["code"] == "invalid_argument");
      QB_CHECK(payload["error"]["field"] == "unexpected_job_arg");
      QB_CHECK(payload["error"]["message"] == "unexpected argument");
    }

    // Illegal job reference on the Write op once --allow-write is honored.
    opts.allow_write = true;
    n31c_expect_error(
        n31c_tool_call(brain, opts, "replay_job", R"({"job_id":424242})", ++rpc_id),
        "not_found", "job_id");
    opts.allow_write = false;
  }

  // ---- schema domain -----------------------------------------------------
  // Validation points: MCP typed-map schemas (schema_stats/ontology_get/
  // reload_schema_pack) + validate_allowed_args + resolve_source in handlers.cpp,
  // pack_not_failed via schema::load_pack.
  {
    // schema_stats: unknown field + wrong type (limit is UnsignedInteger)
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "schema_stats",
                       R"({"limit":10,"extra_schema_arg":"x"})", ++rpc_id),
        "extra_schema_arg");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "schema_stats", R"({"limit":"ten"})", ++rpc_id),
        "limit");
    // illegal source reference via MCP: non-default source without the
    // mcp.allowed_sources allowlist is documented source_not_allowed
    n31c_expect_error(
        n31c_tool_call(brain, opts, "schema_stats",
                       R"({"source_id":"ghost_source_n31"})", ++rpc_id),
        "source_not_allowed", "source_id");
    // the same ghost source from a local (non-MCP) caller reaches the
    // registration check: documented source_not_found
    {
      qbrain::ops::OpContext ctx;
      ctx.brain = &brain;
      ctx.args = {{"source_id", "ghost_source_n31"}};
      const auto r = qbrain::ops::global_registry().call("schema_stats", ctx);
      QB_CHECK(!r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["error"]["code"] == "source_not_found");
      QB_CHECK(payload["error"]["field"] == "source_id");
    }

    // ontology_get: unknown field + wrong type (id is String) + unregistered pack
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "ontology_get", R"({"id":"default","nope":1})", ++rpc_id),
        "nope");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "ontology_get", R"({"id":42})", ++rpc_id), "id");
    // "n31_no_such_pack" is a well-formed id that is not installed
    n31c_expect_error(
        n31c_tool_call(brain, opts, "ontology_get", R"({"id":"n31_no_such_pack"})", ++rpc_id),
        "pack_not_found", "id");

    // reload_schema_pack (Write): argument negatives only; no valid reload is
    // issued, so nothing mutates.
    opts.allow_write = true;
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "reload_schema_pack",
                       R"({"id":"default","bogus":true})", ++rpc_id),
        "bogus");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "reload_schema_pack", R"({"id":7})", ++rpc_id), "id");
    opts.allow_write = false;
  }

  // ---- chronicle/misc domain ---------------------------------------------
  // Validation points: MCP typed-map schemas (chronicle_on_this_day/
  // chronicle_backfill/log_ingest/add_timeline_entry) + validate_allowed_args +
  // valid_utc_day/valid_utc_since + resolve_source in handlers.cpp.
  {
    // chronicle_on_this_day: unknown field + wrong type (date is String)
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "chronicle_on_this_day",
                       R"({"date":"2024-03-01","bogus":1})", ++rpc_id),
        "bogus");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "chronicle_on_this_day", R"({"date":20240301})", ++rpc_id),
        "date");
    // typed map accepts any string; the handler rejects impossible calendar
    // dates (month 13) with its documented code/field
    {
      const auto resp = n31c_tool_call(brain, opts, "chronicle_on_this_day",
                                       R"({"date":"2024-13-45"})", ++rpc_id);
      n31c_expect_invalid_argument(resp, "date");
      const auto payload = n31c_error_payload(resp);
      QB_CHECK(payload["error"]["message"] == "real UTC YYYY-MM-DD required");
    }
    // illegal source reference via MCP (no allowlist) vs local caller
    n31c_expect_error(
        n31c_tool_call(brain, opts, "chronicle_on_this_day",
                       R"({"source_id":"ghost_source_n31"})", ++rpc_id),
        "source_not_allowed", "source_id");
    {
      qbrain::ops::OpContext ctx;
      ctx.brain = &brain;
      ctx.args = {{"source_id", "ghost_source_n31"}};
      const auto r = qbrain::ops::global_registry().call("chronicle_on_this_day", ctx);
      QB_CHECK(!r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["error"]["code"] == "source_not_found");
      QB_CHECK(payload["error"]["field"] == "source_id");
    }
    // handler layer agreement: unknown field through validate_allowed_args
    {
      qbrain::ops::OpContext ctx;
      ctx.brain = &brain;
      ctx.args = {{"source_id", "default"}, {"surprise_arg", "1"}};
      const auto r = qbrain::ops::global_registry().call("chronicle_on_this_day", ctx);
      QB_CHECK(!r.ok);
      const auto payload = json::parse(r.json);
      QB_CHECK(payload["error"]["code"] == "invalid_argument");
      QB_CHECK(payload["error"]["field"] == "surprise_arg");
    }

    // chronicle_backfill (Write): typed argument validation precedes the MCP
    // write gate — an unknown field is invalid_argument even with allow_write
    // off, while valid arguments get the N30 write_denied gate.
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "chronicle_backfill", R"({"bogus":1})", ++rpc_id),
        "bogus");
    n31c_expect_error(
        n31c_tool_call(brain, opts, "chronicle_backfill", R"({"limit":1})", ++rpc_id),
        "write_denied", "operation");
    opts.allow_write = true;
    // wrong type: string where Boolean dry_run is required
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "chronicle_backfill", R"({"dry_run":"yes"})", ++rpc_id),
        "dry_run");
    // handler format check: "since" must be a real UTC date/timestamp
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "chronicle_backfill", R"({"since":"yesterday"})", ++rpc_id),
        "since");
    // illegal source reference (unregistered + not allowlisted)
    n31c_expect_error(
        n31c_tool_call(brain, opts, "chronicle_backfill",
                       R"({"source_id":"ghost_source_n31"})", ++rpc_id),
        "source_not_allowed", "source_id");

    // log_ingest (Write): unknown field + wrong type (keep_last is UnsignedInteger)
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "log_ingest", R"({"path":"x","bogus":1})", ++rpc_id),
        "bogus");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "log_ingest", R"({"keep_last":"ten"})", ++rpc_id),
        "keep_last");
    // log_ingest resolves its source: non-default source needs the MCP allowlist
    n31c_expect_error(
        n31c_tool_call(brain, opts, "log_ingest",
                       R"({"path":"n31c","source_id":"ghost_source_n31"})", ++rpc_id),
        "source_not_allowed", "source_id");

    // add_timeline_entry (Write): unknown field + wrong type (title is String)
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "add_timeline_entry", R"({"title":"x","bogus":1})", ++rpc_id),
        "bogus");
    n31c_expect_invalid_argument(
        n31c_tool_call(brain, opts, "add_timeline_entry", R"({"title":5})", ++rpc_id), "title");
    n31c_expect_error(
        n31c_tool_call(brain, opts, "add_timeline_entry",
                       R"({"title":"x","source_id":"ghost_source_n31"})", ++rpc_id),
        "source_not_allowed", "source_id");
    opts.allow_write = false;
  }

  // ---- D6/AA4: duplicate registration defense -----------------------------
  // Contract (subagent B, N31): Registry::add(Operation) returns true for a
  // fresh name and false for a duplicate name, with the original operation
  // kept (scope/description/handler unchanged) — no silent double registration.
  // A local Registry instance is used so the global registry (whose exact op
  // count the n31-a section freezes) is not polluted by probe operations.
  {
    qbrain::ops::Registry local;
    qbrain::ops::Operation probe;
    probe.name = "n31_duplicate_probe";
    probe.scope = qbrain::ops::Scope::Read;
    probe.local_only = false;
    probe.description = "n31 original probe";
    probe.handler = [](qbrain::ops::OpContext&) {
      qbrain::ops::OpResult r;
      r.ok = true;
      r.text = "original";
      return r;
    };
    qbrain::ops::Operation imposter = probe;
    imposter.description = "n31 imposter probe";
    imposter.scope = qbrain::ops::Scope::Write;
    imposter.handler = [](qbrain::ops::OpContext&) {
      qbrain::ops::OpResult r;
      r.ok = true;
      r.text = "imposter";
      return r;
    };

    // N31 merge-fix (subagent A): see n31c_duplicate_registration_defense
    // above — C's dual-state dispatch moved there so the file compiles both
    // before and after subagent B's bool-returning Registry::add lands.
    n31c_duplicate_registration_defense(local, qbrain::ops::global_registry(),
                                        probe, imposter);
  }

  brain.close();
  fs::remove_all(dir);
}

// (subagent A appends n31-a counts/mapping section above; parent merges)

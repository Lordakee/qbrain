#include "qbrain/core/brain.hpp"
#include "qbrain/graph/traverse.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/service/live_sync.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

namespace {

namespace fs = std::filesystem;

std::string encode_cell(sqlite3_stmt* stmt, int column) {
  std::string out = std::to_string(sqlite3_column_type(stmt, column)) + ":";
  switch (sqlite3_column_type(stmt, column)) {
    case SQLITE_NULL:
      break;
    case SQLITE_INTEGER:
      out += std::to_string(sqlite3_column_int64(stmt, column));
      break;
    case SQLITE_FLOAT: {
      std::ostringstream value;
      value << std::setprecision(std::numeric_limits<double>::max_digits10)
            << sqlite3_column_double(stmt, column);
      out += value.str();
      break;
    }
    case SQLITE_TEXT: {
      const auto* data = sqlite3_column_text(stmt, column);
      const int size = sqlite3_column_bytes(stmt, column);
      out += std::to_string(size) + ":";
      if (data && size > 0) out.append(reinterpret_cast<const char*>(data), size);
      break;
    }
    case SQLITE_BLOB: {
      const auto* data = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, column));
      const int size = sqlite3_column_bytes(stmt, column);
      out += std::to_string(size) + ":";
      static constexpr char hex[] = "0123456789abcdef";
      for (int i = 0; i < size; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0f]);
      }
      break;
    }
  }
  return out;
}

std::string database_snapshot(qbrain::Brain& brain) {
  sqlite3* db = brain.db().handle();
  sqlite3_stmt* table_stmt = nullptr;
  const char* table_sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' "
      "AND name NOT LIKE '%_fts%' ORDER BY name";
  if (sqlite3_prepare_v2(db, table_sql, -1, &table_stmt, nullptr) != SQLITE_OK)
    throw std::runtime_error("snapshot table discovery failed");
  std::vector<std::string> tables;
  while (sqlite3_step(table_stmt) == SQLITE_ROW) {
    const auto* name = sqlite3_column_text(table_stmt, 0);
    if (name) tables.emplace_back(reinterpret_cast<const char*>(name));
  }
  sqlite3_finalize(table_stmt);

  std::string snapshot;
  for (const auto& table : tables) {
    sqlite3_stmt* row_stmt = nullptr;
    const std::string sql = "SELECT * FROM \"" + table + "\"";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &row_stmt, nullptr) != SQLITE_OK)
      throw std::runtime_error("snapshot table read failed: " + table);
    std::vector<std::string> rows;
    const int columns = sqlite3_column_count(row_stmt);
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(row_stmt)) == SQLITE_ROW) {
      std::string row;
      for (int c = 0; c < columns; ++c) {
        if (c) row.push_back('|');
        row += encode_cell(row_stmt, c);
      }
      rows.push_back(std::move(row));
    }
    sqlite3_finalize(row_stmt);
    if (rc != SQLITE_DONE) throw std::runtime_error("snapshot row scan failed: " + table);
    std::sort(rows.begin(), rows.end());
    snapshot += table + "#" + std::to_string(columns) + "\n";
    for (const auto& row : rows) snapshot += row + "\n";
  }
  return snapshot;
}

int64_t scalar(qbrain::Brain& brain, const std::string& sql) {
  auto stmt = brain.db().prepare(sql);
  return stmt.step() ? stmt.column_int(0) : 0;
}

void put_page(qbrain::Brain& brain, const std::string& source, const std::string& slug,
              const std::string& body) {
  qbrain::PageInput input;
  input.source_id = source;
  input.slug = slug;
  input.title = slug;
  input.body = body;
  auto page = brain.put_page(input);
  brain.replace_chunks(page.id, {body});
}

bool has_neighbor(const std::vector<qbrain::graph::Neighbor>& neighbors, const std::string& slug,
                  const std::string& direction, int depth) {
  return std::any_of(neighbors.begin(), neighbors.end(), [&](const auto& item) {
    return item.slug == slug && item.direction == direction && item.depth == depth;
  });
}

int deny_page_delete(void*, int action, const char* arg1, const char*, const char*, const char*) {
  if (action == SQLITE_DELETE && arg1 && std::string(arg1) == "pages") return SQLITE_DENY;
  return SQLITE_OK;
}

qbrain::ops::OpResult call_remote(qbrain::Brain& brain, const std::string& name,
                                  std::unordered_map<std::string, std::string> args,
                                  bool allow_write) {
  qbrain::ops::OpContext ctx;
  ctx.brain = &brain;
  // N30: this helper simulates an MCP client call (old remote semantics).
  ctx.via_mcp = true;
  ctx.allow_write = allow_write;
  ctx.args = std::move(args);
  return qbrain::ops::global_registry().call(name, ctx);
}

}  // namespace

void test_n13() {
  const auto root = fs::temp_directory_path() / "qbrain_n13_test";
  fs::remove_all(root);
  fs::create_directories(root / "notes");
  const auto db_path = root / "brain.db";

  qbrain::Brain brain("N13-Primary");
  brain.open_at(qbrain::util::path_to_utf8(db_path));

  // Live-sync imports, idempotence, changed-file detection, watch-once, and source propagation.
  const auto notes = root / "notes";
  {
    std::ofstream(notes / "one.MD") << "# One\n\nfirst body\n";
    std::ofstream(notes / "two.txt") << "# Two\n\nsecond body\n";
    std::ofstream(notes / "ignored.bin") << "not a note\n";
  }
  auto first = qbrain::service::live_sync_once(brain, qbrain::util::path_to_utf8(notes), "SRC_A");
  QB_CHECK(first.imported_pages == 2);
  QB_CHECK(first.errors == 0);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM pages WHERE source_id='src_a'") == 2);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM pages WHERE source_id='default'") == 0);

  auto second = qbrain::service::live_sync_once(brain, qbrain::util::path_to_utf8(notes), "src_a");
  QB_CHECK(second.imported_pages == 0);
  QB_CHECK(second.skipped == 2);
  {
    std::ofstream(notes / "one.MD") << "# One\n\nchanged body with a longer signature\n";
  }
  auto changed = qbrain::service::live_sync_once(brain, qbrain::util::path_to_utf8(notes), "src_a");
  QB_CHECK(changed.imported_pages == 1);
  fs::create_directories(notes / "new");
  std::ofstream(notes / "new" / "three.md") << "# Three\n\nthird body\n";
  QB_CHECK(qbrain::service::live_sync_watch(brain, qbrain::util::path_to_utf8(notes), 1, 1, "src_a") ==
           1);

  const auto invalid_before = database_snapshot(brain);
  auto invalid = qbrain::service::live_sync_once(brain, qbrain::util::path_to_utf8(notes), "bad/id");
  QB_CHECK(invalid.errors == 1 && invalid.imported_pages == 0);
  QB_CHECK(database_snapshot(brain) == invalid_before);

  // The same root is independent across brains and source ids.
  qbrain::Brain second_brain("N13-Secondary");
  second_brain.open_at(qbrain::util::path_to_utf8(root / "second.db"));
  auto second_scope =
      qbrain::service::live_sync_once(second_brain, qbrain::util::path_to_utf8(notes), "SRC_B");
  QB_CHECK(second_scope.imported_pages == 3);
  QB_CHECK(scalar(second_brain, "SELECT COUNT(*) FROM pages WHERE source_id='src_b'") == 3);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM pages WHERE source_id='src_b'") == 0);
  std::cout << "[INFO] n13_live_sync first_pages=" << first.imported_pages
            << " second_skipped=" << second.skipped << " changed_pages=" << changed.imported_pages
            << " watch_once=1 source_isolation=pass\n";

  // Source status and lifecycle, including FK-safe force removal and rollback injection.
  put_page(brain, "other", "other-page", "other body");
  qbrain::Link other_link;
  other_link.source_id = "other";
  other_link.from_slug = "other-page";
  other_link.to_slug = "other-target";
  other_link.link_type = "related";
  brain.add_link(other_link);
  put_page(brain, "removable", "owned-page", "owned body");
  auto owned = brain.get_page("owned-page", "removable", true);
  QB_CHECK(owned.has_value());
  brain.add_tag("owned-page", "owned", "removable");
  qbrain::Link owned_link;
  owned_link.source_id = "removable";
  owned_link.from_slug = "owned-page";
  owned_link.to_slug = "owned-target";
  owned_link.link_type = "related";
  brain.add_link(owned_link);
  brain.add_fact("owned-page", "kind", "temporary", owned->id);
  auto status = brain.source_status("OTHER");
  QB_CHECK(status.id == "other" && status.pages == 1 && status.links == 1);
  const auto nonforce_before = database_snapshot(brain);
  QB_CHECK(!brain.remove_source("removable", false));
  QB_CHECK(database_snapshot(brain) == nonforce_before);
  QB_CHECK(brain.ensure_source("empty-source"));
  QB_CHECK(brain.remove_source("EMPTY-SOURCE", false));
  QB_CHECK(!brain.remove_source("default", true));

  const auto rollback_before = database_snapshot(brain);
  sqlite3_set_authorizer(brain.db().handle(), deny_page_delete, nullptr);
  QB_CHECK(!brain.remove_source("removable", true));
  sqlite3_set_authorizer(brain.db().handle(), nullptr, nullptr);
  QB_CHECK(database_snapshot(brain) == rollback_before);
  QB_CHECK(brain.remove_source("REMOVABLE", true));
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM sources WHERE id='removable'") == 0);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM pages WHERE source_id='removable'") == 0);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM links WHERE source_id='removable'") == 0);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM facts WHERE page_id=" + std::to_string(owned->id)) == 0);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM pages WHERE source_id='other'") == 1);
  std::cout << "[INFO] n13_source_cleanup rollback_unchanged=pass force_cleanup=pass\n";

  // Bidirectional, depth-limited, source-scoped graph traversal with a cycle.
  put_page(brain, "graph_a", "a", "a");
  put_page(brain, "graph_a", "b", "b");
  put_page(brain, "graph_a", "c", "c");
  put_page(brain, "graph_a", "x", "x");
  qbrain::Link ab{0, "graph_a", "a", "b", "related", "", "manual"};
  qbrain::Link bc{0, "graph_a", "b", "c", "related", "", "manual"};
  qbrain::Link ca{0, "graph_a", "c", "a", "related", "", "manual"};
  qbrain::Link xa{0, "graph_a", "x", "a", "related", "", "manual"};
  brain.add_link(ab);
  brain.add_link(bc);
  brain.add_link(ca);
  brain.add_link(xa);
  qbrain::Link other_source_link{0, "graph_b", "a", "leak", "related", "", "manual"};
  brain.ensure_source("graph_b");
  brain.add_link(other_source_link);
  auto graph = qbrain::graph::neighbors(brain, "a", 2, "graph_a");
  QB_CHECK(has_neighbor(graph, "b", "out", 1));
  QB_CHECK(has_neighbor(graph, "c", "out", 2));
  QB_CHECK(has_neighbor(graph, "x", "in", 1));
  QB_CHECK(!std::any_of(graph.begin(), graph.end(), [](const auto& n) { return n.slug == "leak"; }));
  auto cycle = qbrain::graph::neighbors(brain, "a", 100, "graph_a");
  QB_CHECK(cycle.size() < 20);
  QB_CHECK(qbrain::graph::neighbors(brain, "a", 0, "graph_a").empty());
  std::cout << "[INFO] n13_graph neighbors_depth2=" << graph.size()
            << " cycle_nodes=" << cycle.size() << " source_isolation=pass\n";

  // Retry and fact deactivation state machines.
  auto retry_id = qbrain::jobs::submit_job(brain, "embed", R"({"page_id":1})");
  QB_CHECK(qbrain::jobs::cancel_job(brain, retry_id));
  auto retry_before = qbrain::jobs::get_job(brain, retry_id);
  QB_CHECK(retry_before.has_value());
  QB_CHECK(qbrain::jobs::retry_job(brain, retry_id));
  auto retry_after = qbrain::jobs::get_job(brain, retry_id);
  QB_CHECK(retry_after->status == "waiting");
  QB_CHECK(retry_after->payload_json == retry_before->payload_json);
  QB_CHECK(retry_after->lock_token.empty() && retry_after->error_text.empty());
  QB_CHECK(!qbrain::jobs::retry_job(brain, retry_id));
  qbrain::PageInput fact_input;
  fact_input.source_id = "default";
  fact_input.slug = "fact-a";
  fact_input.title = "fact-a";
  fact_input.body = "fact-a";
  auto fact_a = brain.put_page(fact_input);
  brain.add_fact("entity", "p1", "one", fact_a.id);
  brain.add_fact("entity", "p2", "two", fact_a.id);
  brain.add_fact("other-entity", "p1", "other", fact_a.id);
  QB_CHECK(brain.forget_fact("entity", "p1") == 1);
  QB_CHECK(brain.forget_fact("entity", "p1") == 0);
  QB_CHECK(brain.list_facts("entity").size() == 1);
  QB_CHECK(brain.forget_fact("entity") == 1);
  QB_CHECK(brain.list_facts("entity").empty());
  QB_CHECK(brain.list_facts("other-entity").size() == 1);

  // Registry scope and remote deny: every denied write leaves a full snapshot unchanged.
  qbrain::ops::register_builtin_ops();
  for (const auto& name : {"sync_brain", "sources_remove", "retry_job", "forget_fact"}) {
    auto op = qbrain::ops::global_registry().find(name);
    QB_CHECK(op && op->local_only);
  }
  const auto op_notes = root / "op-notes";
  fs::create_directories(op_notes);
  std::ofstream(op_notes / "op.md") << "# Op\n\noperation note\n";
  const auto denied_before = database_snapshot(brain);
  QB_CHECK(!call_remote(brain, "sync_brain",
                        {{"path", qbrain::util::path_to_utf8(op_notes)}, {"source_id", "ops"}},
                        false)
                .ok);
  QB_CHECK(database_snapshot(brain) == denied_before);
  QB_CHECK(!call_remote(brain, "sources_remove", {{"id", "other"}, {"force", "true"}}, false).ok);
  QB_CHECK(database_snapshot(brain) == denied_before);
  QB_CHECK(!call_remote(brain, "retry_job", {{"id", std::to_string(retry_id)}}, false).ok);
  QB_CHECK(database_snapshot(brain) == denied_before);
  QB_CHECK(!call_remote(brain, "forget_fact", {{"entity_slug", "other-entity"}}, false).ok);
  QB_CHECK(database_snapshot(brain) == denied_before);

  auto sync_allowed = call_remote(
      brain, "sync_brain",
      {{"path", qbrain::util::path_to_utf8(op_notes)}, {"source_id", "ops"}}, true);
  QB_CHECK(sync_allowed.ok);
  QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM pages WHERE source_id='ops'") == 1);
  QB_CHECK(brain.ensure_source("ops-removable"));
  auto op_page = qbrain::PageInput{"ops-removable", "op-owned", "op-owned", "op-owned"};
  brain.put_page(op_page);
  QB_CHECK(call_remote(brain, "sources_remove",
                       {{"id", "ops-removable"}, {"force", "true"}}, true)
               .ok);
  auto allowed_retry_id = qbrain::jobs::submit_job(brain, "embed", "{}");
  QB_CHECK(qbrain::jobs::cancel_job(brain, allowed_retry_id));
  QB_CHECK(call_remote(brain, "retry_job", {{"id", std::to_string(allowed_retry_id)}}, true).ok);
  QB_CHECK(qbrain::jobs::get_job(brain, allowed_retry_id)->status == "waiting");
  brain.add_fact("allow-entity", "p", "value", fact_a.id);
  auto allowed_forget = call_remote(brain, "forget_fact", {{"entity_slug", "allow-entity"}}, true);
  QB_CHECK(allowed_forget.ok);
  QB_CHECK(brain.list_facts("allow-entity").empty());

  QB_CHECK(call_remote(brain, "sources_status", {{"id", "ops"}}, false).ok);
  auto resolved = call_remote(brain, "resolve_slugs", {{"slugs", "op,missing"}, {"source_id", "ops"}}, false);
  QB_CHECK(resolved.ok && resolved.json.find("exists") != std::string::npos);
  auto traversed = call_remote(brain, "traverse_graph", {{"slug", "a"}, {"source_id", "graph_a"}}, false);
  QB_CHECK(traversed.ok);
  auto recalled = call_remote(brain, "recall", {{"query", "operation"}, {"no_vector", "true"}}, false);
  QB_CHECK(recalled.ok);

  const auto denied_hash = qbrain::util::sha256_hex(denied_before);
  std::cout << "[INFO] n13_mcp_deny_snapshot_sha256=" << denied_hash
            << " unchanged=pass allow_write=pass read_ops=pass\n";

  second_brain.close();
  brain.close();
  fs::remove_all(root);
}

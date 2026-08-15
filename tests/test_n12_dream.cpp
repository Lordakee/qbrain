#include "qbrain/core/brain.hpp"
#include "qbrain/cycle/dream.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/hash.hpp"
#include "qbrain/util/paths.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond); \
    }                                                                   \
  } while (0)

namespace {

class ScopedEnv {
 public:
  ScopedEnv(const char* key, const char* value) : key_(key) {
    if (const char* old = std::getenv(key)) old_ = std::string(old);
    set(value);
  }

  ~ScopedEnv() {
    if (old_)
      set(old_->c_str());
    else
      set("");
  }

 private:
  void set(const char* value) {
#ifdef _WIN32
    _putenv_s(key_.c_str(), value ? value : "");
#else
    if (value && *value)
      setenv(key_.c_str(), value, 1);
    else
      unsetenv(key_.c_str());
#endif
  }

  std::string key_;
  std::optional<std::string> old_;
};

std::string quote_identifier(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"') out += '"';
    out += c;
  }
  out += '"';
  return out;
}

void append_hex(std::string& out, const void* data, int size) {
  static constexpr char kHex[] = "0123456789abcdef";
  const auto* bytes = static_cast<const unsigned char*>(data);
  out += std::to_string(size);
  out += ':';
  for (int i = 0; i < size; ++i) {
    out += kHex[bytes[i] >> 4];
    out += kHex[bytes[i] & 0x0f];
  }
}

std::string encode_cell(sqlite3_stmt* st, int column) {
  std::string out = std::to_string(sqlite3_column_type(st, column));
  out += ':';
  switch (sqlite3_column_type(st, column)) {
    case SQLITE_NULL:
      break;
    case SQLITE_INTEGER:
      out += std::to_string(sqlite3_column_int64(st, column));
      break;
    case SQLITE_FLOAT: {
      std::ostringstream oss;
      oss << std::setprecision(std::numeric_limits<double>::max_digits10)
          << sqlite3_column_double(st, column);
      out += oss.str();
      break;
    }
    case SQLITE_TEXT:
      append_hex(out, sqlite3_column_text(st, column), sqlite3_column_bytes(st, column));
      break;
    case SQLITE_BLOB:
      append_hex(out, sqlite3_column_blob(st, column), sqlite3_column_bytes(st, column));
      break;
  }
  return out;
}

std::string database_snapshot(qbrain::Brain& brain) {
  sqlite3* db = brain.db().handle();
  sqlite3_stmt* tables = nullptr;
  const char* table_sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' "
      "ORDER BY name";
  if (sqlite3_prepare_v2(db, table_sql, -1, &tables, nullptr) != SQLITE_OK)
    throw std::runtime_error("snapshot table discovery failed");

  std::vector<std::string> names;
  while (sqlite3_step(tables) == SQLITE_ROW) {
    const auto* text = sqlite3_column_text(tables, 0);
    if (text) names.emplace_back(reinterpret_cast<const char*>(text));
  }
  sqlite3_finalize(tables);

  std::string snapshot;
  for (const auto& name : names) {
    sqlite3_stmt* rows = nullptr;
    const auto sql = "SELECT * FROM " + quote_identifier(name);
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &rows, nullptr) != SQLITE_OK)
      throw std::runtime_error("snapshot table read failed: " + name);

    std::vector<std::string> encoded_rows;
    const int columns = sqlite3_column_count(rows);
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(rows)) == SQLITE_ROW) {
      std::string row;
      for (int i = 0; i < columns; ++i) {
        if (i) row += '|';
        row += encode_cell(rows, i);
      }
      encoded_rows.push_back(std::move(row));
    }
    sqlite3_finalize(rows);
    if (rc != SQLITE_DONE) throw std::runtime_error("snapshot row scan failed: " + name);

    std::sort(encoded_rows.begin(), encoded_rows.end());
    snapshot += name + "#" + std::to_string(columns) + "#" +
                std::to_string(encoded_rows.size()) + "\n";
    for (const auto& row : encoded_rows) snapshot += row + "\n";
  }
  return snapshot;
}

int64_t scalar(qbrain::Brain& brain, const std::string& sql) {
  auto st = brain.db().prepare(sql);
  return st.step() ? st.column_int(0) : 0;
}

int64_t row_count(qbrain::Brain& brain, const char* table) {
  return scalar(brain, std::string("SELECT COUNT(*) FROM ") + table);
}

int64_t page_row_exists(qbrain::Brain& brain, int64_t page_id) {
  auto st = brain.db().prepare("SELECT COUNT(*) FROM pages WHERE id=?");
  st.bind_int(1, page_id);
  return st.step() ? st.column_int(0) : 0;
}

int64_t page_ref_count(qbrain::Brain& brain, const char* table, int64_t page_id) {
  auto st = brain.db().prepare(std::string("SELECT COUNT(*) FROM ") + table + " WHERE page_id=?");
  st.bind_int(1, page_id);
  return st.step() ? st.column_int(0) : 0;
}

int64_t link_ref_count(qbrain::Brain& brain, const std::string& source_id,
                       const std::string& slug) {
  auto st = brain.db().prepare(
      "SELECT COUNT(*) FROM links WHERE source_id=? AND (from_slug=? OR to_slug=?)");
  st.bind_text(1, source_id);
  st.bind_text(2, slug);
  st.bind_text(3, slug);
  return st.step() ? st.column_int(0) : 0;
}

qbrain::Page put_indexed(qbrain::Brain& brain, const std::string& slug,
                         const std::string& title, const std::string& body,
                         const std::string& source_id = "default") {
  QB_CHECK(brain.ensure_source(source_id));
  qbrain::PageInput in;
  in.slug = slug;
  in.title = title;
  in.body = body;
  in.source_id = source_id;
  auto page = brain.put_page(in);
  brain.replace_chunks(page.id, {title + "\n" + body});
  return page;
}

void set_deleted_age(qbrain::Brain& brain, int64_t page_id, int hours) {
  auto st = brain.db().prepare(
      "UPDATE pages SET deleted_at=datetime('now', ?) WHERE id=?");
  st.bind_text(1, "-" + std::to_string(hours) + " hours");
  st.bind_int(2, page_id);
  st.step_done();
}

void add_version(qbrain::Brain& brain, const qbrain::Page& page) {
  auto st = brain.db().prepare(
      "INSERT INTO page_versions(page_id, source_id, slug, title, body) VALUES(?,?,?,?,?)");
  st.bind_int(1, page.id);
  st.bind_text(2, page.source_id);
  st.bind_text(3, page.slug);
  st.bind_text(4, page.title);
  st.bind_text(5, page.body);
  st.step_done();
}

const qbrain::cycle::PhaseResult& phase(const qbrain::cycle::CycleReport& report,
                                        const std::string& name) {
  for (const auto& item : report.phases) {
    if (item.phase == name) return item;
  }
  throw std::runtime_error("missing dream phase: " + name);
}

qbrain::cycle::CycleReport run_phase(qbrain::Brain& brain, const std::string& name,
                                     bool dry_run, const std::string& retention = {}) {
  qbrain::cycle::DreamOpts opts;
  opts.phase = name;
  opts.dry_run = dry_run;
  opts.retention_hours = retention;
  return qbrain::cycle::run_dream(brain, opts);
}

void verify_dry_run_matrix(qbrain::Brain& brain) {
  for (const auto& name : {"orphans", "extract_facts", "consolidate", "embed", "purge"}) {
    const auto before = database_snapshot(brain);
    const auto report = run_phase(brain, name, true);
    QB_CHECK(report.status == "ok");
    QB_CHECK(report.phases.size() == 1);
    QB_CHECK(report.phases[0].phase == name);
    QB_CHECK(report.phases[0].mutations == 0);
    QB_CHECK(database_snapshot(brain) == before);
  }

  const auto before = database_snapshot(brain);
  qbrain::cycle::DreamOpts all;
  all.dry_run = true;
  const auto report = qbrain::cycle::run_dream(brain, all);
  QB_CHECK(report.status == "ok");
  QB_CHECK(report.phases.size() == 5);
  for (const auto& item : report.phases) QB_CHECK(item.mutations == 0);
  QB_CHECK(database_snapshot(brain) == before);
  std::cout << "[INFO] dream_dry_run_snapshot_sha256="
            << qbrain::util::sha256_hex(before)
            << " phases=orphans,extract_facts,consolidate,embed,purge unchanged=pass\n"
            << std::flush;
}

}  // namespace

void test_n12_dream() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "qbrain_n12_dream_test";
  fs::remove_all(root);
  fs::create_directories(root);

  {
    qbrain::Brain brain("n12-dream-phases");
    brain.open_at(qbrain::util::path_to_utf8(root / "phases.db"));

    const auto linked = put_indexed(brain, "phase-linked", "Shared Embed", "same embed body");
    const auto target = put_indexed(brain, "phase-target", "Shared Embed", "same embed body");
    (void)put_indexed(brain, "phase-orphan", "Phase Orphan", "orphan body");
    qbrain::Link link;
    link.from_slug = linked.slug;
    link.to_slug = target.slug;
    brain.add_link(link);
    const auto job1 = qbrain::jobs::submit_job(
        brain, "embed", json({{"page_id", linked.id}}).dump());
    const auto job2 = qbrain::jobs::submit_job(
        brain, "embed", json({{"page_id", target.id}}).dump());
    QB_CHECK(job1 > 0 && job2 > 0);

    const auto protected_deleted =
        put_indexed(brain, "default-apply-protected", "Protected", "must not purge");
    set_deleted_age(brain, protected_deleted.id, 200);

    verify_dry_run_matrix(brain);

    const auto tags_before = row_count(brain, "tags");
    const auto orphan_report = run_phase(brain, "orphans", false);
    QB_CHECK(orphan_report.status == "ok");
    QB_CHECK(phase(orphan_report, "orphans").mutations ==
             row_count(brain, "tags") - tags_before);

    const auto extract_before = row_count(brain, "facts");
    const auto extract_report = run_phase(brain, "extract_facts", false);
    QB_CHECK(extract_report.status == "ok");
    QB_CHECK(phase(extract_report, "extract_facts").mutations ==
             row_count(brain, "facts") - extract_before);

    const auto consolidate_before = row_count(brain, "facts");
    const auto consolidate_report = run_phase(brain, "consolidate", false);
    QB_CHECK(consolidate_report.status == "ok");
    QB_CHECK(phase(consolidate_report, "consolidate").mutations ==
             row_count(brain, "facts") - consolidate_before);

    {
      ScopedEnv mock("QBRAIN_EMBED_MOCK", "1");
      const auto embed_report = run_phase(brain, "embed", false);
      QB_CHECK(embed_report.status == "ok");
      QB_CHECK(phase(embed_report, "embed").mutations == 2);
      const auto completed1 = qbrain::jobs::get_job(brain, job1);
      const auto completed2 = qbrain::jobs::get_job(brain, job2);
      QB_CHECK(completed1 && completed1->status == "completed");
      QB_CHECK(completed2 && completed2->status == "completed");
      const auto chunks1 = brain.get_chunks(linked.id);
      const auto chunks2 = brain.get_chunks(target.id);
      QB_CHECK(!chunks1.empty() && !chunks1[0].embedding.empty());
      QB_CHECK(!chunks2.empty() && !chunks2[0].embedding.empty());
      QB_CHECK(chunks1[0].embedding == chunks2[0].embedding);

      const auto apply_page =
          put_indexed(brain, "default-apply-embed", "Default Apply", "default apply embed");
      const auto apply_job = qbrain::jobs::submit_job(
          brain, "embed", json({{"page_id", apply_page.id}}).dump());
      qbrain::cycle::DreamOpts apply_all;
      apply_all.dry_run = false;
      const auto apply_report = qbrain::cycle::run_dream(brain, apply_all);
      QB_CHECK(apply_report.status == "ok");
      QB_CHECK(apply_report.phases.size() == 5);
      QB_CHECK(phase(apply_report, "purge").status == "skipped");
      QB_CHECK(phase(apply_report, "purge").mutations == 0);
      QB_CHECK(phase(apply_report, "embed").mutations > 0);
      QB_CHECK(page_row_exists(brain, protected_deleted.id) == 1);
      const auto completed = qbrain::jobs::get_job(brain, apply_job);
      QB_CHECK(completed && completed->status == "completed");
      const auto report_json = json::parse(qbrain::cycle::report_to_json(apply_report));
      QB_CHECK(report_json["phases"][0].contains("mutations"));
    }

    brain.close();
  }

  {
    qbrain::Brain brain("n12-dream-purge");
    brain.open_at(qbrain::util::path_to_utf8(root / "purge.db"));

    const auto old_deleted = put_indexed(brain, "old-deleted", "Old Deleted", "old body");
    const auto recent_deleted =
        put_indexed(brain, "recent-deleted", "Recent Deleted", "recent body");
    const auto old_active = put_indexed(brain, "old-active", "Old Active", "active body");
    const auto same_slug_other =
        put_indexed(brain, "old-deleted", "Other Source", "other body", "other");
    const auto other_target =
        put_indexed(brain, "other-target", "Other Target", "other target", "other");

    brain.add_tag(old_deleted.slug, "purge-me");
    brain.add_tag(recent_deleted.slug, "keep-me");
    brain.add_fact(old_deleted.slug, "state", "purge", old_deleted.id);
    brain.add_fact(recent_deleted.slug, "state", "keep", recent_deleted.id);
    brain.add_fact(old_active.slug, "state", "active", old_active.id);
    brain.add_fact("unrelated", "state", "keep", 0);
    add_version(brain, old_deleted);
    add_version(brain, recent_deleted);

    qbrain::Link outbound;
    outbound.from_slug = old_deleted.slug;
    outbound.to_slug = old_active.slug;
    brain.add_link(outbound);
    qbrain::Link inbound;
    inbound.from_slug = old_active.slug;
    inbound.to_slug = old_deleted.slug;
    brain.add_link(inbound);
    qbrain::Link recent_link;
    recent_link.from_slug = recent_deleted.slug;
    recent_link.to_slug = old_active.slug;
    brain.add_link(recent_link);
    qbrain::Link other_link;
    other_link.source_id = "other";
    other_link.from_slug = same_slug_other.slug;
    other_link.to_slug = other_target.slug;
    brain.add_link(other_link);

    set_deleted_age(brain, old_deleted.id, 73);
    set_deleted_age(brain, recent_deleted.id, 71);
    brain.db().exec("UPDATE pages SET updated_at=datetime('now','-9000 hours') "
                    "WHERE slug='old-active' AND source_id='default';");

    const auto unrelated_job = qbrain::jobs::submit_job(brain, "noop", "{}");
    QB_CHECK(unrelated_job > 0);
    const auto jobs_before = row_count(brain, "jobs");
    const int64_t expected_mutations =
        1 + page_ref_count(brain, "content_chunks", old_deleted.id) +
        page_ref_count(brain, "tags", old_deleted.id) +
        page_ref_count(brain, "page_versions", old_deleted.id) +
        page_ref_count(brain, "facts", old_deleted.id) +
        link_ref_count(brain, "default", old_deleted.slug);

    const auto purge_before_snapshot = database_snapshot(brain);
    const auto purge = run_phase(brain, "purge", false);
    QB_CHECK(purge.status == "ok");
    QB_CHECK(phase(purge, "purge").count == 1);
    QB_CHECK(phase(purge, "purge").mutations == expected_mutations);
    QB_CHECK(phase(purge, "purge").summary.find("retention_hours=72") != std::string::npos);
    QB_CHECK(page_row_exists(brain, old_deleted.id) == 0);
    QB_CHECK(page_ref_count(brain, "content_chunks", old_deleted.id) == 0);
    QB_CHECK(page_ref_count(brain, "tags", old_deleted.id) == 0);
    QB_CHECK(page_ref_count(brain, "page_versions", old_deleted.id) == 0);
    QB_CHECK(page_ref_count(brain, "facts", old_deleted.id) == 0);
    QB_CHECK(link_ref_count(brain, "default", old_deleted.slug) == 0);
    QB_CHECK(page_row_exists(brain, recent_deleted.id) == 1);
    QB_CHECK(page_row_exists(brain, old_active.id) == 1);
    QB_CHECK(page_row_exists(brain, same_slug_other.id) == 1);
    QB_CHECK(link_ref_count(brain, "other", same_slug_other.slug) == 1);
    QB_CHECK(row_count(brain, "jobs") == jobs_before);
    QB_CHECK(page_ref_count(brain, "facts", recent_deleted.id) == 1);
    QB_CHECK(page_ref_count(brain, "facts", old_active.id) == 1);
    QB_CHECK(scalar(brain, "SELECT COUNT(*) FROM facts WHERE page_id IS NULL") == 1);
    const auto purge_after_snapshot = database_snapshot(brain);
    std::cout << "[INFO] dream_purge_snapshot_before_sha256="
              << qbrain::util::sha256_hex(purge_before_snapshot)
              << " after_sha256=" << qbrain::util::sha256_hex(purge_after_snapshot)
              << " mutations=" << phase(purge, "purge").mutations << "\n"
              << std::flush;

    const auto invalid_target =
        put_indexed(brain, "invalid-retention", "Invalid Retention", "stay on failure");
    set_deleted_age(brain, invalid_target.id, 9000);
    for (const auto& invalid : {"abc", "1.5", "999999999999999999999999999999",
                                "-999999999999999999999999999999"}) {
      const auto before = database_snapshot(brain);
      const auto report = run_phase(brain, "purge", false, invalid);
      QB_CHECK(report.status == "failed");
      QB_CHECK(phase(report, "purge").mutations == 0);
      QB_CHECK(database_snapshot(brain) == before);
    }

    const auto clamp_zero = put_indexed(brain, "clamp-zero", "Clamp Zero", "delete at one hour");
    set_deleted_age(brain, clamp_zero.id, 2);
    const auto zero_report = run_phase(brain, "purge", false, "0");
    QB_CHECK(zero_report.status == "ok");
    QB_CHECK(phase(zero_report, "purge").summary.find("retention_hours=1") != std::string::npos);
    QB_CHECK(page_row_exists(brain, clamp_zero.id) == 0);

    const auto clamp_negative =
        put_indexed(brain, "clamp-negative", "Clamp Negative", "delete at one hour");
    set_deleted_age(brain, clamp_negative.id, 2);
    const auto negative_report = run_phase(brain, "purge", false, "-5");
    QB_CHECK(negative_report.status == "ok");
    QB_CHECK(phase(negative_report, "purge").summary.find("retention_hours=1") !=
             std::string::npos);
    QB_CHECK(page_row_exists(brain, clamp_negative.id) == 0);

    const auto exact_one = put_indexed(brain, "exact-one", "Exact One", "one-hour input");
    set_deleted_age(brain, exact_one.id, 2);
    QB_CHECK(run_phase(brain, "purge", false, "1").status == "ok");
    QB_CHECK(page_row_exists(brain, exact_one.id) == 0);

    const auto exact_72 = put_indexed(brain, "exact-72", "Exact 72", "72-hour input");
    set_deleted_age(brain, exact_72.id, 73);
    QB_CHECK(run_phase(brain, "purge", false, "72").status == "ok");
    QB_CHECK(page_row_exists(brain, exact_72.id) == 0);

    const auto ancient = put_indexed(brain, "ancient", "Ancient", "older than a year");
    const auto moderate = put_indexed(brain, "moderate", "Moderate", "inside a year");
    set_deleted_age(brain, ancient.id, 9000);
    set_deleted_age(brain, moderate.id, 100);
    const auto max_report = run_phase(brain, "purge", false, "8761");
    QB_CHECK(max_report.status == "ok");
    QB_CHECK(phase(max_report, "purge").summary.find("retention_hours=8760") !=
             std::string::npos);
    QB_CHECK(page_row_exists(brain, ancient.id) == 0);
    QB_CHECK(page_row_exists(brain, moderate.id) == 1);

    const auto exact_max = put_indexed(brain, "exact-max", "Exact Max", "max input");
    set_deleted_age(brain, exact_max.id, 9000);
    QB_CHECK(run_phase(brain, "purge", false, "8760").status == "ok");
    QB_CHECK(page_row_exists(brain, exact_max.id) == 0);

    qbrain::ops::register_builtin_ops();
    const auto denied_target =
        put_indexed(brain, "mcp-denied", "MCP Denied", "deny must not mutate");
    set_deleted_age(brain, denied_target.id, 2);
    const auto denied_cancel_job =
        qbrain::jobs::submit_job(brain, "noop", R"({"deny":"cancel"})", "mcp-deny");
    QB_CHECK(denied_cancel_job > 0);
    const auto denied_before = database_snapshot(brain);
    qbrain::mcp::ServeOptions mcp_opts;
    mcp_opts.allow_write = false;
    const auto denied_request =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run_dream","arguments":{"apply":true,"phase":"purge","retention_hours":1}}})";
    const auto denied_response =
        json::parse(qbrain::mcp::handle_rpc_body(brain, mcp_opts, denied_request));
    QB_CHECK(denied_response["result"]["isError"] == true);
    QB_CHECK(database_snapshot(brain) == denied_before);
    QB_CHECK(page_row_exists(brain, denied_target.id) == 1);

    const auto denied_submit_request =
        R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"submit_job","arguments":{"type":"noop","payload_json":"{}"}}})";
    const auto denied_submit_response =
        json::parse(qbrain::mcp::handle_rpc_body(brain, mcp_opts, denied_submit_request));
    QB_CHECK(denied_submit_response["result"]["isError"] == true);
    QB_CHECK(database_snapshot(brain) == denied_before);

    const auto denied_cancel_request =
        std::string(
            R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"cancel_job","arguments":{"id":)") +
        std::to_string(denied_cancel_job) + R"(}}})";
    const auto denied_cancel_response =
        json::parse(qbrain::mcp::handle_rpc_body(brain, mcp_opts, denied_cancel_request));
    QB_CHECK(denied_cancel_response["result"]["isError"] == true);
    QB_CHECK(database_snapshot(brain) == denied_before);
    const auto still_waiting = qbrain::jobs::get_job(brain, denied_cancel_job);
    QB_CHECK(still_waiting && still_waiting->status == "waiting");
    std::cout << "[INFO] mcp_write_deny_snapshot_sha256="
              << qbrain::util::sha256_hex(denied_before)
              << " ops=submit_job,cancel_job,run_dream unchanged=pass\n"
              << std::flush;

    const auto tools_response = json::parse(qbrain::mcp::handle_rpc_body(
        brain, mcp_opts,
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})"));
    bool saw_retention_schema = false;
    for (const auto& tool : tools_response["result"]["tools"]) {
      if (tool["name"] == "run_dream") {
        saw_retention_schema = tool["inputSchema"]["properties"].contains("retention_hours");
      }
    }
    QB_CHECK(saw_retention_schema);

    mcp_opts.allow_write = true;
    const auto invalid_mcp_request =
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run_dream","arguments":{"apply":true,"phase":"purge","retention_hours":"999999999999999999999999999"}}})";
    const auto invalid_mcp_response =
        json::parse(qbrain::mcp::handle_rpc_body(brain, mcp_opts, invalid_mcp_request));
    QB_CHECK(invalid_mcp_response["result"]["isError"] == true);
    QB_CHECK(database_snapshot(brain) == denied_before);

    brain.close();
  }

  fs::remove_all(root);
}

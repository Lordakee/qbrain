#include "qbrain/jobs/minions.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/util/paths.hpp"
#include "wave3_test_support.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define QB_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond)) throw std::runtime_error(std::string("CHECK failed: ") + #cond); \
  } while (0)

namespace {

using qbrain::test_support::logical_snapshot;
using qbrain::test_support::scalar;
using qbrain::test_support::snapshot_sha256;

int count_pending_embed(qbrain::Brain& brain, int64_t page_id) {
  auto statement = brain.db().prepare(
      "SELECT COUNT(*) FROM jobs WHERE type='embed' "
      "AND status IN ('waiting','active','paused') AND payload_json=?");
  statement.bind_text(1, nlohmann::json({{"page_id", page_id}}).dump());
  return statement.step() ? static_cast<int>(statement.column_int(0)) : 0;
}

int deny_embed_job_insert(void*, int action, const char* table, const char*, const char*,
                          const char*) {
  return action == SQLITE_INSERT && table && std::string(table) == "jobs" ? SQLITE_DENY
                                                                     : SQLITE_OK;
}

bool is_sqlite_busy(const std::exception_ptr& error) {
  if (!error) return false;
  try {
    std::rethrow_exception(error);
  } catch (const std::exception& exception) {
    const std::string text = exception.what();
    return text.find("locked") != std::string::npos || text.find("busy") != std::string::npos;
  }
}

void require_non_state_job_fields(const qbrain::jobs::Job& before,
                                  const qbrain::jobs::Job& after) {
  QB_CHECK(after.id == before.id);
  QB_CHECK(after.queue == before.queue);
  QB_CHECK(after.type == before.type);
  QB_CHECK(after.payload_json == before.payload_json);
  QB_CHECK(after.result_json == before.result_json);
  QB_CHECK(after.error_text == before.error_text);
  QB_CHECK(after.priority == before.priority);
  QB_CHECK(after.attempts == before.attempts);
  QB_CHECK(after.created_at == before.created_at);
}

nlohmann::json require_structured_error(const qbrain::ops::OpResult& result,
                                        const std::string& code) {
  QB_CHECK(!result.ok && result.exit_code != 0);
  const auto parsed = nlohmann::json::parse(result.json);
  QB_CHECK(parsed.contains("error") && parsed["error"].is_object());
  QB_CHECK(parsed["error"]["code"] == code);
  return parsed["error"];
}

nlohmann::json require_progress_contract(qbrain::Brain& brain, int64_t job_id,
                                         bool remote = false) {
  const auto before = logical_snapshot(brain);
  auto result = qbrain::test_support::call_op(
      brain, "get_job_progress", {{"id", std::to_string(job_id)}}, remote, false);
  QB_CHECK(result.ok && result.exit_code == 0);
  const auto progress = nlohmann::json::parse(result.json);
  QB_CHECK(progress.is_object() && progress.size() == 6);
  for (const auto* key : {"id", "type", "status", "attempts", "lock_until", "error_text"}) {
    QB_CHECK(progress.contains(key));
  }
  QB_CHECK(progress["id"].is_number_integer());
  QB_CHECK(progress["type"].is_string());
  QB_CHECK(progress["status"].is_string());
  QB_CHECK(progress["attempts"].is_number_integer());
  QB_CHECK(progress["lock_until"].is_string());
  QB_CHECK(progress["error_text"].is_string());
  for (const auto* forbidden : {"payload_json", "result_json", "priority", "queue",
                                "lock_token", "provider", "model", "api_key"}) {
    QB_CHECK(!progress.contains(forbidden));
  }
  const auto job = qbrain::jobs::get_job(brain, job_id);
  QB_CHECK(job);
  QB_CHECK(progress["id"] == job->id);
  QB_CHECK(progress["type"] == job->type);
  QB_CHECK(progress["status"] == job->status);
  QB_CHECK(progress["attempts"] == job->attempts);
  QB_CHECK(progress["lock_until"] == job->lock_until);
  QB_CHECK(logical_snapshot(brain) == before);
  return progress;
}

nlohmann::json require_mcp_result_json(const nlohmann::json& response) {
  QB_CHECK(response.contains("result"));
  QB_CHECK(!response["result"].value("isError", true));
  for (auto it = response["result"]["content"].rbegin();
       it != response["result"]["content"].rend(); ++it) {
    if (it->value("type", "") != "text" || !it->contains("text") ||
        !(*it)["text"].is_string()) {
      continue;
    }
    auto parsed = nlohmann::json::parse((*it)["text"].get<std::string>(), nullptr, false);
    if (!parsed.is_discarded()) return parsed;
  }
  throw std::runtime_error("MCP result did not contain JSON content");
}

struct N14MatrixEvidence {
  std::string job_snapshot_hash;
  std::string status_snapshot_hash;
  std::string remediation_snapshot_hash;
  std::string remediation_rollback_hash;
  int concurrent_claim_winners = 0;
  int remediation_embed_delta = 0;
};

N14MatrixEvidence g_matrix_evidence;

void exercise_job_and_progress_matrix(const std::filesystem::path& root) {
  qbrain::Brain brain("n14-job-matrix");
  const auto db_path = root / "job-matrix.db";
  brain.open_at(qbrain::util::path_to_utf8(db_path));

  const auto waiting_id = qbrain::jobs::submit_job(
      brain, "matrix", R"({"case":"waiting","secret":"payload-only"})", "matrix-waiting", 17);
  auto waiting_before = qbrain::jobs::get_job(brain, waiting_id);
  QB_CHECK(waiting_before);
  const auto waiting_wrong_snapshot = logical_snapshot(brain);
  require_structured_error(qbrain::test_support::call_op(
                               brain, "resume_job", {{"id", std::to_string(waiting_id)}}),
                           "invalid_state");
  QB_CHECK(logical_snapshot(brain) == waiting_wrong_snapshot);
  QB_CHECK(qbrain::test_support::call_op(
               brain, "pause_job", {{"id", std::to_string(waiting_id)}})
               .ok);
  auto waiting_paused = qbrain::jobs::get_job(brain, waiting_id);
  QB_CHECK(waiting_paused && waiting_paused->status == "paused" &&
           waiting_paused->lock_token.empty() && waiting_paused->lock_until.empty());
  require_non_state_job_fields(*waiting_before, *waiting_paused);
  const auto repeated_pause_snapshot = logical_snapshot(brain);
  require_structured_error(qbrain::test_support::call_op(
                               brain, "pause_job", {{"id", std::to_string(waiting_id)}}),
                           "invalid_state");
  QB_CHECK(logical_snapshot(brain) == repeated_pause_snapshot);
  QB_CHECK(qbrain::test_support::call_op(
               brain, "resume_job", {{"id", std::to_string(waiting_id)}})
               .ok);
  auto waiting_resumed = qbrain::jobs::get_job(brain, waiting_id);
  QB_CHECK(waiting_resumed && waiting_resumed->status == "waiting");
  require_non_state_job_fields(*waiting_paused, *waiting_resumed);

  const auto active_id = qbrain::jobs::submit_job(
      brain, "matrix", R"({"case":"active"})", "matrix-active", 23);
  auto active_claim = qbrain::jobs::claim_job(brain, "old-active-token", 30000,
                                               "matrix-active");
  QB_CHECK(active_claim && active_claim->id == active_id && active_claim->attempts == 1);
  const auto active_wrong_snapshot = logical_snapshot(brain);
  require_structured_error(qbrain::test_support::call_op(
                               brain, "resume_job", {{"id", std::to_string(active_id)}}),
                           "invalid_state");
  QB_CHECK(logical_snapshot(brain) == active_wrong_snapshot);
  QB_CHECK(qbrain::test_support::call_op(
               brain, "pause_job", {{"id", std::to_string(active_id)}})
               .ok);
  auto active_paused = qbrain::jobs::get_job(brain, active_id);
  QB_CHECK(active_paused && active_paused->status == "paused" &&
           active_paused->lock_token.empty() && active_paused->lock_until.empty());
  require_non_state_job_fields(*active_claim, *active_paused);
  QB_CHECK(!qbrain::jobs::complete_job(brain, active_id, "old-active-token", "{}"));
  QB_CHECK(!qbrain::jobs::fail_job(brain, active_id, "old-active-token", "stale"));
  QB_CHECK(!qbrain::jobs::claim_job(brain, "paused-token", 30000, "matrix-active"));
  QB_CHECK(qbrain::test_support::call_op(
               brain, "resume_job", {{"id", std::to_string(active_id)}})
               .ok);
  auto fresh_claim =
      qbrain::jobs::claim_job(brain, "fresh-active-token", 30000, "matrix-active");
  QB_CHECK(fresh_claim && fresh_claim->id == active_id &&
           fresh_claim->lock_token == "fresh-active-token" &&
           fresh_claim->lock_token != "old-active-token" && fresh_claim->attempts == 2);

  const auto completed_id = qbrain::jobs::submit_job(
      brain, "matrix", R"({"case":"completed"})", "matrix-completed");
  auto completed_claim = qbrain::jobs::claim_job(brain, "completed-token", 30000,
                                                  "matrix-completed");
  QB_CHECK(completed_claim && qbrain::jobs::complete_job(
                                  brain, completed_id, "completed-token",
                                  R"({"result_secret":"must-not-appear"})"));
  const auto failed_id = qbrain::jobs::submit_job(
      brain, "matrix", R"({"case":"failed"})", "matrix-failed");
  auto failed_claim =
      qbrain::jobs::claim_job(brain, "failed-token", 30000, "matrix-failed");
  QB_CHECK(failed_claim &&
           qbrain::jobs::fail_job(brain, failed_id, "failed-token", "bounded benign failure"));
  const auto cancelled_id = qbrain::jobs::submit_job(
      brain, "matrix", R"({"case":"cancelled"})", "matrix-cancelled");
  QB_CHECK(qbrain::jobs::cancel_job(brain, cancelled_id));

  for (const auto terminal_id : {completed_id, failed_id, cancelled_id}) {
    const auto before = logical_snapshot(brain);
    require_structured_error(qbrain::test_support::call_op(
                                 brain, "pause_job", {{"id", std::to_string(terminal_id)}}),
                             "invalid_state");
    require_structured_error(qbrain::test_support::call_op(
                                 brain, "resume_job", {{"id", std::to_string(terminal_id)}}),
                             "invalid_state");
    QB_CHECK(logical_snapshot(brain) == before);
  }

  const auto unknown_snapshot = logical_snapshot(brain);
  require_structured_error(
      qbrain::test_support::call_op(brain, "pause_job", {{"id", "9223372036854775807"}}),
      "invalid_state");
  require_structured_error(
      qbrain::test_support::call_op(brain, "resume_job", {{"id", "9223372036854775807"}}),
      "invalid_state");
  require_structured_error(qbrain::test_support::call_op(
                               brain, "get_job_progress", {{"id", "9223372036854775807"}}),
                           "not_found");
  QB_CHECK(logical_snapshot(brain) == unknown_snapshot);

  for (const auto& invalid : std::vector<std::string>{"", "0", "-1", "1junk",
                                                       "9223372036854775808"}) {
    const auto before = logical_snapshot(brain);
    for (const auto* operation : {"pause_job", "resume_job", "get_job_progress"}) {
      require_structured_error(
          qbrain::test_support::call_op(brain, operation, {{"id", invalid}}),
          "invalid_argument");
    }
    QB_CHECK(logical_snapshot(brain) == before);
  }

  const auto paused_progress_id = qbrain::jobs::submit_job(
      brain, "progress", R"({"private":"paused"})", "progress-paused");
  QB_CHECK(qbrain::jobs::pause_job(brain, paused_progress_id));
  const auto waiting_progress_id = qbrain::jobs::submit_job(
      brain, "progress", R"({"private":"waiting"})", "progress-waiting");
  const auto active_progress_id = qbrain::jobs::submit_job(
      brain, "progress", R"({"private":"active"})", "progress-active");
  QB_CHECK(qbrain::jobs::claim_job(brain, "progress-active-token", 30000,
                                   "progress-active"));
  for (const auto id : {waiting_progress_id, active_progress_id, paused_progress_id,
                        failed_id, completed_id, cancelled_id}) {
    const auto progress = require_progress_contract(brain, id);
    if (id == failed_id) QB_CHECK(progress["error_text"] == "bounded benign failure");
  }
  (void)require_progress_contract(brain, waiting_progress_id, true);

  const auto concurrent_id = qbrain::jobs::submit_job(
      brain, "matrix", R"({"case":"concurrent"})", "matrix-concurrent");
  QB_CHECK(qbrain::jobs::pause_job(brain, concurrent_id));
  QB_CHECK(qbrain::jobs::resume_job(brain, concurrent_id));
  qbrain::Brain peer("n14-job-matrix-peer");
  peer.open_at(qbrain::util::path_to_utf8(db_path));
  std::atomic<int> ready{0};
  std::atomic<bool> start{false};
  std::optional<qbrain::jobs::Job> first_claim;
  std::optional<qbrain::jobs::Job> second_claim;
  std::exception_ptr first_error;
  std::exception_ptr second_error;
  auto claim = [&](qbrain::Brain& connection, const char* token,
                   std::optional<qbrain::jobs::Job>& result,
                   std::exception_ptr& error) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    try {
      result = qbrain::jobs::claim_job(connection, token, 30000, "matrix-concurrent");
    } catch (...) {
      error = std::current_exception();
    }
  };
  std::thread first(claim, std::ref(brain), "concurrent-a", std::ref(first_claim),
                    std::ref(first_error));
  std::thread second(claim, std::ref(peer), "concurrent-b", std::ref(second_claim),
                     std::ref(second_error));
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  first.join();
  second.join();
  if (first_error && !is_sqlite_busy(first_error)) std::rethrow_exception(first_error);
  if (second_error && !is_sqlite_busy(second_error)) std::rethrow_exception(second_error);
  g_matrix_evidence.concurrent_claim_winners =
      static_cast<int>(first_claim.has_value()) + static_cast<int>(second_claim.has_value());
  QB_CHECK(g_matrix_evidence.concurrent_claim_winners == 1);
  const auto concurrent_job = qbrain::jobs::get_job(brain, concurrent_id);
  QB_CHECK(concurrent_job && concurrent_job->status == "active" && concurrent_job->attempts == 1);

  qbrain::mcp::ServeOptions options;
  const auto mcp_before = logical_snapshot(brain);
  nlohmann::json progress_request = {
      {"jsonrpc", "2.0"}, {"id", 1401}, {"method", "tools/call"},
      {"params", {{"name", "get_job_progress"},
                  {"arguments", {{"id", waiting_progress_id}}}}}};
  auto progress_response = nlohmann::json::parse(
      qbrain::mcp::handle_rpc_body(brain, options, progress_request.dump()));
  auto mcp_progress = require_mcp_result_json(progress_response);
  QB_CHECK(mcp_progress["id"] == waiting_progress_id && mcp_progress.size() == 6);
  QB_CHECK(logical_snapshot(brain) == mcp_before);

  g_matrix_evidence.job_snapshot_hash = snapshot_sha256(logical_snapshot(brain));
  peer.close();
  brain.close();
}

void exercise_status_matrix(const std::filesystem::path& root) {
  qbrain::Brain selected("n14-status-selected");
  selected.open_at(qbrain::util::path_to_utf8(root / "status-selected.db"));
  auto live = qbrain::test_support::put_page(selected, "default", "status/live", "live");
  selected.replace_chunks(live.id, {"embedded", "missing"});
  auto chunks = selected.get_chunks(live.id);
  QB_CHECK(chunks.size() == 2);
  selected.update_chunk_embedding(chunks.front().id, {0.25f, 0.5f}, "n14-test-model");
  auto target = qbrain::test_support::put_page(selected, "default", "status/target", "target");
  auto deleted = qbrain::test_support::put_page(selected, "default", "status/deleted", "deleted");
  selected.replace_chunks(deleted.id, {"stored-after-delete"});
  QB_CHECK(selected.soft_delete(deleted.slug));
  qbrain::Link link;
  link.source_id = "default";
  link.from_slug = live.slug;
  link.to_slug = target.slug;
  link.link_type = "wiki";
  link.link_source = "n14-status";
  selected.add_link(link);

  (void)qbrain::jobs::submit_job(selected, "status", "{}", "status-waiting");
  const auto active_id =
      qbrain::jobs::submit_job(selected, "status", "{}", "status-active");
  QB_CHECK(qbrain::jobs::claim_job(selected, "status-active-token", 30000,
                                   "status-active"));
  const auto paused_id =
      qbrain::jobs::submit_job(selected, "status", "{}", "status-paused");
  QB_CHECK(qbrain::jobs::pause_job(selected, paused_id));
  const auto failed_id =
      qbrain::jobs::submit_job(selected, "status", "{}", "status-failed");
  QB_CHECK(qbrain::jobs::claim_job(selected, "status-failed-token", 30000,
                                   "status-failed"));
  QB_CHECK(qbrain::jobs::fail_job(selected, failed_id, "status-failed-token", "failed"));
  const auto completed_id =
      qbrain::jobs::submit_job(selected, "status", "{}", "status-completed");
  QB_CHECK(qbrain::jobs::claim_job(selected, "status-completed-token", 30000,
                                   "status-completed"));
  QB_CHECK(qbrain::jobs::complete_job(selected, completed_id, "status-completed-token"));
  const auto cancelled_id =
      qbrain::jobs::submit_job(selected, "status", "{}", "status-cancelled");
  QB_CHECK(qbrain::jobs::cancel_job(selected, cancelled_id));
  QB_CHECK(active_id > 0 && completed_id > 0 && cancelled_id > 0);

  const auto selected_before = logical_snapshot(selected);
  auto first = qbrain::test_support::call_op(selected, "get_status_snapshot", {}, true, false);
  auto second = qbrain::test_support::call_op(selected, "get_status_snapshot", {}, true, false);
  QB_CHECK(first.ok && second.ok && first.json == second.json);
  auto status = nlohmann::json::parse(first.json);
  QB_CHECK(status.is_object() && status.size() == 6);
  QB_CHECK(status["schema_version"] ==
           scalar(selected, "SELECT COALESCE(MAX(version),0) FROM schema_version"));
  QB_CHECK(status["pages"] ==
           scalar(selected, "SELECT COUNT(*) FROM pages WHERE deleted_at IS NULL"));
  QB_CHECK(status["chunks"] == scalar(selected, "SELECT COUNT(*) FROM content_chunks"));
  QB_CHECK(status["links"] == scalar(selected, "SELECT COUNT(*) FROM links"));
  QB_CHECK(status["embedded_chunks"] ==
           scalar(selected, "SELECT COUNT(*) FROM content_chunks WHERE embedding IS NOT NULL"));
  QB_CHECK(status["jobs"].is_object() && status["jobs"].size() == 4);
  for (const auto* state : {"waiting", "active", "failed", "paused"}) {
    auto statement = selected.db().prepare("SELECT COUNT(*) FROM jobs WHERE status=?");
    statement.bind_text(1, state);
    QB_CHECK(statement.step());
    QB_CHECK(status["jobs"][state] == statement.column_int(0));
  }
  QB_CHECK(logical_snapshot(selected) == selected_before);

  qbrain::mcp::ServeOptions options;
  nlohmann::json request = {{"jsonrpc", "2.0"},
                            {"id", 1402},
                            {"method", "tools/call"},
                            {"params", {{"name", "get_status_snapshot"},
                                         {"arguments", nlohmann::json::object()}}}};
  auto response = nlohmann::json::parse(
      qbrain::mcp::handle_rpc_body(selected, options, request.dump()));
  QB_CHECK(require_mcp_result_json(response) == status);
  QB_CHECK(logical_snapshot(selected) == selected_before);

  qbrain::Brain decoy("n14-status-decoy");
  decoy.open_at(qbrain::util::path_to_utf8(root / "status-decoy.db"));
  qbrain::test_support::put_page(decoy, "default", "status/decoy-only", "decoy");
  auto decoy_status = qbrain::test_support::call_op(decoy, "get_status_snapshot");
  QB_CHECK(decoy_status.ok);
  QB_CHECK(nlohmann::json::parse(decoy_status.json)["pages"] == 1);
  QB_CHECK(decoy_status.json != first.json);
  QB_CHECK(logical_snapshot(selected) == selected_before);

  qbrain::Brain damaged_schema("n14-status-damaged-schema");
  damaged_schema.open_at(qbrain::util::path_to_utf8(root / "damaged-schema.db"));
  damaged_schema.db().exec("DROP TABLE schema_version;");
  const auto damaged_before = logical_snapshot(damaged_schema);
  require_structured_error(
      qbrain::test_support::call_op(damaged_schema, "get_status_snapshot"),
      "database_error");
  auto doctor = qbrain::test_support::call_op(damaged_schema, "run_doctor");
  QB_CHECK(!doctor.ok);
  QB_CHECK(logical_snapshot(damaged_schema) == damaged_before);

  qbrain::Brain damaged_jobs("n14-status-damaged-jobs");
  damaged_jobs.open_at(qbrain::util::path_to_utf8(root / "damaged-jobs.db"));
  damaged_jobs.db().exec("DROP TABLE jobs;");
  require_structured_error(qbrain::test_support::call_op(
                               damaged_jobs, "get_job_progress", {{"id", "1"}}),
                           "database_error");
  nlohmann::json damaged_request = {
      {"jsonrpc", "2.0"}, {"id", 1403}, {"method", "tools/call"},
      {"params", {{"name", "get_job_progress"}, {"arguments", {{"id", 1}}}}}};
  auto damaged_response = nlohmann::json::parse(
      qbrain::mcp::handle_rpc_body(damaged_jobs, options, damaged_request.dump()));
  QB_CHECK(damaged_response.contains("result"));
  QB_CHECK(damaged_response["result"].value("isError", false));
  QB_CHECK(damaged_response.dump().find("database_error") != std::string::npos);
  QB_CHECK(!damaged_response.contains("error"));

  g_matrix_evidence.status_snapshot_hash = snapshot_sha256(selected_before);
  damaged_jobs.close();
  damaged_schema.close();
  decoy.close();
  selected.close();
}

void exercise_remediation_matrix(const std::filesystem::path& root) {
  qbrain::Brain leases("n14-remediation-leases");
  leases.open_at(qbrain::util::path_to_utf8(root / "remediation-leases.db"));
  leases.db().exec("DELETE FROM sources WHERE id='default'");
  QB_CHECK(scalar(leases, "SELECT COUNT(*) FROM sources WHERE id='default'") == 0);

  const auto expired_id =
      qbrain::jobs::submit_job(leases, "lease", "{}", "default");
  QB_CHECK(qbrain::jobs::claim_job(leases, "expired-token", 30000, "default"));
  leases.db().exec("UPDATE jobs SET lock_until='2000-01-01 00:00:00' WHERE id=" +
                   std::to_string(expired_id));
  const auto future_id =
      qbrain::jobs::submit_job(leases, "lease", "{}", "future-queue");
  QB_CHECK(qbrain::jobs::claim_job(leases, "future-token", 30000, "future-queue"));
  leases.db().exec("UPDATE jobs SET queue='default', lock_until='2999-01-01 00:00:00' WHERE id=" +
                   std::to_string(future_id));
  const auto other_id =
      qbrain::jobs::submit_job(leases, "lease", "{}", "other-queue");
  QB_CHECK(qbrain::jobs::claim_job(leases, "other-token", 30000, "other-queue"));
  leases.db().exec("UPDATE jobs SET lock_until='2000-01-01 00:00:00' WHERE id=" +
                   std::to_string(other_id));

  const auto expired_before = qbrain::jobs::get_job(leases, expired_id);
  const auto future_before = qbrain::jobs::get_job(leases, future_id);
  const auto other_before = qbrain::jobs::get_job(leases, other_id);
  QB_CHECK(expired_before && future_before && other_before);
  auto unavailable = leases.remediate(false);
  QB_CHECK(unavailable.default_source && unavailable.reclaimed == 1 &&
           unavailable.embed_jobs_enqueued == 0 && !unavailable.api_key_present);
  QB_CHECK(scalar(leases, "SELECT COUNT(*) FROM sources WHERE id='default'") == 1);
  auto expired_after = qbrain::jobs::get_job(leases, expired_id);
  auto future_after = qbrain::jobs::get_job(leases, future_id);
  auto other_after = qbrain::jobs::get_job(leases, other_id);
  QB_CHECK(expired_after && expired_after->status == "waiting" &&
           expired_after->attempts == expired_before->attempts + 1 &&
           expired_after->lock_token.empty() && expired_after->lock_until.empty());
  QB_CHECK(future_after && future_after->status == "active" &&
           future_after->attempts == future_before->attempts &&
           future_after->lock_token == "future-token");
  QB_CHECK(other_after && other_after->status == "active" &&
           other_after->attempts == other_before->attempts &&
           other_after->lock_token == "other-token");
  QB_CHECK(!qbrain::jobs::complete_job(leases, expired_id, "expired-token"));
  QB_CHECK(!qbrain::jobs::fail_job(leases, expired_id, "expired-token", "stale"));
  auto unavailable_repeat = leases.remediate(false);
  QB_CHECK(unavailable_repeat.reclaimed == 0 && unavailable_repeat.embed_jobs_enqueued == 0 &&
           !unavailable_repeat.api_key_present);
  QB_CHECK(qbrain::jobs::get_job(leases, expired_id)->attempts == expired_after->attempts);

  qbrain::Brain embeds("n14-remediation-embeds");
  embeds.open_at(qbrain::util::path_to_utf8(root / "remediation-embeds.db"));
  auto make_missing = [&](const std::string& slug, int chunks = 1) {
    auto page = qbrain::test_support::put_page(embeds, "default", slug, "body");
    std::vector<std::string> texts;
    for (int index = 0; index < chunks; ++index) texts.push_back("chunk-" + std::to_string(index));
    embeds.replace_chunks(page.id, texts);
    return page;
  };
  auto multi = make_missing("embed/multi", 3);
  auto pending_waiting = make_missing("embed/pending-waiting");
  (void)qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", pending_waiting.id}}).dump(),
      "embed-waiting");
  auto pending_active = make_missing("embed/pending-active");
  (void)qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", pending_active.id}}).dump(),
      "embed-active");
  QB_CHECK(qbrain::jobs::claim_job(embeds, "embed-active-token", 30000, "embed-active"));
  auto pending_paused = make_missing("embed/pending-paused");
  const auto paused_job = qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", pending_paused.id}}).dump(),
      "embed-paused");
  QB_CHECK(qbrain::jobs::pause_job(embeds, paused_job));
  auto terminal_failed = make_missing("embed/terminal-failed");
  const auto failed_job = qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", terminal_failed.id}}).dump(),
      "embed-failed");
  QB_CHECK(qbrain::jobs::claim_job(embeds, "embed-failed-token", 30000, "embed-failed"));
  QB_CHECK(qbrain::jobs::fail_job(embeds, failed_job, "embed-failed-token", "terminal"));
  auto terminal_completed = make_missing("embed/terminal-completed");
  const auto completed_job = qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", terminal_completed.id}}).dump(),
      "embed-completed");
  QB_CHECK(qbrain::jobs::claim_job(embeds, "embed-completed-token", 30000,
                                   "embed-completed"));
  QB_CHECK(qbrain::jobs::complete_job(embeds, completed_job, "embed-completed-token"));
  auto terminal_cancelled = make_missing("embed/terminal-cancelled");
  const auto cancelled_job = qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", terminal_cancelled.id}}).dump(),
      "embed-cancelled");
  QB_CHECK(qbrain::jobs::cancel_job(embeds, cancelled_job));
  auto malformed = make_missing("embed/malformed-payload");
  (void)qbrain::jobs::submit_job(
      embeds, "embed", nlohmann::json({{"page_id", std::to_string(malformed.id)}}).dump(),
      "embed-malformed");
  auto deleted = make_missing("embed/deleted");
  QB_CHECK(embeds.soft_delete(deleted.slug));
  auto already_embedded = make_missing("embed/already-embedded");
  auto embedded_chunks = embeds.get_chunks(already_embedded.id);
  QB_CHECK(embedded_chunks.size() == 1);
  embeds.update_chunk_embedding(embedded_chunks.front().id, {1.0f}, "n14-test-model");

  const auto pending_before = scalar(
      embeds, "SELECT COUNT(*) FROM jobs WHERE type='embed' AND status IN ('waiting','active','paused')");
  auto available = embeds.remediate(true);
  const auto pending_after = scalar(
      embeds, "SELECT COUNT(*) FROM jobs WHERE type='embed' AND status IN ('waiting','active','paused')");
  g_matrix_evidence.remediation_embed_delta = static_cast<int>(pending_after - pending_before);
  QB_CHECK(available.api_key_present && available.embed_jobs_enqueued == 5);
  QB_CHECK(g_matrix_evidence.remediation_embed_delta == available.embed_jobs_enqueued);
  QB_CHECK(count_pending_embed(embeds, multi.id) == 1);
  QB_CHECK(count_pending_embed(embeds, pending_waiting.id) == 1);
  QB_CHECK(count_pending_embed(embeds, pending_active.id) == 1);
  QB_CHECK(count_pending_embed(embeds, pending_paused.id) == 1);
  QB_CHECK(count_pending_embed(embeds, terminal_failed.id) == 1);
  QB_CHECK(count_pending_embed(embeds, terminal_completed.id) == 1);
  QB_CHECK(count_pending_embed(embeds, terminal_cancelled.id) == 1);
  QB_CHECK(count_pending_embed(embeds, malformed.id) == 1);
  QB_CHECK(count_pending_embed(embeds, deleted.id) == 0);
  QB_CHECK(count_pending_embed(embeds, already_embedded.id) == 0);
  auto available_repeat = embeds.remediate(true);
  QB_CHECK(available_repeat.embed_jobs_enqueued == 0);

  qbrain::Brain rollback("n14-remediation-rollback");
  rollback.open_at(qbrain::util::path_to_utf8(root / "remediation-rollback.db"));
  QB_CHECK(rollback.ensure_source("team_a"));
  auto rollback_page =
      qbrain::test_support::put_page(rollback, "team_a", "rollback/page", "body");
  rollback.replace_chunks(rollback_page.id, {"missing"});
  rollback.db().exec("DELETE FROM sources WHERE id='default'");
  const auto rollback_job = qbrain::jobs::submit_job(rollback, "lease", "{}", "default");
  QB_CHECK(qbrain::jobs::claim_job(rollback, "rollback-token", 30000, "default"));
  rollback.db().exec("UPDATE jobs SET lock_until='2000-01-01 00:00:00' WHERE id=" +
                     std::to_string(rollback_job));
  const auto rollback_before = logical_snapshot(rollback);
  sqlite3_set_authorizer(rollback.db().handle(), deny_embed_job_insert, nullptr);
  bool rolled_back = false;
  try {
    (void)rollback.remediate(true);
  } catch (...) {
    rolled_back = true;
  }
  sqlite3_set_authorizer(rollback.db().handle(), nullptr, nullptr);
  QB_CHECK(rolled_back && logical_snapshot(rollback) == rollback_before);
  QB_CHECK(scalar(rollback, "SELECT COUNT(*) FROM sources WHERE id='default'") == 0);
  auto rollback_job_after = qbrain::jobs::get_job(rollback, rollback_job);
  QB_CHECK(rollback_job_after && rollback_job_after->status == "active" &&
           rollback_job_after->lock_token == "rollback-token");

  qbrain::Brain allowed("n14-remediation-remote-allowed");
  allowed.open_at(qbrain::util::path_to_utf8(root / "remote-allowed.db"));
  const auto allowed_job = qbrain::jobs::submit_job(
      allowed, "allowed", R"({"preserve":true})", "allowed-queue", 31);
  auto allowed_before = qbrain::jobs::get_job(allowed, allowed_job);
  QB_CHECK(allowed_before);
  auto allowed_pause = qbrain::test_support::call_op(
      allowed, "pause_job", {{"id", std::to_string(allowed_job)}}, true, true);
  QB_CHECK(allowed_pause.ok);
  auto allowed_paused = qbrain::jobs::get_job(allowed, allowed_job);
  QB_CHECK(allowed_paused && allowed_paused->status == "paused");
  require_non_state_job_fields(*allowed_before, *allowed_paused);
  auto allowed_resume = qbrain::test_support::call_op(
      allowed, "resume_job", {{"id", std::to_string(allowed_job)}}, true, true);
  QB_CHECK(allowed_resume.ok);
  auto allowed_resumed = qbrain::jobs::get_job(allowed, allowed_job);
  QB_CHECK(allowed_resumed && allowed_resumed->status == "waiting");
  require_non_state_job_fields(*allowed_paused, *allowed_resumed);
  allowed.db().exec("DELETE FROM sources WHERE id='default'");
  QB_CHECK(qbrain::test_support::call_op(allowed, "doctor_remediate", {}, true, true).ok);
  QB_CHECK(scalar(allowed, "SELECT COUNT(*) FROM sources WHERE id='default'") == 1);

  g_matrix_evidence.remediation_snapshot_hash = snapshot_sha256(logical_snapshot(embeds));
  g_matrix_evidence.remediation_rollback_hash = snapshot_sha256(rollback_before);
  allowed.close();
  rollback.close();
  embeds.close();
  leases.close();
}

}  // namespace

void test_n14() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "qbrain_n14_test";
  fs::remove_all(root);
  fs::create_directories(root);

  qbrain::ops::register_builtin_ops();
  exercise_job_and_progress_matrix(root);
  exercise_status_matrix(root);
  exercise_remediation_matrix(root);
  qbrain::Brain brain("n14-primary");
  brain.open_at(qbrain::util::path_to_utf8(root / "primary.db"));

  const auto waiting_id = qbrain::jobs::submit_job(brain, "embed", R"({"page_id":7})");
  const auto before_bad_id = logical_snapshot(brain);
  auto bad_id = qbrain::test_support::call_op(brain, "pause_job", {{"id", "1junk"}});
  QB_CHECK(!bad_id.ok);
  QB_CHECK(logical_snapshot(brain) == before_bad_id);
  QB_CHECK(!qbrain::test_support::call_op(brain, "resume_job", {{"id", "0"}}).ok);
  QB_CHECK(!qbrain::test_support::call_op(
                brain, "get_job_progress", {{"id", "9223372036854775808"}})
                .ok);

  QB_CHECK(qbrain::jobs::pause_job(brain, waiting_id));
  auto paused = qbrain::jobs::get_job(brain, waiting_id);
  QB_CHECK(paused && paused->status == "paused" && paused->lock_token.empty());
  QB_CHECK(qbrain::jobs::resume_job(brain, waiting_id));
  auto claimed = qbrain::jobs::claim_job(brain, "n14-fence", 30000);
  QB_CHECK(claimed && claimed->id == waiting_id);
  QB_CHECK(qbrain::jobs::pause_job(brain, waiting_id));
  QB_CHECK(!qbrain::jobs::complete_job(brain, waiting_id, "n14-fence", "{}"));
  QB_CHECK(!qbrain::jobs::fail_job(brain, waiting_id, "n14-fence", "stale"));
  QB_CHECK(qbrain::jobs::resume_job(brain, waiting_id));

  const auto failed_id =
      qbrain::jobs::submit_job(brain, "embed", R"({"page_id":8})", "n14-redaction");
  auto failed_claim = qbrain::jobs::claim_job(brain, "n14-redaction", 30000, "n14-redaction");
  QB_CHECK(failed_claim && failed_claim->id == failed_id);
  QB_CHECK(qbrain::jobs::fail_job(brain, failed_id, "n14-redaction",
                                   "Authorization: Bearer credential-value"));
  auto progress = qbrain::jobs::get_job_progress(brain, failed_id);
  QB_CHECK(progress && progress->error_text == "[redacted]");
  auto progress_result =
      qbrain::test_support::call_op(brain, "get_job_progress", {{"id", std::to_string(failed_id)}});
  QB_CHECK(progress_result.ok);
  auto progress_json = nlohmann::json::parse(progress_result.json);
  QB_CHECK(progress_json.size() == 6 && progress_json["error_text"] == "[redacted]");
  QB_CHECK(progress_result.json.find("payload_json") == std::string::npos);

  const auto bare_key_id =
      qbrain::jobs::submit_job(brain, "embed", R"({"page_id":9})", "n14-bare-key");
  auto bare_key_claim = qbrain::jobs::claim_job(brain, "n14-bare-key", 30000, "n14-bare-key");
  QB_CHECK(bare_key_claim && bare_key_claim->id == bare_key_id);
  QB_CHECK(qbrain::jobs::fail_job(brain, bare_key_id, "n14-bare-key",
                                  "request failed sk-proj-0123456789abcdef"));
  auto bare_key_progress = qbrain::jobs::get_job_progress(brain, bare_key_id);
  QB_CHECK(bare_key_progress && bare_key_progress->error_text == "[redacted]");

  const auto utf8_id =
      qbrain::jobs::submit_job(brain, "embed", R"({"page_id":11})", "n14-utf8");
  auto utf8_claim = qbrain::jobs::claim_job(brain, "n14-utf8", 30000, "n14-utf8");
  QB_CHECK(utf8_claim && utf8_claim->id == utf8_id);
  std::string utf8_error(499, 'a');
  utf8_error += "\xC3\xA9";
  QB_CHECK(qbrain::jobs::fail_job(brain, utf8_id, "n14-utf8", utf8_error));
  auto utf8_progress = qbrain::jobs::get_job_progress(brain, utf8_id);
  QB_CHECK(utf8_progress && utf8_progress->error_text == std::string(499, 'a'));
  auto utf8_result = qbrain::test_support::call_op(
      brain, "get_job_progress", {{"id", std::to_string(utf8_id)}});
  QB_CHECK(utf8_result.ok);
  QB_CHECK(nlohmann::json::parse(utf8_result.json)["error_text"] == std::string(499, 'a'));

  const auto local_snapshot = logical_snapshot(brain);
  auto status = qbrain::test_support::call_op(brain, "get_status_snapshot");
  QB_CHECK(status.ok);
  auto status_json = nlohmann::json::parse(status.json);
  QB_CHECK(status_json.contains("jobs") && status_json["jobs"].size() == 4);
  QB_CHECK(status_json["pages"] == scalar(brain, "SELECT COUNT(*) FROM pages WHERE deleted_at IS NULL"));
  QB_CHECK(logical_snapshot(brain) == local_snapshot);

  for (const auto& name : {"pause_job", "resume_job", "doctor_remediate"}) {
    const auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation && operation->scope == qbrain::ops::Scope::Write && operation->local_only);
  }
  for (const auto& name : {"get_job_progress", "get_status_snapshot"}) {
    const auto* operation = qbrain::ops::global_registry().find(name);
    QB_CHECK(operation && operation->scope == qbrain::ops::Scope::Read && !operation->local_only);
  }
  const auto denied_snapshot = logical_snapshot(brain);
  QB_CHECK(!qbrain::test_support::call_op(
                brain, "pause_job", {{"id", std::to_string(waiting_id)}}, true, false)
                .ok);
  QB_CHECK(!qbrain::test_support::call_op(
                brain, "resume_job", {{"id", std::to_string(waiting_id)}}, true, false)
                .ok);
  QB_CHECK(!qbrain::test_support::call_op(brain, "doctor_remediate", {}, true, false).ok);
  QB_CHECK(logical_snapshot(brain) == denied_snapshot);

  qbrain::mcp::ServeOptions mcp_options;
  auto mcp_list = nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
      brain, mcp_options, R"({"jsonrpc":"2.0","id":90,"method":"tools/list","params":{}})"));
  QB_CHECK(std::any_of(mcp_list["result"]["tools"].begin(), mcp_list["result"]["tools"].end(),
                       [](const auto& tool) { return tool.value("name", "") == "pause_job"; }));
  auto mcp_deny = nlohmann::json::parse(qbrain::mcp::handle_rpc_body(
      brain, mcp_options,
      R"({"jsonrpc":"2.0","id":91,"method":"tools/call","params":{"name":"pause_job","arguments":{"id":"1"}}})"));
  QB_CHECK(mcp_deny["result"]["isError"] == true);

  auto live = qbrain::test_support::put_page(brain, "default", "n14/live", "live body");
  brain.replace_chunks(live.id, {"missing embedding"});
  auto deleted = qbrain::test_support::put_page(brain, "default", "n14/deleted", "gone body");
  brain.replace_chunks(deleted.id, {"deleted missing embedding"});
  QB_CHECK(brain.soft_delete("n14/deleted"));
  QB_CHECK(live.id == 1 && deleted.id == 2);
  for (int page_id = 3; page_id < 10; ++page_id) {
    auto filler = qbrain::test_support::put_page(
        brain, "default", "n14/filler-" + std::to_string(page_id), "filler");
    QB_CHECK(filler.id == page_id);
  }
  auto page_ten =
      qbrain::test_support::put_page(brain, "default", "n14/page-ten", "page ten body");
  QB_CHECK(page_ten.id == 10);
  brain.replace_chunks(page_ten.id, {"page ten missing embedding"});
  (void)qbrain::jobs::submit_job(
      brain, "embed", nlohmann::json({{"page_id", page_ten.id}}).dump(), "n14-page-ten");
  brain.set_embedding_available_for_test(true);
  auto remediation = brain.remediate();
  QB_CHECK(remediation.default_source && remediation.api_key_present);
  QB_CHECK(remediation.embed_jobs_enqueued == 1);
  QB_CHECK(count_pending_embed(brain, live.id) == 1);
  QB_CHECK(count_pending_embed(brain, deleted.id) == 0);
  QB_CHECK(count_pending_embed(brain, page_ten.id) == 1);
  auto repeat_remediation = brain.remediate();
  QB_CHECK(repeat_remediation.embed_jobs_enqueued == 0);
  QB_CHECK(count_pending_embed(brain, live.id) == 1);

  auto rollback_page = qbrain::test_support::put_page(brain, "default", "n14/rollback", "body");
  brain.replace_chunks(rollback_page.id, {"must roll back"});
  const auto rollback_before = logical_snapshot(brain);
  sqlite3_set_authorizer(brain.db().handle(), deny_embed_job_insert, nullptr);
  bool rolled_back = false;
  try {
    (void)brain.remediate();
  } catch (...) {
    rolled_back = true;
  }
  sqlite3_set_authorizer(brain.db().handle(), nullptr, nullptr);
  QB_CHECK(rolled_back);
  QB_CHECK(logical_snapshot(brain) == rollback_before);

  auto race_page = qbrain::test_support::put_page(brain, "default", "n14/race", "body");
  brain.replace_chunks(race_page.id, {"race missing embedding"});
  qbrain::Brain second_connection("n14-secondary-connection");
  second_connection.open_at(qbrain::util::path_to_utf8(root / "primary.db"));
  second_connection.set_embedding_available_for_test(true);
  std::atomic<int> ready{0};
  std::atomic<bool> start{false};
  std::exception_ptr first_error;
  std::exception_ptr second_error;
  auto run_remediate = [&](qbrain::Brain& connection, std::exception_ptr& error) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    try {
      (void)connection.remediate();
    } catch (...) {
      error = std::current_exception();
    }
  };
  std::thread first(run_remediate, std::ref(brain), std::ref(first_error));
  std::thread second(run_remediate, std::ref(second_connection), std::ref(second_error));
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  first.join();
  second.join();
  if (first_error && !is_sqlite_busy(first_error)) std::rethrow_exception(first_error);
  if (second_error && !is_sqlite_busy(second_error)) std::rethrow_exception(second_error);
  const int concurrent_pending = count_pending_embed(brain, race_page.id);
  QB_CHECK(concurrent_pending <= 1);

  qbrain::Brain damaged("n14-damaged");
  damaged.open_at(qbrain::util::path_to_utf8(root / "damaged.db"));
  damaged.db().exec("DROP TABLE schema_version;");
  auto damaged_status = qbrain::test_support::call_op(damaged, "get_status_snapshot");
  QB_CHECK(!damaged_status.ok);
  QB_CHECK(nlohmann::json::parse(damaged_status.json)["error"]["code"] == "database_error");

  std::cout << "[INFO] n14 job_fence=pass progress_redaction=pass status_snapshot=pass "
            << "snapshot_schema=pass snapshot_matrix=pass "
            << "state_matrix=pass progress_matrix=pass selected_brain=pass "
            << "remediation_lease_matrix=pass remediation_embed_matrix=pass "
            << "allowed_remote_writes=pass job_matrix_snapshot_sha256="
            << g_matrix_evidence.job_snapshot_hash << " status_matrix_snapshot_sha256="
            << g_matrix_evidence.status_snapshot_hash << " remediation_snapshot_sha256="
            << g_matrix_evidence.remediation_snapshot_hash
            << " remediation_matrix_rollback_sha256="
            << g_matrix_evidence.remediation_rollback_hash
            << " concurrent_claim_winners=" << g_matrix_evidence.concurrent_claim_winners
            << " remediation_embed_delta=" << g_matrix_evidence.remediation_embed_delta << ' '
            << "mcp_deny_snapshot_sha256=" << snapshot_sha256(denied_snapshot) << ' '
            << "mcp_rpc=pass page_id_exact_dedup=pass remediation_idempotent=pass rollback_snapshot_sha256="
            << snapshot_sha256(rollback_before) << " concurrent_pending=" << concurrent_pending
            << " damaged_status=database_error\n";

  damaged.close();
  second_connection.close();
  brain.close();
  fs::remove_all(root);
}

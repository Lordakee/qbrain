// tests/test_n34.cpp — N34 bounded parent/child minion hierarchy tests.
// Sections are owned by parallel subagents; keep them separated:
//   // --- n34-b: fence/aggregate-atomicity ---     (subagent B: D3 guard + fence-unchanged)
//   // --- n34-b: hierarchy acceptance matrix ---   (subagent B: D5 matrix over N34-A lifecycle)
// N34-A owns the lifecycle functions in src/qbrain/jobs/minions.cpp
// (spawn/aggregate/cancel/retry) and storage/migrate.cpp v13; this file
// drives them through the plan contract and asserts the D3 concurrency
// properties on top of the landed API.
//
// All brains live in temp directories with %LOCALAPPDATA% redirected so
// nothing touches %LOCALAPPDATA%\Qbrain.

#include "qbrain/core/brain.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/storage/database.hpp"
#include "qbrain/util/paths.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

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

namespace {

// Redirects an environment variable for the lifetime of the object
// (MSVC _putenv_s semantics, same pattern as tests/test_n31.cpp).
class N34ScopedEnv {
 public:
  N34ScopedEnv(const char* name, const std::string& value) : name_(name) {
    if (const char* previous = std::getenv(name)) previous_ = previous;
    if (_putenv_s(name, value.c_str()) != 0) {
      throw std::runtime_error("failed to set environment variable");
    }
  }
  ~N34ScopedEnv() {
    _putenv_s(name_, previous_ ? previous_->c_str() : "");
  }
  N34ScopedEnv(const N34ScopedEnv&) = delete;
  N34ScopedEnv& operator=(const N34ScopedEnv&) = delete;

 private:
  const char* name_;
  std::optional<std::string> previous_;
};

std::filesystem::path n34_fresh_dir(const char* leaf) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / leaf;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

bool n34_is_busy(const std::exception& e) {
  const std::string message = e.what();
  return message.find("locked") != std::string::npos ||
         message.find("busy") != std::string::npos;
}

// Two connections write the same WAL database with no busy_timeout, so a
// loser observes SQLITE_BUSY/locked. Busy is a valid transient loser state
// (see tests/test_minions.cpp): retry the SAME guarded statement until it
// commits; the token fence / aggregation fence decides the winner.
template <typename Fn>
auto n34_retry_on_busy(Fn&& fn, int attempts = 400) -> decltype(fn()) {
  for (int i = 0;; ++i) {
    try {
      return fn();
    } catch (const std::exception& e) {
      if (i + 1 >= attempts || !n34_is_busy(e)) throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(1 + i / 8));
    }
  }
}

// Total fence rows in the brain.
int64_t n34_fence_rows(qbrain::Brain& brain) {
  // No fence table yet == zero fence rows (legacy paths never create it).
  {
    auto exists = brain.db().prepare(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND "
        "name='job_aggregation_fence' LIMIT 1");
    if (!exists.step()) return 0;
  }
  auto st = brain.db().prepare("SELECT COUNT(*) FROM job_aggregation_fence");
  if (!st.step()) return 0;
  return st.column_int(0);
}

// Fence rows for one parent id (any value, including 0/negatives: exact match).
int64_t n34_fence_rows(qbrain::Brain& brain, int64_t parent_id) {
  {
    auto exists = brain.db().prepare(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND "
        "name='job_aggregation_fence' LIMIT 1");
    if (!exists.step()) return 0;
  }
  auto st = brain.db().prepare(
      "SELECT COUNT(*) FROM job_aggregation_fence WHERE parent_id=?");
  st.bind_int(1, parent_id);
  if (!st.step()) return 0;
  return st.column_int(0);
}

// Windows: antivirus/WAL-sidecar timing can briefly hold a temp file after
// close. Cleaning the test's own temp directory must never fail assertions
// that already passed: retry briefly, then leave leftovers to the OS.
void n34_best_effort_remove(const std::filesystem::path& dir) {
  for (int attempt = 0; attempt < 12; ++attempt) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (!ec) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
}

int64_t n34_scalar(qbrain::Brain& brain, const std::string& sql) {
  auto st = brain.db().prepare(sql);
  if (!st.step()) return 0;
  return st.column_int(0);
}

// Raw hierarchy-column readers (independent of any struct field additions).
int64_t n34_parent_id_of(qbrain::Brain& brain, int64_t job_id) {
  auto st = brain.db().prepare("SELECT COALESCE(parent_id,0) FROM jobs WHERE id=?");
  st.bind_int(1, job_id);
  if (!st.step()) return -1;
  return st.column_int(0);
}

int64_t n34_depth_of(qbrain::Brain& brain, int64_t job_id) {
  auto st = brain.db().prepare("SELECT COALESCE(depth,0) FROM jobs WHERE id=?");
  st.bind_int(1, job_id);
  if (!st.step()) return -1;
  return st.column_int(0);
}

std::string n34_status_of(qbrain::Brain& brain, int64_t job_id) {
  const auto job = qbrain::jobs::get_job(brain, job_id);
  if (!job) throw std::runtime_error("job missing: " + std::to_string(job_id));
  return job->status;
}

// Every child points at the parent, carries depth 1, and starts waiting.
bool n34_child_shape(qbrain::Brain& brain, int64_t parent_id,
                     const std::vector<int64_t>& children) {
  if (n34_parent_id_of(brain, parent_id) != 0) return false;
  if (n34_depth_of(brain, parent_id) != 0) return false;
  for (int64_t child : children) {
    if (n34_parent_id_of(brain, child) != parent_id) return false;
    if (n34_depth_of(brain, child) != 1) return false;
    if (n34_status_of(brain, child) != "waiting") return false;
  }
  return true;
}

// Happy-path spawn helper: a waiting parent (submitted via the untouched N12
// submit_job) that then spawns its children in one transaction.
struct N34SpawnOutcome {
  qbrain::jobs::JobOperationStatus status = qbrain::jobs::JobOperationStatus::invalid_argument;
  std::string reason;
  int64_t parent_id = 0;
  std::vector<int64_t> child_ids;
};

N34SpawnOutcome n34_spawn(qbrain::Brain& brain, const std::string& parent_type,
                          const std::vector<qbrain::jobs::ChildSpec>& children) {
  const int64_t parent = qbrain::jobs::submit_job(brain, parent_type, "{}", "default", 100);
  auto result = qbrain::jobs::spawn_children(brain, parent, children);
  N34SpawnOutcome out;
  out.status = result.status;
  out.reason = result.reason;
  out.parent_id = parent;
  out.child_ids = std::move(result.child_ids);
  return out;
}

}  // namespace

// --- n34-b: fence/aggregate-atomicity ---

void test_n34() {
  namespace fs = std::filesystem;
  using qbrain::Brain;
  using qbrain::jobs::claim_job;
  using qbrain::jobs::complete_job;
  using qbrain::jobs::get_job;
  using qbrain::jobs::submit_job;
  using qbrain::jobs::try_begin_aggregation;

  // ============================================================
  // (1) D3 point 3 — the N12 single-job token fence is unchanged
  // for legacy depth-0 jobs (no children anywhere near this brain
  // section; every claim/complete rule keeps its exact N12 shape).
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_fence_unchanged");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);

    Brain b("n34_fence_unchanged");
    b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));

    const int64_t id = submit_job(b, "extract_facts", R"({"slug":"n34","source_id":"default"})",
                                  "n34-fence", 100);
    QB_CHECK(id > 0);

    // Empty token cannot claim; non-empty token claims once.
    QB_CHECK(!claim_job(b, "", 30000, "n34-fence").has_value());
    const auto job = claim_job(b, "tok-n34-a", 30000, "n34-fence");
    QB_CHECK(job.has_value());
    QB_CHECK(job->id == id);
    QB_CHECK(job->status == "active");
    QB_CHECK(job->lock_token == "tok-n34-a");
    QB_CHECK(job->attempts == 1);
    QB_CHECK(!claim_job(b, "tok-n34-b", 30000, "n34-fence").has_value());

    // Wrong/empty tokens cannot complete; the fence stays intact.
    QB_CHECK(!complete_job(b, id, "tok-n34-b", "{}"));
    QB_CHECK(!complete_job(b, id, "", "{}"));
    const auto still = get_job(b, id);
    QB_CHECK(still.has_value() && still->status == "active" && still->lock_token == "tok-n34-a");

    // Matching token completes exactly once; terminal rows cannot re-complete.
    QB_CHECK(complete_job(b, id, "tok-n34-a", R"({"ok":1})"));
    QB_CHECK(!complete_job(b, id, "tok-n34-a", "{}"));
    const auto done = get_job(b, id);
    QB_CHECK(done.has_value());
    QB_CHECK(done->status == "completed");
    QB_CHECK(done->result_json == R"({"ok":1})");
    QB_CHECK(done->lock_token.empty());
    QB_CHECK(done->lock_until.empty());

    b.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (2) D3 point 2 — aggregate-once guard unit semantics:
  // try_begin_aggregation returns true EXACTLY once per parent id,
  // across calls, connections, threads and crash/reopen.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_agg_guard");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);
    const std::string db_path = qbrain::util::path_to_utf8(dir / "brain.db");

    Brain b("n34_agg_guard");
    b.open_at(db_path);

    // Same connection: first call true, every later call false.
    QB_CHECK(try_begin_aggregation(b, 42));
    QB_CHECK(!try_begin_aggregation(b, 42));
    QB_CHECK(!try_begin_aggregation(b, 42));
    // Different parent ids claim independently.
    QB_CHECK(try_begin_aggregation(b, 43));
    QB_CHECK(!try_begin_aggregation(b, 43));
    // Non-positive ids never claim and never insert rows.
    QB_CHECK(!try_begin_aggregation(b, 0));
    QB_CHECK(!try_begin_aggregation(b, -7));
    QB_CHECK(n34_fence_rows(b, 0) == 0);
    QB_CHECK(n34_fence_rows(b, -7) == 0);
    QB_CHECK(n34_fence_rows(b) == 2);

    // Second connection on the same database sees the same fence.
    Brain b2("n34_agg_guard_second");
    b2.open_at(db_path);
    QB_CHECK(!try_begin_aggregation(b2, 42));
    QB_CHECK(try_begin_aggregation(b2, 44));
    QB_CHECK(!try_begin_aggregation(b, 44));
    QB_CHECK(n34_fence_rows(b) == 3);
    b2.close();

    // Crash/reopen persistence: the fence row survives close+reopen, so the
    // exactly-once property holds across restarts (plan P2-1).
    b.close();
    Brain b3("n34_agg_guard_reopened");
    b3.open_at(db_path);
    QB_CHECK(!try_begin_aggregation(b3, 42));
    QB_CHECK(!try_begin_aggregation(b3, 44));
    QB_CHECK(n34_fence_rows(b3) == 3);

    // Concurrent guard claims on one fresh parent id: two threads on two
    // connections; exactly one observes true (busy loser retries).
    Brain b4("n34_agg_guard_racer_a");
    b4.open_at(db_path);
    Brain b5("n34_agg_guard_racer_b");
    b5.open_at(db_path);
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    int trues_a = 0;
    int trues_b = 0;
    std::exception_ptr unexpected_a;
    std::exception_ptr unexpected_b;
    auto race_guard = [&](Brain& brain, int& trues, std::exception_ptr& unexpected) {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      try {
        const bool won = n34_retry_on_busy([&] { return try_begin_aggregation(brain, 77); });
        if (won) trues = 1;
      } catch (...) {
        unexpected = std::current_exception();
      }
    };
    std::thread thread_a(race_guard, std::ref(b4), std::ref(trues_a), std::ref(unexpected_a));
    std::thread thread_b(race_guard, std::ref(b5), std::ref(trues_b), std::ref(unexpected_b));
    while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    thread_a.join();
    thread_b.join();
    if (unexpected_a) std::rethrow_exception(unexpected_a);
    if (unexpected_b) std::rethrow_exception(unexpected_b);
    QB_CHECK(trues_a + trues_b == 1);
    QB_CHECK(n34_fence_rows(b3, 77) == 1);
    b4.close();
    b5.close();
    b3.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (3) Legacy depth-0 jobs keep their exact pre-N34 storage shape
  // once the v13 hierarchy columns exist: parent_id NULL, depth 0.
  // (Raw SQL so the assertion holds regardless of Job struct fields.)
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_legacy_depth0");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);

    Brain b("n34_legacy_depth0");
    b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
    const int64_t id = submit_job(b, "embed", R"({"page_id":1})", "n34-legacy", 100);
    QB_CHECK(id > 0);
    QB_CHECK(complete_job(b, id, "tok-legacy", "{}") == false);  // not claimed yet: fence holds
    const auto job = claim_job(b, "tok-legacy", 30000, "n34-legacy");
    QB_CHECK(job.has_value() && job->id == id);
    QB_CHECK(complete_job(b, id, "tok-legacy", R"({"legacy":true})"));
    const auto done = get_job(b, id);
    QB_CHECK(done.has_value() && done->status == "completed");

    // v13 columns exist and legacy rows are untouched by them.
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE parent_id IS NULL AND depth=0") >= 1);
    QB_CHECK(n34_scalar(b,
                        "SELECT COUNT(*) FROM jobs WHERE parent_id IS NOT NULL OR depth<>0") == 0);
    // Legacy job paths never touch the aggregation fence: the table is not
    // even created on a brain that only ever ran depth-0 jobs.
    QB_CHECK(n34_scalar(b,
                        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                        "name='job_aggregation_fence'") == 0);
    QB_CHECK(n34_fence_rows(b) == 0);

    b.close();
    n34_best_effort_remove(dir);
  }

  // --- n34-b: hierarchy acceptance matrix ---
  // (subagent B; drives N34-A lifecycle: fanout/depth/cancel/retry races,
  // aggregation determinism, crash recovery, v12->v13 migration)

  // ============================================================
  // (4) Fanout matrix: 1 and 8 children succeed; 9 rejected
  // ("fanout"); oversized child payload rejected
  // ("children.payload_json"); a child cannot spawn ("depth").
  // Every rejection writes zero rows.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_fanout");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);

    Brain b("n34_fanout");
    b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));

    const auto spec = [](const std::string& payload, const std::string& queue = "") {
      qbrain::jobs::ChildSpec s;
      s.type = "n34_task";
      s.payload_json = payload;
      s.queue = queue;
      return s;
    };

    // n=1: parent + exactly one child.
    auto s1 = n34_spawn(b, "n34_fanout", {spec(R"({"k":1})")});
    QB_CHECK(s1.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(s1.reason.empty());
    QB_CHECK(s1.parent_id > 0);
    QB_CHECK(s1.child_ids.size() == 1);
    QB_CHECK(n34_status_of(b, s1.parent_id) == "waiting_children");
    QB_CHECK(n34_child_shape(b, s1.parent_id, s1.child_ids));
    QB_CHECK(n34_fence_rows(b) == 0);
    {
      const auto hierarchy = qbrain::jobs::get_job_hierarchy(b, s1.parent_id);
      QB_CHECK(hierarchy.has_value());
      QB_CHECK(hierarchy->child_count == 1);
      QB_CHECK(hierarchy->children.size() == 1);
      QB_CHECK(hierarchy->children.front().id == s1.child_ids.front());
      QB_CHECK(hierarchy->depth == 0 && hierarchy->parent_id == 0);
    }

    // n=8: maximum fanout succeeds. Each child keeps ITS OWN queue and
    // payload (plan: "每子独立 payload/queue") — asserted row by row below.
    std::vector<qbrain::jobs::ChildSpec> eight;
    std::vector<std::string> expected_payloads;
    for (int i = 0; i < 8; ++i) {
      eight.push_back(spec(R"({"k":)" + std::to_string(i) + "}", "n34_f" + std::to_string(i)));
      expected_payloads.push_back(R"({"k":)" + std::to_string(i) + "}");
    }
    auto s8 = n34_spawn(b, "n34_fanout", eight);
    QB_CHECK(s8.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(s8.child_ids.size() == 8);
    QB_CHECK(n34_status_of(b, s8.parent_id) == "waiting_children");
    QB_CHECK(n34_child_shape(b, s8.parent_id, s8.child_ids));
    for (size_t i = 0; i < s8.child_ids.size(); ++i) {
      const auto row = get_job(b, s8.child_ids[i]);
      QB_CHECK(row.has_value());
      QB_CHECK(row->queue == "n34_f" + std::to_string(i));
      QB_CHECK(row->payload_json == expected_payloads[i]);
      QB_CHECK(row->type == "n34_task");
    }
    const int64_t jobs_after_8 = n34_scalar(b, "SELECT COUNT(*) FROM jobs");
    QB_CHECK(jobs_after_8 == 2 + 1 + 8);

    // n=9: invalid_argument "fanout" against an existing waiting parent;
    // zero rows written (no parent flip, no children).
    const int64_t p9 = qbrain::jobs::submit_job(b, "n34_fanout", "{}", "default", 100);
    std::vector<qbrain::jobs::ChildSpec> nine;
    for (int i = 0; i < 9; ++i) nine.push_back(spec(R"({"k":9})"));
    const int64_t jobs_before_reject = n34_scalar(b, "SELECT COUNT(*) FROM jobs");
    auto r9 = qbrain::jobs::spawn_children(b, p9, nine);
    QB_CHECK(r9.status == qbrain::jobs::JobOperationStatus::invalid_argument);
    QB_CHECK(r9.reason == "fanout");
    QB_CHECK(r9.child_ids.empty());
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs") == jobs_before_reject);
    QB_CHECK(n34_status_of(b, p9) == "waiting");
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE parent_id=" +
                              std::to_string(p9)) == 0);

    // Oversized child payload (valid JSON beyond kJobChildPayloadMaxBytes =
    // 65536): invalid_argument "children.payload_json", zero rows.
    const std::string oversize = R"({"pad":")" + std::string(70000, 'x') + R"("})";
    QB_CHECK(oversize.size() > qbrain::jobs::kJobChildPayloadMaxBytes);
    const std::vector<qbrain::jobs::ChildSpec> oversize_specs{spec(oversize)};
    auto ro = qbrain::jobs::spawn_children(b, p9, oversize_specs);
    QB_CHECK(ro.status == qbrain::jobs::JobOperationStatus::invalid_argument);
    QB_CHECK(ro.reason == "children.payload_json");
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs") == jobs_before_reject);
    QB_CHECK(n34_status_of(b, p9) == "waiting");

    // A child (depth=1) cannot spawn: invalid_argument "depth", zero rows.
    const std::vector<qbrain::jobs::ChildSpec> grandchild{spec(R"({"k":"gc"})")};
    auto rd = qbrain::jobs::spawn_children(b, s1.child_ids.front(), grandchild);
    QB_CHECK(rd.status == qbrain::jobs::JobOperationStatus::invalid_argument);
    QB_CHECK(rd.reason == "depth");
    QB_CHECK(rd.child_ids.empty());
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs") == jobs_before_reject);

    // Unknown parent: not_found "parent_id".
    const std::vector<qbrain::jobs::ChildSpec> one_spec{spec(R"({"k":1})")};
    auto rn = qbrain::jobs::spawn_children(b, 424242, one_spec);
    QB_CHECK(rn.status == qbrain::jobs::JobOperationStatus::not_found);
    QB_CHECK(rn.reason == "parent_id");

    b.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (5) Cancel propagation: cancelling a parent cancels every
  // non-terminal child in the same transaction (exact counts);
  // a single child cancel leaves siblings and the parent alone.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_cancel");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);

    Brain b("n34_cancel");
    b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));

    const auto spec = [](const std::string& payload, const std::string& queue = "") {
      qbrain::jobs::ChildSpec s;
      s.type = "n34_task";
      s.payload_json = payload;
      s.queue = queue;
      return s;
    };

    // Parent A with 3 children; child A1 is claimed (active), A2/A3 waiting.
    auto sa = n34_spawn(b, "n34_cancel_parent",
                        {spec(R"({"k":"a1"})", "n34_ca1"), spec(R"({"k":"a2"})"),
                         spec(R"({"k":"a3"})")});
    QB_CHECK(sa.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(sa.child_ids.size() == 3);
    const auto claimed = claim_job(b, "tok-cancel-a1", 30000, "n34_ca1");
    QB_CHECK(claimed.has_value() && claimed->id == sa.child_ids[0]);
    QB_CHECK(claimed->status == "active");

    // Cancelling the parent cancels all three children, whatever their state.
    const auto cancel_a = qbrain::jobs::cancel_job_tree(b, sa.parent_id);
    QB_CHECK(cancel_a.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(cancel_a.cancelled);
    QB_CHECK(cancel_a.cancelled_children == 3);
    QB_CHECK(n34_status_of(b, sa.parent_id) == "cancelled");
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE parent_id=" +
                              std::to_string(sa.parent_id) + " AND status='cancelled'") == 3);
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE parent_id=" +
                              std::to_string(sa.parent_id)) == 3);

    // Parent B with 3 children; cancel only B2: B1/B3 and the parent stay.
    auto sb = n34_spawn(b, "n34_cancel_parent",
                        {spec(R"({"k":"b1"})", "n34_cb1"), spec(R"({"k":"b2"})"),
                         spec(R"({"k":"b3"})")});
    QB_CHECK(sb.status == qbrain::jobs::JobOperationStatus::success);
    const auto cancel_b2 = qbrain::jobs::cancel_job_tree(b, sb.child_ids[1]);
    QB_CHECK(cancel_b2.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(cancel_b2.cancelled);
    QB_CHECK(cancel_b2.cancelled_children == 0);  // leaf cancel: no propagation
    QB_CHECK(n34_status_of(b, sb.child_ids[1]) == "cancelled");
    QB_CHECK(n34_status_of(b, sb.parent_id) == "waiting_children");
    QB_CHECK(n34_status_of(b, sb.child_ids[0]) == "waiting");
    QB_CHECK(n34_status_of(b, sb.child_ids[2]) == "waiting");
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE parent_id=" +
                              std::to_string(sb.parent_id) +
                              " AND status='cancelled'") == 1);
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE parent_id=" +
                              std::to_string(sb.parent_id) +
                              " AND status='waiting'") == 2);

    b.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (6) D3 point 1 — dual-worker race on the SAME child: exactly
  // one claim wins (N12 token fence extended to the hierarchy,
  // siblings may be claimed by different workers in parallel).
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_claim_race");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);
    const std::string db_path = qbrain::util::path_to_utf8(dir / "brain.db");

    Brain b("n34_claim_race");
    b.open_at(db_path);
    qbrain::jobs::ChildSpec race_spec;
    race_spec.type = "n34_task";
    race_spec.payload_json = R"({"k":1})";
    race_spec.queue = "n34_race";
    auto s = n34_spawn(b, "n34_claim_race", {race_spec});
    QB_CHECK(s.status == qbrain::jobs::JobOperationStatus::success);
    const int64_t child = s.child_ids.front();
    // The parent is waiting_children, so it is invisible to claim_job: the
    // only claimable row in this queue is the child.
    QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs WHERE queue='n34_race' AND "
                          "status='waiting'") == 1);

    Brain b2("n34_claim_race_second");
    b2.open_at(db_path);

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::optional<qbrain::jobs::Job> out_a;
    std::optional<qbrain::jobs::Job> out_b;
    std::exception_ptr unexpected_a;
    std::exception_ptr unexpected_b;
    bool busy_a = false;
    bool busy_b = false;
    auto race_claim = [&](Brain& brain, const char* token, std::optional<qbrain::jobs::Job>& out,
                          std::exception_ptr& unexpected, bool& busy) {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      try {
        out = n34_retry_on_busy(
            [&] { return claim_job(brain, token, 30000, "n34_race"); });
        if (!out.has_value()) busy = true;  // fence loser: nothing to claim
      } catch (const std::exception& e) {
        busy = n34_is_busy(e);
        if (!busy) unexpected = std::current_exception();
      }
    };
    std::thread thread_a(race_claim, std::ref(b), "tok-race-A", std::ref(out_a),
                         std::ref(unexpected_a), std::ref(busy_a));
    std::thread thread_b(race_claim, std::ref(b2), "tok-race-B", std::ref(out_b),
                         std::ref(unexpected_b), std::ref(busy_b));
    while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    thread_a.join();
    thread_b.join();
    if (unexpected_a) std::rethrow_exception(unexpected_a);
    if (unexpected_b) std::rethrow_exception(unexpected_b);

    const int winners = static_cast<int>(out_a.has_value()) + static_cast<int>(out_b.has_value());
    QB_CHECK(winners == 1);  // exactly one worker holds the child fence
    const auto winner_token = out_a ? "tok-race-A" : "tok-race-B";
    const auto& winner = out_a ? out_a : out_b;
    QB_CHECK(winner->id == child);
    QB_CHECK(winner->status == "active");
    QB_CHECK(winner->lock_token == winner_token);
    const auto fenced = get_job(b, child);
    QB_CHECK(fenced.has_value() && fenced->status == "active" &&
             fenced->lock_token == winner_token);
    // The loser cannot complete the fenced child.
    const auto loser_token = out_a ? "tok-race-B" : "tok-race-A";
    QB_CHECK(!complete_job(b, child, loser_token, "{}"));
    QB_CHECK(complete_job(b, child, winner_token, R"({"race":"won"})"));

    b2.close();
    b.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (7) D3 point 2 — the last two children complete CONCURRENTLY:
  // each worker completes its child and then attempts the parent
  // aggregation; the transition happens EXACTLY once (one fence
  // row, exactly one aggregated==true), both children complete,
  // and the aggregate content is deterministic.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_agg_race");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);
    const std::string db_path = qbrain::util::path_to_utf8(dir / "brain.db");

    Brain b("n34_agg_race");
    b.open_at(db_path);
    const auto spec = [&](const std::string& queue) {
      qbrain::jobs::ChildSpec s;
      s.type = "n34_task";
      s.payload_json = R"({"k":1})";
      s.queue = queue;
      return s;
    };
    auto s = n34_spawn(b, "n34_agg_race", {spec("n34_ac1"), spec("n34_ac2")});
    QB_CHECK(s.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(s.child_ids.size() == 2);
    const int64_t child1 = s.child_ids[0];
    const int64_t child2 = s.child_ids[1];

    // Both children are claimed by DIFFERENT workers before the race; the
    // parent aggregates only when the last of them completes, so racing the
    // two completions is exactly the plan's "last two children concurrent"
    // scenario.
    const auto c1 = claim_job(b, "tok-aggr-1", 30000, "n34_ac1");
    QB_CHECK(c1.has_value() && c1->id == child1);
    Brain b2("n34_agg_race_second");
    b2.open_at(db_path);
    const auto c2 = claim_job(b2, "tok-aggr-2", 30000, "n34_ac2");
    QB_CHECK(c2.has_value() && c2->id == child2);

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::exception_ptr unexpected_a;
    std::exception_ptr unexpected_b;
    bool aggregated_a = false;
    bool aggregated_b = false;
    auto race_worker = [&](Brain& brain, int64_t child, const char* token, bool& aggregated,
                           std::exception_ptr& unexpected) {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      try {
        // Worker protocol: complete the fenced child, then attempt the
        // idempotent parent aggregation. Busy is transient (no busy_timeout
        // on these connections): retry the same guarded operations.
        n34_retry_on_busy([&] { return complete_job(brain, child, token, R"({"child":"done"})"); });
        const auto agg = n34_retry_on_busy(
            [&] { return qbrain::jobs::aggregate_if_ready(brain, s.parent_id); });
        aggregated = agg.aggregated;
      } catch (const std::exception& e) {
        if (!n34_is_busy(e)) unexpected = std::current_exception();
      }
    };
    std::thread thread_a(race_worker, std::ref(b), child1, "tok-aggr-1",
                         std::ref(aggregated_a), std::ref(unexpected_a));
    std::thread thread_b(race_worker, std::ref(b2), child2, "tok-aggr-2",
                         std::ref(aggregated_b), std::ref(unexpected_b));
    while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    thread_a.join();
    thread_b.join();
    if (unexpected_a) std::rethrow_exception(unexpected_a);
    if (unexpected_b) std::rethrow_exception(unexpected_b);

    // Both children reached their terminal state.
    QB_CHECK(n34_status_of(b, child1) == "completed");
    QB_CHECK(n34_status_of(b, child2) == "completed");
    // Aggregation happened EXACTLY once across the racing workers.
    QB_CHECK(static_cast<int>(aggregated_a) + static_cast<int>(aggregated_b) == 1);
    QB_CHECK(n34_status_of(b, s.parent_id) == "completed");
    QB_CHECK(n34_fence_rows(b, s.parent_id) == 1);
    QB_CHECK(n34_fence_rows(b) == 1);
    // The guard stays claimed afterwards and re-aggregation is a no-op that
    // returns the stored JSON.
    QB_CHECK(!try_begin_aggregation(b, s.parent_id));
    const auto again = qbrain::jobs::aggregate_if_ready(b, s.parent_id);
    QB_CHECK(again.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(again.ready);
    QB_CHECK(!again.aggregated);
    // Deterministic aggregate content for the raced run.
    const auto parent = get_job(b, s.parent_id);
    QB_CHECK(parent.has_value());
    QB_CHECK(again.aggregate_json == parent->result_json);
    const auto aggregate = json::parse(parent->result_json);
    QB_CHECK(aggregate["child_counts"].size() == 1);
    QB_CHECK(aggregate["child_counts"]["completed"] == 2);
    QB_CHECK(aggregate["errors"].is_array() && aggregate["errors"].empty());
    QB_CHECK(aggregate["order"] == "child_id");

    b2.close();
    b.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (8) Aggregation determinism: two independent constructions
  // with different completion orders (and different terminal
  // trigger paths) produce a byte-identical aggregate; mixed
  // statuses give exact counts and child_id-sorted errors (<= 8).
  // ============================================================
  {
    const auto run_fixture = [](const char* leaf, bool reverse_order) {
      const fs::path dir = n34_fresh_dir(leaf);
      Brain b(leaf);
      b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
      const auto spec = [](int index) {
        qbrain::jobs::ChildSpec s;
        s.type = "n34_task";
        s.payload_json = R"({"k":1})";
        s.queue = "n34_d" + std::to_string(index + 1);
        return s;
      };
      auto s = n34_spawn(b, "n34_det", {spec(0), spec(1), spec(2), spec(3)});
      QB_CHECK(s.status == qbrain::jobs::JobOperationStatus::success);
      QB_CHECK(s.child_ids.size() == 4);
      // Child terminal plan (by index): 0 completed, 1 failed, 2 cancelled,
      // 3 completed. The worker protocol runs aggregate_if_ready after each
      // terminal transition; only the final one flips the parent. The
      // completion order is reversed in the second run and the final trigger
      // differs (completion vs cancellation of the last non-terminal child).
      const auto finish = [&](int index) {
        const int64_t id = s.child_ids[index];
        const std::string queue = "n34_d" + std::to_string(index + 1);
        if (index == 0 || index == 3) {
          const auto job = claim_job(b, "tok-det", 30000, queue);
          QB_CHECK(job.has_value() && job->id == id);
          QB_CHECK(complete_job(b, id, "tok-det", R"({"n":1})"));
        } else if (index == 1) {
          const auto job = claim_job(b, "tok-det", 30000, queue);
          QB_CHECK(job.has_value() && job->id == id);
          QB_CHECK(qbrain::jobs::fail_job(b, id, "tok-det", "n34 deterministic failure"));
        } else {
          const auto cancelled = qbrain::jobs::cancel_job_tree(b, id);
          QB_CHECK(cancelled.status == qbrain::jobs::JobOperationStatus::success);
        }
        const auto agg = qbrain::jobs::aggregate_if_ready(b, s.parent_id);
        QB_CHECK(agg.status == qbrain::jobs::JobOperationStatus::success ||
                 agg.reason == "not_ready");
      };
      if (reverse_order) {
        for (int index : {2, 1, 3, 0}) finish(index);  // ends on a completion
      } else {
        for (int index : {0, 1, 3, 2}) finish(index);  // ends on a cancellation
      }
      const auto parent = get_job(b, s.parent_id);
      QB_CHECK(parent.has_value());
      QB_CHECK(parent->status == "completed");
      const auto failed_child = get_job(b, s.child_ids[1]);
      QB_CHECK(failed_child.has_value());
      const std::string failed_error = failed_child->error_text;
      const int64_t failed_id = failed_child->id;
      b.close();
      n34_best_effort_remove(dir);
      return std::tuple<std::string, std::string, int64_t>(parent->result_json, failed_error,
                                                           failed_id);
    };

    const auto forward = run_fixture("qbrain_n34_det_forward", false);
    const auto reverse = run_fixture("qbrain_n34_det_reverse", true);
    // Two independent constructions are byte-identical.
    QB_CHECK(std::get<0>(forward) == std::get<0>(reverse));
    QB_CHECK(std::get<1>(forward) == std::get<1>(reverse));

    // Exact schema: counts, one child_id-sorted error entry, order marker.
    const auto aggregate = json::parse(std::get<0>(forward));
    QB_CHECK(aggregate["child_counts"].size() == 3);
    QB_CHECK(aggregate["child_counts"]["completed"] == 2);
    QB_CHECK(aggregate["child_counts"]["failed"] == 1);
    QB_CHECK(aggregate["child_counts"]["cancelled"] == 1);
    QB_CHECK(aggregate["errors"].is_array() && aggregate["errors"].size() == 1);
    QB_CHECK(aggregate["errors"][0]["child_id"] == std::get<2>(forward));
    QB_CHECK(aggregate["errors"][0]["error"] == std::get<1>(forward));
    QB_CHECK(aggregate["order"] == "child_id");

    // Eight failing children: error list stays capped at 8, child_id sorted.
    {
      const fs::path dir = n34_fresh_dir("qbrain_n34_det_errors8");
      Brain b("n34_det_errors8");
      b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));
      std::vector<qbrain::jobs::ChildSpec> eight;
      for (int i = 0; i < 8; ++i) {
        qbrain::jobs::ChildSpec s;
        s.type = "n34_task";
        s.payload_json = R"({"k":"f"})";
        s.queue = "n34_e" + std::to_string(i);
        eight.push_back(s);
      }
      auto s = n34_spawn(b, "n34_det8", eight);
      QB_CHECK(s.status == qbrain::jobs::JobOperationStatus::success);
      for (size_t i = 0; i < s.child_ids.size(); ++i) {
        const auto queue = "n34_e" + std::to_string(i);
        const auto job = claim_job(b, "tok-det8", 30000, queue);
        QB_CHECK(job.has_value() && job->id == s.child_ids[i]);
        QB_CHECK(qbrain::jobs::fail_job(b, s.child_ids[i], "tok-det8", "boom"));
      }
      const auto agg = qbrain::jobs::aggregate_if_ready(b, s.parent_id);
      QB_CHECK(agg.status == qbrain::jobs::JobOperationStatus::success);
      QB_CHECK(agg.aggregated);
      const auto parent = get_job(b, s.parent_id);
      QB_CHECK(parent.has_value() && parent->status == "completed");
      const auto aggregate8 = json::parse(parent->result_json);
      QB_CHECK(aggregate8["child_counts"].size() == 1);
      QB_CHECK(aggregate8["child_counts"]["failed"] == 8);
      QB_CHECK(aggregate8["errors"].size() == 8);
      std::vector<int64_t> error_ids;
      for (const auto& e : aggregate8["errors"]) error_ids.push_back(e["child_id"]);
      QB_CHECK(std::is_sorted(error_ids.begin(), error_ids.end()));
      // And the error list equals the sorted child id set exactly.
      std::vector<int64_t> sorted_children = s.child_ids;
      std::sort(sorted_children.begin(), sorted_children.end());
      QB_CHECK(error_ids == sorted_children);
      b.close();
      n34_best_effort_remove(dir);
    }
  }

  // ============================================================
  // (9) Retry matrix: a leaf child retries to waiting (exact N12
  // semantics); a parent with non-terminal children is rejected
  // with "non_terminal_children"; an all-terminal parent is
  // rejected with "parent_not_retryable"; legacy depth-0 retry
  // is unchanged.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_retry");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);

    Brain b("n34_retry");
    b.open_at(qbrain::util::path_to_utf8(dir / "brain.db"));

    const auto spec = [](const std::string& queue) {
      qbrain::jobs::ChildSpec s;
      s.type = "n34_task";
      s.payload_json = R"({"k":1})";
      s.queue = queue;
      return s;
    };
    auto s = n34_spawn(b, "n34_retry_parent", {spec("n34_r1"), spec("n34_r2")});
    QB_CHECK(s.status == qbrain::jobs::JobOperationStatus::success);

    // Leaf retry: cancel child 1, retry brings it back to waiting.
    const auto cancelled = qbrain::jobs::cancel_job_tree(b, s.child_ids[0]);
    QB_CHECK(cancelled.status == qbrain::jobs::JobOperationStatus::success);
    const auto retry_leaf = qbrain::jobs::retry_job_hierarchy(b, s.child_ids[0]);
    QB_CHECK(retry_leaf.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(retry_leaf.requeued);
    QB_CHECK(n34_status_of(b, s.child_ids[0]) == "waiting");

    // Parent retry while children are non-terminal: rejected, nothing moves.
    const auto retry_parent = qbrain::jobs::retry_job_hierarchy(b, s.parent_id);
    QB_CHECK(retry_parent.status == qbrain::jobs::JobOperationStatus::invalid_state);
    QB_CHECK(retry_parent.reason == "non_terminal_children");
    QB_CHECK(!retry_parent.requeued);
    QB_CHECK(n34_status_of(b, s.parent_id) == "waiting_children");
    QB_CHECK(n34_status_of(b, s.child_ids[0]) == "waiting");
    QB_CHECK(n34_status_of(b, s.child_ids[1]) == "waiting");

    // Complete both leaves; the completed parent is not retryable either.
    for (size_t i = 0; i < 2; ++i) {
      const auto queue = "n34_r" + std::to_string(i + 1);
      const auto job = claim_job(b, "tok-retry", 30000, queue);
      QB_CHECK(job.has_value() && job->id == s.child_ids[i]);
      QB_CHECK(complete_job(b, s.child_ids[i], "tok-retry", "{}"));
    }
    const auto agg = qbrain::jobs::aggregate_if_ready(b, s.parent_id);
    QB_CHECK(agg.status == qbrain::jobs::JobOperationStatus::success && agg.aggregated);
    QB_CHECK(n34_status_of(b, s.parent_id) == "completed");
    const auto retry_done = qbrain::jobs::retry_job_hierarchy(b, s.parent_id);
    QB_CHECK(retry_done.status == qbrain::jobs::JobOperationStatus::invalid_state);
    QB_CHECK(retry_done.reason == "parent_not_retryable");

    // Legacy depth-0 leaf keeps the exact N12 retry path.
    const int64_t legacy = submit_job(b, "embed", R"({"page_id":1})", "n34-legacy", 100);
    QB_CHECK(qbrain::jobs::cancel_job_tree(b, legacy).status ==
             qbrain::jobs::JobOperationStatus::success);
    const auto retry_legacy = qbrain::jobs::retry_job_hierarchy(b, legacy);
    QB_CHECK(retry_legacy.status == qbrain::jobs::JobOperationStatus::success);
    QB_CHECK(retry_legacy.requeued);
    QB_CHECK(n34_status_of(b, legacy) == "waiting");

    b.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (10) Crash recovery (plan P2-1): after spawn + claim, close
  // and reopen the brain — status/attempts/lock_token/parent_id/
  // depth all persist; the claimed child is still completable
  // with its pre-crash token; the parent reaches completed with
  // the aggregate exactly once.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_crash");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);
    const std::string db_path = qbrain::util::path_to_utf8(dir / "brain.db");

    std::vector<qbrain::jobs::ChildSpec> two;
    for (const char* queue : {"n34_cr1", "n34_cr2"}) {
      qbrain::jobs::ChildSpec s;
      s.type = "n34_task";
      s.payload_json = R"({"k":1})";
      s.queue = queue;
      two.push_back(s);
    }
    int64_t parent_id = 0;
    std::vector<int64_t> children;

    {
      Brain b("n34_crash");
      b.open_at(db_path);
      auto s = n34_spawn(b, "n34_crash_parent", two);
      QB_CHECK(s.status == qbrain::jobs::JobOperationStatus::success);
      parent_id = s.parent_id;
      children = s.child_ids;
      const auto claimed = claim_job(b, "tok-crash-1", 30000, "n34_cr1");
      QB_CHECK(claimed.has_value() && claimed->id == children[0]);
      QB_CHECK(claimed->attempts == 1);
      b.close();  // "crash" after claim, before completion
    }

    Brain b2("n34_crash_reopened");
    b2.open_at(db_path);
    // Every fence/hierarchy field survived the close/reopen cycle.
    const auto parent = get_job(b2, parent_id);
    QB_CHECK(parent.has_value());
    QB_CHECK(parent->status == "waiting_children");
    QB_CHECK(parent->attempts == 0);
    QB_CHECK(parent->lock_token.empty());
    QB_CHECK(n34_parent_id_of(b2, parent_id) == 0);  // NULL
    QB_CHECK(n34_depth_of(b2, parent_id) == 0);
    const auto c1 = get_job(b2, children[0]);
    QB_CHECK(c1.has_value());
    QB_CHECK(c1->status == "active");
    QB_CHECK(c1->attempts == 1);
    QB_CHECK(c1->lock_token == "tok-crash-1");
    QB_CHECK(n34_parent_id_of(b2, children[0]) == parent_id);
    QB_CHECK(n34_depth_of(b2, children[0]) == 1);
    const auto c2 = get_job(b2, children[1]);
    QB_CHECK(c2.has_value() && c2->status == "waiting" && c2->lock_token.empty());
    QB_CHECK(n34_parent_id_of(b2, children[1]) == parent_id);
    QB_CHECK(n34_depth_of(b2, children[1]) == 1);

    // The pre-crash fence still governs: a wrong token cannot complete the
    // claimed child; the persisted token can. The aggregation attempt at
    // this point correctly reports not-ready (child 2 is still waiting).
    QB_CHECK(!complete_job(b2, children[0], "tok-crash-wrong", "{}"));
    QB_CHECK(complete_job(b2, children[0], "tok-crash-1", R"({"after":"reopen"})"));
    {
      const auto early = qbrain::jobs::aggregate_if_ready(b2, parent_id);
      QB_CHECK(early.status == qbrain::jobs::JobOperationStatus::invalid_state);
      QB_CHECK(early.reason == "not_ready");
    }
    // Finish the second child; the parent aggregates exactly once.
    const auto claimed2 = claim_job(b2, "tok-crash-2", 30000, "n34_cr2");
    QB_CHECK(claimed2.has_value() && claimed2->id == children[1]);
    QB_CHECK(complete_job(b2, children[1], "tok-crash-2", R"({"after":"reopen"})"));
    const auto agg = qbrain::jobs::aggregate_if_ready(b2, parent_id);
    QB_CHECK(agg.status == qbrain::jobs::JobOperationStatus::success && agg.aggregated);
    QB_CHECK(n34_status_of(b2, parent_id) == "completed");
    QB_CHECK(n34_fence_rows(b2, parent_id) == 1);
    const auto finished = get_job(b2, parent_id);
    QB_CHECK(finished.has_value());
    const auto aggregate = json::parse(finished->result_json);
    QB_CHECK(aggregate["child_counts"]["completed"] == 2);

    b2.close();
    n34_best_effort_remove(dir);
  }

  // ============================================================
  // (11) v12 -> v13 migration: a populated v12 database (crafted
  // from the migrated shape by removing the v13 objects) opens,
  // auto-migrates, and keeps every row; the migration is
  // idempotent; doctor fails when a hierarchy column is missing.
  // ============================================================
  {
    const fs::path dir = n34_fresh_dir("qbrain_n34_migration");
    const std::string local_root = qbrain::util::path_to_utf8(dir / "localappdata");
    N34ScopedEnv local_app_data("LOCALAPPDATA", local_root);
    const std::string db_path = qbrain::util::path_to_utf8(dir / "brain.db");

    // Populate a real brain: pages/chunks, facts, jobs in mixed states.
    int64_t pages = 0, chunks = 0, jobs = 0, facts = 0;
    {
      Brain b("n34_migration");
      b.open_at(db_path);
      b.ensure_source("default");
      for (int i = 0; i < 3; ++i) {
        qbrain::PageInput in;
        in.slug = "n34-mig-" + std::to_string(i);
        in.body = "n34 migration body " + std::to_string(i) + " with content";
        b.put_page(in);
      }
      b.add_fact("n34-entity", "mentions", "n34 migration fact");
      const int64_t j1 = submit_job(b, "embed", R"({"page_id":1})", "n34-mig", 100);
      const int64_t j2 = submit_job(b, "extract_facts", R"({"slug":"n34-mig-0"})", "n34-mig", 100);
      const auto claimed = claim_job(b, "tok-mig", 30000, "n34-mig");
      QB_CHECK(claimed.has_value() && claimed->id == j1);
      QB_CHECK(complete_job(b, j1, "tok-mig", R"({"mig":1})"));
      QB_CHECK(qbrain::jobs::cancel_job(b, j2));
      pages = n34_scalar(b, "SELECT COUNT(*) FROM pages");
      chunks = n34_scalar(b, "SELECT COUNT(*) FROM content_chunks");
      jobs = n34_scalar(b, "SELECT COUNT(*) FROM jobs");
      facts = n34_scalar(b, "SELECT COUNT(*) FROM facts");
      QB_CHECK(pages == 3 && jobs == 2 && facts >= 1);
      b.close();
    }

    // Craft the pre-N34 v12 shape: drop the v13 objects and version row.
    {
      qbrain::storage::Database raw;
      raw.open(db_path);
      // Drop any indexes on jobs that use the hierarchy columns first:
      // SQLite refuses DROP COLUMN while the column is indexed.
      std::vector<std::string> drop_indexes;
      auto idx = raw.prepare(
          "SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='jobs'");
      while (idx.step()) {
        const std::string name = idx.column_text(0);
        auto info = raw.prepare("PRAGMA index_info(" + name + ")");
        bool hierarchy = false;
        while (info.step()) {
          const std::string column = info.column_text(2);
          if (column == "parent_id" || column == "depth") hierarchy = true;
        }
        if (hierarchy) drop_indexes.push_back(name);
      }
      for (const auto& name : drop_indexes) raw.exec("DROP INDEX IF EXISTS " + name + ";");
      raw.exec("DROP TABLE IF EXISTS job_aggregation_fence;");
      raw.exec("ALTER TABLE jobs DROP COLUMN parent_id;");
      raw.exec("ALTER TABLE jobs DROP COLUMN depth;");
      raw.exec("DELETE FROM schema_version WHERE version>=13;");
      {
        auto st = raw.prepare("SELECT COALESCE(MAX(version),0) FROM schema_version");
        QB_CHECK(st.step() && st.column_int(0) == 12);
      }
      {
        auto st = raw.prepare("SELECT COUNT(*) FROM jobs");
        QB_CHECK(st.step() && st.column_int(0) == jobs);
      }
      raw.close();
    }

    // Reopening auto-migrates v12 -> v13 with every row preserved.
    {
      Brain b("n34_migration_reopen");
      b.open_at(db_path);
      QB_CHECK(n34_scalar(b, "SELECT COALESCE(MAX(version),0) FROM schema_version") == 13);
      QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM pages") == pages);
      QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM content_chunks") == chunks);
      QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM jobs") == jobs);
      QB_CHECK(n34_scalar(b, "SELECT COUNT(*) FROM facts") == facts);
      // Existing rows are depth-0 legacy rows with no parent.
      QB_CHECK(n34_scalar(b,
                          "SELECT COUNT(*) FROM jobs WHERE parent_id IS NULL AND depth=0") ==
               jobs);
      // Jobs remain fully operational after the migration.
      const int64_t fresh = submit_job(b, "embed", R"({"page_id":2})", "n34-mig", 100);
      const auto claimed = claim_job(b, "tok-mig2", 30000, "n34-mig");
      QB_CHECK(claimed.has_value() && claimed->id == fresh);
      QB_CHECK(complete_job(b, fresh, "tok-mig2", "{}"));
      // Idempotent: a second open is a no-op migration.
      b.close();
      Brain again("n34_migration_again");
      again.open_at(db_path);
      QB_CHECK(n34_scalar(again, "SELECT COALESCE(MAX(version),0) FROM schema_version") == 13);
      QB_CHECK(n34_scalar(again, "SELECT COUNT(*) FROM schema_version WHERE version=13") == 1);
      QB_CHECK(n34_scalar(again, "SELECT COUNT(*) FROM jobs") == jobs + 1);
      again.close();
    }

    // Doctor integrity (acceptance 7b): removing either hierarchy column
    // from a v13 database fails check_schema_integrity with that column
    // named. Crafted on file copies so the migrated DB stays intact.
    {
      const fs::path v13_copy_a = dir / "doctor_a.db";
      const fs::path v13_copy_b = dir / "doctor_b.db";
      fs::copy_file(db_path, v13_copy_a, fs::copy_options::overwrite_existing);
      fs::copy_file(db_path, v13_copy_b, fs::copy_options::overwrite_existing);

      const auto drop_column = [](const fs::path& path, const char* column) {
        qbrain::storage::Database raw;
        raw.open(qbrain::util::path_to_utf8(path));
        std::vector<std::string> drop_indexes;
        auto idx = raw.prepare(
            "SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='jobs'");
        while (idx.step()) {
          const std::string name = idx.column_text(0);
          auto info = raw.prepare("PRAGMA index_info(" + name + ")");
          bool uses_column = false;
          while (info.step()) {
            if (info.column_text(2) == column) uses_column = true;
          }
          if (uses_column) drop_indexes.push_back(name);
        }
        for (const auto& name : drop_indexes) raw.exec("DROP INDEX IF EXISTS " + name + ";");
        raw.exec(std::string("ALTER TABLE jobs DROP COLUMN ") + column + ";");
        raw.close();
      };

      drop_column(v13_copy_a, "parent_id");
      {
        qbrain::storage::Database raw;
        raw.open(qbrain::util::path_to_utf8(v13_copy_a));
        const auto integrity = qbrain::storage::check_schema_integrity(raw);
        QB_CHECK(!integrity.ok);
        bool named = false;
        for (const auto& entry : integrity.missing) {
          if (entry.find("parent_id") != std::string::npos) named = true;
        }
        QB_CHECK(named);
        raw.close();
      }

      drop_column(v13_copy_b, "depth");
      {
        qbrain::storage::Database raw;
        raw.open(qbrain::util::path_to_utf8(v13_copy_b));
        const auto integrity = qbrain::storage::check_schema_integrity(raw);
        QB_CHECK(!integrity.ok);
        bool named = false;
        for (const auto& entry : integrity.missing) {
          if (entry.find("depth") != std::string::npos) named = true;
        }
        QB_CHECK(named);
        raw.close();
      }
    }

    n34_best_effort_remove(dir);
  }
}

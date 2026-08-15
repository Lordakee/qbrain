#include "qbrain/core/brain.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/util/paths.hpp"
#include <atomic>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#define QB_CHECK(cond)                                                  \
  do {                                                                  \
    if (!(cond)) {                                                      \
      throw std::runtime_error(std::string("CHECK failed: ") + #cond);  \
    }                                                                   \
  } while (0)

void test_minions() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "qbrain_minions_test";
  fs::create_directories(dir);
  auto dbp = dir / "brain.db";
  fs::remove(dbp);

  qbrain::Brain b("minions_test");
  b.open_at(qbrain::util::path_to_utf8(dbp));

  // empty token cannot claim
  auto id = qbrain::jobs::submit_job(b, "extract_facts",
                                     R"({"slug":"x","source_id":"default"})");
  QB_CHECK(id > 0);
  auto bad = qbrain::jobs::claim_job(b, "", 30000);
  QB_CHECK(!bad.has_value());

  // claim with token
  auto job = qbrain::jobs::claim_job(b, "tok-A", 30000);
  QB_CHECK(job.has_value());
  QB_CHECK(job->status == "active");
  QB_CHECK(job->lock_token == "tok-A");
  QB_CHECK(job->attempts == 1);

  // An active row cannot be claimed again.
  auto duplicate_claim = qbrain::jobs::claim_job(b, "tok-second", 30000);
  QB_CHECK(!duplicate_claim.has_value());

  // Wrong/empty tokens cannot complete or fail, and leave the active fence intact.
  QB_CHECK(!qbrain::jobs::complete_job(b, job->id, "tok-B", "{}"));
  QB_CHECK(!qbrain::jobs::complete_job(b, job->id, "", "{}"));
  QB_CHECK(!qbrain::jobs::fail_job(b, job->id, "tok-B", "wrong token"));
  QB_CHECK(!qbrain::jobs::fail_job(b, job->id, "", "empty token"));
  auto still_active = qbrain::jobs::get_job(b, job->id);
  QB_CHECK(still_active.has_value());
  QB_CHECK(still_active->status == "active");
  QB_CHECK(still_active->lock_token == "tok-A");

  QB_CHECK(qbrain::jobs::complete_job(b, job->id, "tok-A", R"({"ok":1})"));
  auto done = qbrain::jobs::get_job(b, job->id);
  QB_CHECK(done.has_value());
  QB_CHECK(done->status == "completed");
  QB_CHECK(done->lock_token.empty());
  QB_CHECK(done->lock_until.empty());
  QB_CHECK(!qbrain::jobs::complete_job(b, job->id, "tok-A", "{}"));
  QB_CHECK(!qbrain::jobs::cancel_job(b, job->id));

  // Matching-token failure is terminal. The progress-facing column stays bounded,
  // while the internal result retains the full diagnostic used by existing workers.
  auto failed_id = qbrain::jobs::submit_job(b, "extract_facts", R"({"slug":"bad"})");
  auto to_fail = qbrain::jobs::claim_job(b, "tok-F", 30000);
  QB_CHECK(to_fail.has_value());
  QB_CHECK(to_fail->id == failed_id);
  std::string long_error(500, 'E');
  long_error += "-UNBOUNDED-TRAIL";
  QB_CHECK(!qbrain::jobs::fail_job(b, failed_id, "tok-wrong", long_error));
  QB_CHECK(qbrain::jobs::fail_job(b, failed_id, "tok-F", long_error));
  auto failed = qbrain::jobs::get_job(b, failed_id);
  QB_CHECK(failed.has_value());
  QB_CHECK(failed->status == "failed");
  QB_CHECK(failed->error_text.size() == 500);
  QB_CHECK(failed->error_text == long_error.substr(0, 500));
  QB_CHECK(failed->result_json.find("UNBOUNDED-TRAIL") != std::string::npos);
  QB_CHECK(failed->lock_token.empty());
  QB_CHECK(failed->lock_until.empty());
  QB_CHECK(!qbrain::jobs::cancel_job(b, failed_id));
  QB_CHECK(!qbrain::jobs::fail_job(b, failed_id, "tok-F", "again"));

  // Cancel also fences an active job and terminal rows cannot be cancelled twice.
  auto active_cancel_id =
      qbrain::jobs::submit_job(b, "embed", R"({"page_id":99})", "cancel-active");
  auto active_cancel =
      qbrain::jobs::claim_job(b, "tok-C", 30000, "cancel-active");
  QB_CHECK(active_cancel.has_value());
  QB_CHECK(active_cancel->id == active_cancel_id);
  QB_CHECK(qbrain::jobs::cancel_job(b, active_cancel_id));
  auto active_cancelled = qbrain::jobs::get_job(b, active_cancel_id);
  QB_CHECK(active_cancelled.has_value());
  QB_CHECK(active_cancelled->status == "cancelled");
  QB_CHECK(active_cancelled->lock_token.empty());
  QB_CHECK(active_cancelled->lock_until.empty());
  QB_CHECK(!qbrain::jobs::cancel_job(b, active_cancel_id));
  QB_CHECK(!qbrain::jobs::complete_job(b, active_cancel_id, "tok-C", "{}"));
  QB_CHECK(!qbrain::jobs::fail_job(b, active_cancel_id, "tok-C", "stale"));

  // cancel waiting
  auto id2 = qbrain::jobs::submit_job(b, "embed", R"({"page_id":1})");
  QB_CHECK(qbrain::jobs::cancel_job(b, id2));
  auto c = qbrain::jobs::get_job(b, id2);
  QB_CHECK(c->status == "cancelled");

  // retry cancelled → waiting
  QB_CHECK(qbrain::jobs::retry_job(b, id2));
  auto r = qbrain::jobs::get_job(b, id2);
  QB_CHECK(r->status == "waiting");

  // pause waiting → paused
  QB_CHECK(qbrain::jobs::pause_job(b, id2));
  auto paused = qbrain::jobs::get_job(b, id2);
  QB_CHECK(paused->status == "paused");
  auto prog = qbrain::jobs::get_job_progress(b, id2);
  QB_CHECK(prog.has_value());
  QB_CHECK(prog->status == "paused");
  QB_CHECK(prog->id == id2);

  // resume paused → waiting
  QB_CHECK(qbrain::jobs::resume_job(b, id2));
  auto resumed = qbrain::jobs::get_job(b, id2);
  QB_CHECK(resumed->status == "waiting");
  QB_CHECK(qbrain::jobs::cancel_job(b, id2));

  // pause active clears lock
  auto id3 = qbrain::jobs::submit_job(b, "embed", R"({"page_id":2})");
  auto claimed = qbrain::jobs::claim_job(b, "tok-P", 30000);
  QB_CHECK(claimed.has_value());
  QB_CHECK(claimed->id == id3);
  QB_CHECK(qbrain::jobs::pause_job(b, id3));
  auto p3 = qbrain::jobs::get_job(b, id3);
  QB_CHECK(p3->status == "paused");
  QB_CHECK(p3->lock_token.empty());
  QB_CHECK(p3->lock_until.empty());
  QB_CHECK(!qbrain::jobs::pause_job(b, id3));
  QB_CHECK(!qbrain::jobs::resume_job(b, job->id));

  // Expired leases are requeued once, clear the stale fence, and account for one retry.
  auto reclaim_id =
      qbrain::jobs::submit_job(b, "embed", R"({"page_id":7})", "reclaim-test");
  auto reclaim_claim =
      qbrain::jobs::claim_job(b, "tok-old", 30000, "reclaim-test");
  QB_CHECK(reclaim_claim.has_value());
  QB_CHECK(reclaim_claim->id == reclaim_id);
  const int attempts_after_first_claim = reclaim_claim->attempts;
  auto expire = b.db().prepare(
      "UPDATE jobs SET lock_until=datetime('now','-5 seconds') WHERE id=?");
  expire.bind_int(1, reclaim_id);
  expire.step_done();
  QB_CHECK(qbrain::jobs::reclaim_stalled(b, "reclaim-test") == 1);
  auto reclaimed = qbrain::jobs::get_job(b, reclaim_id);
  QB_CHECK(reclaimed.has_value());
  QB_CHECK(reclaimed->status == "waiting");
  QB_CHECK(reclaimed->lock_token.empty());
  QB_CHECK(reclaimed->lock_until.empty());
  QB_CHECK(reclaimed->attempts == attempts_after_first_claim + 1);
  QB_CHECK(qbrain::jobs::reclaim_stalled(b, "reclaim-test") == 0);
  QB_CHECK(qbrain::jobs::get_job(b, reclaim_id)->attempts == reclaimed->attempts);
  QB_CHECK(!qbrain::jobs::complete_job(b, reclaim_id, "tok-old", "{}"));
  QB_CHECK(!qbrain::jobs::fail_job(b, reclaim_id, "tok-old", "stale"));

  auto reclaimed_again =
      qbrain::jobs::claim_job(b, "tok-new", 30000, "reclaim-test");
  QB_CHECK(reclaimed_again.has_value());
  QB_CHECK(reclaimed_again->id == reclaim_id);
  QB_CHECK(reclaimed_again->attempts == reclaimed->attempts + 1);
  QB_CHECK(!qbrain::jobs::complete_job(b, reclaim_id, "tok-old", "{}"));
  auto fenced_again = qbrain::jobs::get_job(b, reclaim_id);
  QB_CHECK(fenced_again->status == "active");
  QB_CHECK(fenced_again->lock_token == "tok-new");
  QB_CHECK(qbrain::jobs::complete_job(b, reclaim_id, "tok-new", "{}"));

  // Separate connections racing the same queue produce one winner; SQLite busy is a valid loser.
  auto concurrent_id =
      qbrain::jobs::submit_job(b, "embed", R"({"page_id":8})", "concurrent-test");
  qbrain::Brain b2("minions_test_second_connection");
  b2.open_at(qbrain::util::path_to_utf8(dbp));
  std::atomic<int> ready{0};
  std::atomic<bool> start{false};
  std::optional<qbrain::jobs::Job> concurrent_a;
  std::optional<qbrain::jobs::Job> concurrent_b;
  std::exception_ptr unexpected_a;
  std::exception_ptr unexpected_b;
  bool busy_a = false;
  bool busy_b = false;
  std::string sqlite_error_a;
  std::string sqlite_error_b;
  auto race_claim = [&](qbrain::Brain& brain, const char* token,
                        std::optional<qbrain::jobs::Job>& out,
                        std::exception_ptr& unexpected, bool& busy,
                        std::string& sqlite_error) {
    ready.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    try {
      out = qbrain::jobs::claim_job(brain, token, 30000, "concurrent-test");
    } catch (const std::exception& e) {
      const std::string message = e.what();
      busy = message.find("locked") != std::string::npos ||
             message.find("busy") != std::string::npos;
      if (busy)
        sqlite_error = message;
      else
        unexpected = std::current_exception();
    }
  };
  std::thread thread_a(race_claim, std::ref(b), "tok-race-A", std::ref(concurrent_a),
                       std::ref(unexpected_a), std::ref(busy_a), std::ref(sqlite_error_a));
  std::thread thread_b(race_claim, std::ref(b2), "tok-race-B", std::ref(concurrent_b),
                       std::ref(unexpected_b), std::ref(busy_b), std::ref(sqlite_error_b));
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  thread_a.join();
  thread_b.join();
  if (unexpected_a) std::rethrow_exception(unexpected_a);
  if (unexpected_b) std::rethrow_exception(unexpected_b);
  const int claim_winners = static_cast<int>(concurrent_a.has_value()) +
                            static_cast<int>(concurrent_b.has_value());
  QB_CHECK(claim_winners == 1);
  QB_CHECK((!concurrent_a.has_value() || concurrent_a->id == concurrent_id));
  QB_CHECK((!concurrent_b.has_value() || concurrent_b->id == concurrent_id));
  QB_CHECK(!busy_a || !concurrent_a.has_value());
  QB_CHECK(!busy_b || !concurrent_b.has_value());
  const auto winner_token = concurrent_a ? "tok-race-A" : "tok-race-B";
  const bool loser_busy = concurrent_a ? busy_b : busy_a;
  const auto& loser_error = concurrent_a ? sqlite_error_b : sqlite_error_a;
  std::cout << "[INFO] minions_concurrent_claim winner=" << winner_token
            << " loser=" << (loser_busy ? "sqlite_busy" : "no_job");
  if (loser_busy) std::cout << " error=" << loser_error;
  std::cout << "\n" << std::flush;
  QB_CHECK(qbrain::jobs::complete_job(b, concurrent_id, winner_token, "{}"));

  // N17 replay + messages
  auto id4 = qbrain::jobs::submit_job(b, "embed", R"({"page_id":9})", "n17-replay");
  auto mid = qbrain::jobs::send_job_message(b, id4, "test", R"({"hello":1})");
  QB_CHECK(mid > 0);
  auto msgs = qbrain::jobs::list_job_messages(b, id4, 10);
  QB_CHECK(!msgs.empty());
  QB_CHECK(msgs[0].sender == "test");
  auto replay_source =
      qbrain::jobs::claim_job(b, "tok-n17-replay", 60000, "n17-replay");
  QB_CHECK(replay_source.has_value());
  QB_CHECK(replay_source->id == id4);
  QB_CHECK(qbrain::jobs::complete_job(b, id4, "tok-n17-replay", R"({"ok":true})"));
  auto nid = qbrain::jobs::replay_job(b, id4);
  QB_CHECK(nid > 0);
  QB_CHECK(nid != id4);
  auto nj = qbrain::jobs::get_job(b, nid);
  QB_CHECK(nj.has_value());
  QB_CHECK(nj->status == "waiting");
  QB_CHECK(nj->type == "embed");

  auto counts = qbrain::jobs::count_jobs(b);
  QB_CHECK(counts.paused >= 0);
  QB_CHECK(counts.waiting + counts.active + counts.failed + counts.paused + counts.completed +
               counts.cancelled >=
           2);

  auto snap = b.status_snapshot();
  QB_CHECK(snap.schema_version >= 1);
  QB_CHECK(snap.jobs_waiting + snap.jobs_active + snap.jobs_failed + snap.jobs_paused >= 0);

  auto rem = b.remediate();
  QB_CHECK(rem.default_source);

  auto listed = qbrain::jobs::list_jobs(b, "", 50);
  QB_CHECK(listed.size() >= 2);
}

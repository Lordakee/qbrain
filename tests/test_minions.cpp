#include "qbrain/core/brain.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/util/paths.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>

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

  // wrong token cannot complete
  QB_CHECK(!qbrain::jobs::complete_job(b, job->id, "tok-B", "{}"));
  // empty token cannot complete
  QB_CHECK(!qbrain::jobs::complete_job(b, job->id, "", "{}"));
  // correct token completes
  QB_CHECK(qbrain::jobs::complete_job(b, job->id, "tok-A", R"({"ok":1})"));
  auto done = qbrain::jobs::get_job(b, job->id);
  QB_CHECK(done.has_value());
  QB_CHECK(done->status == "completed");

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

  // N17 replay + messages
  auto id4 = qbrain::jobs::submit_job(b, "embed", R"({"page_id":9})");
  auto mid = qbrain::jobs::send_job_message(b, id4, "test", R"({"hello":1})");
  QB_CHECK(mid > 0);
  auto msgs = qbrain::jobs::list_job_messages(b, id4, 10);
  QB_CHECK(!msgs.empty());
  QB_CHECK(msgs[0].sender == "test");
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

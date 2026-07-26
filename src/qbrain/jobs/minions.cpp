#include "qbrain/jobs/minions.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/util/time_util.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace qbrain::jobs {
namespace {

Job row_to_job(storage::Database::Statement& st) {
  Job j;
  j.id = st.column_int(0);
  j.queue = st.column_text(1);
  j.type = st.column_text(2);
  j.status = st.column_text(3);
  j.payload_json = st.column_text(4);
  j.result_json = st.column_text(5);
  j.priority = static_cast<int>(st.column_int(6));
  j.attempts = static_cast<int>(st.column_int(7));
  j.created_at = st.column_text(8);
  j.updated_at = st.column_text(9);
  j.lock_until = st.column_text(10);
  j.lock_token = st.column_text(11);
  j.error_text = st.column_text(12);
  return j;
}

const char* kSelectCols =
    "id, queue, type, status, payload_json, COALESCE(result_json,''), priority, attempts, "
    "created_at, updated_at, COALESCE(lock_until,''), COALESCE(lock_token,''), "
    "COALESCE(error_text,'')";

bool handle_embed(Brain& brain, Job& job, std::string& result_json, std::string& err) {
  int64_t page_id = 0;
  try {
    auto j = json::parse(job.payload_json);
    page_id = j.at("page_id").get<int64_t>();
  } catch (...) {
    err = "bad payload";
    return false;
  }
  auto chunks = brain.get_chunks(page_id);
  std::vector<Chunk> missing;
  for (auto& c : chunks)
    if (c.embedding.empty()) missing.push_back(c);
  int done = 0;
  if (!missing.empty()) {
    std::vector<std::string> texts;
    for (auto& c : missing) texts.push_back(c.text);
    auto er = ai::embed_texts(brain.config(), texts);
    if (!er.ok) {
      err = er.error;
      return false;
    }
    for (size_t i = 0; i < missing.size() && i < er.vectors.size(); ++i) {
      brain.update_chunk_embedding(missing[i].id, er.vectors[i], er.model);
      ++done;
    }
  }
  result_json = json({{"chunks", done}}).dump();
  return true;
}

bool handle_extract_facts(Brain& brain, Job& job, std::string& result_json, std::string& err) {
  std::string slug;
  std::string source_id = "default";
  try {
    auto j = json::parse(job.payload_json);
    slug = j.value("slug", "");
    source_id = j.value("source_id", "default");
  } catch (...) {
    err = "bad payload";
    return false;
  }
  if (slug.empty()) {
    err = "slug required";
    return false;
  }
  int n = brain.extract_facts_from_page(slug, source_id);
  result_json = json({{"facts", n}, {"slug", slug}}).dump();
  return true;
}

}  // namespace

int64_t submit_job(Brain& brain, const std::string& type, const std::string& payload_json,
                   const std::string& queue, int priority) {
  auto st = brain.db().prepare(
      "INSERT INTO jobs(queue, type, status, payload_json, priority) VALUES(?,?,'waiting',?,?)");
  st.bind_text(1, queue);
  st.bind_text(2, type);
  st.bind_text(3, payload_json.empty() ? "{}" : payload_json);
  st.bind_int(4, priority);
  st.step_done();
  return brain.db().last_insert_rowid();
}

std::optional<Job> claim_job(Brain& brain, const std::string& lock_token, int lock_ms,
                             const std::string& queue, const std::vector<std::string>& types) {
  // Token fence: empty token cannot claim (workers must identify themselves).
  if (lock_token.empty()) return std::nullopt;
  reclaim_stalled(brain, queue);
  std::ostringstream sql;
  sql << "SELECT id FROM jobs WHERE queue=? AND status='waiting'";
  if (!types.empty()) {
    sql << " AND type IN (";
    for (size_t i = 0; i < types.size(); ++i) {
      if (i) sql << ",";
      sql << "?";
    }
    sql << ")";
  }
  sql << " ORDER BY priority ASC, id ASC LIMIT 1";
  auto st = brain.db().prepare(sql.str());
  int idx = 1;
  st.bind_text(idx++, queue);
  for (auto& t : types) st.bind_text(idx++, t);
  if (!st.step()) return std::nullopt;
  int64_t id = st.column_int(0);

  std::string mod = "+" + std::to_string(std::max(1, lock_ms / 1000)) + " seconds";
  auto u = brain.db().prepare(
      "UPDATE jobs SET status='active', lock_token=?, lock_until=datetime('now', ?), "
      "attempts=attempts+1, updated_at=?, error_text=NULL WHERE id=? AND status='waiting'");
  u.bind_text(1, lock_token);
  u.bind_text(2, mod);
  u.bind_text(3, util::utc_now());
  u.bind_int(4, id);
  u.step_done();
  if (brain.db().changes() == 0) return std::nullopt;
  return get_job(brain, id);
}

bool complete_job(Brain& brain, int64_t job_id, const std::string& lock_token,
                  const std::string& result_json) {
  // Strict token fence: must match the claim token (no empty/NULL bypass).
  if (lock_token.empty()) return false;
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='completed', result_json=?, lock_token=NULL, lock_until=NULL, "
      "updated_at=?, error_text=NULL WHERE id=? AND status='active' AND lock_token=?");
  st.bind_text(1, result_json);
  st.bind_text(2, util::utc_now());
  st.bind_int(3, job_id);
  st.bind_text(4, lock_token);
  st.step_done();
  return brain.db().changes() > 0;
}

bool fail_job(Brain& brain, int64_t job_id, const std::string& lock_token,
              const std::string& error_text) {
  if (lock_token.empty()) return false;
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='failed', error_text=?, result_json=?, lock_token=NULL, "
      "lock_until=NULL, updated_at=? WHERE id=? AND status='active' AND lock_token=?");
  st.bind_text(1, error_text.substr(0, 500));
  st.bind_text(2, json({{"error", error_text}}).dump());
  st.bind_text(3, util::utc_now());
  st.bind_int(4, job_id);
  st.bind_text(5, lock_token);
  st.step_done();
  return brain.db().changes() > 0;
}

bool cancel_job(Brain& brain, int64_t job_id) {
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='cancelled', lock_token=NULL, lock_until=NULL, updated_at=? "
      "WHERE id=? AND status IN ('waiting','active')");
  st.bind_text(1, util::utc_now());
  st.bind_int(2, job_id);
  st.step_done();
  return brain.db().changes() > 0;
}

bool retry_job(Brain& brain, int64_t job_id) {
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='waiting', lock_token=NULL, lock_until=NULL, error_text=NULL, "
      "updated_at=? WHERE id=? AND status IN ('failed','cancelled','dead')");
  st.bind_text(1, util::utc_now());
  st.bind_int(2, job_id);
  st.step_done();
  return brain.db().changes() > 0;
}

bool pause_job(Brain& brain, int64_t job_id) {
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='paused', lock_token=NULL, lock_until=NULL, updated_at=? "
      "WHERE id=? AND status IN ('waiting','active')");
  st.bind_text(1, util::utc_now());
  st.bind_int(2, job_id);
  st.step_done();
  return brain.db().changes() > 0;
}

bool resume_job(Brain& brain, int64_t job_id) {
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='waiting', lock_token=NULL, lock_until=NULL, updated_at=? "
      "WHERE id=? AND status='paused'");
  st.bind_text(1, util::utc_now());
  st.bind_int(2, job_id);
  st.step_done();
  return brain.db().changes() > 0;
}

int reclaim_stalled(Brain& brain, const std::string& queue) {
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='waiting', lock_token=NULL, lock_until=NULL, updated_at=? "
      "WHERE queue=? AND status='active' AND lock_until IS NOT NULL AND lock_until < datetime('now')");
  st.bind_text(1, util::utc_now());
  st.bind_text(2, queue);
  st.step_done();
  return brain.db().changes();
}

std::optional<Job> get_job(Brain& brain, int64_t job_id) {
  std::string sql = std::string("SELECT ") + kSelectCols + " FROM jobs WHERE id=?";
  auto st = brain.db().prepare(sql);
  st.bind_int(1, job_id);
  if (!st.step()) return std::nullopt;
  return row_to_job(st);
}

std::vector<Job> list_jobs(Brain& brain, const std::string& status, int limit) {
  std::vector<Job> out;
  std::string sql = std::string("SELECT ") + kSelectCols + " FROM jobs";
  if (!status.empty()) sql += " WHERE status=?";
  sql += " ORDER BY id DESC LIMIT ?";
  auto st = brain.db().prepare(sql);
  int idx = 1;
  if (!status.empty()) st.bind_text(idx++, status);
  st.bind_int(idx, limit);
  while (st.step()) out.push_back(row_to_job(st));
  return out;
}

std::optional<JobProgress> get_job_progress(Brain& brain, int64_t job_id) {
  auto j = get_job(brain, job_id);
  if (!j) return std::nullopt;
  JobProgress p;
  p.id = j->id;
  p.type = j->type;
  p.status = j->status;
  p.attempts = j->attempts;
  p.lock_until = j->lock_until;
  p.error_text = j->error_text;
  return p;
}

JobCounts count_jobs(Brain& brain) {
  JobCounts c;
  auto st = brain.db().prepare("SELECT status, COUNT(*) FROM jobs GROUP BY status");
  while (st.step()) {
    auto status = st.column_text(0);
    int64_t n = st.column_int(1);
    if (status == "waiting")
      c.waiting = n;
    else if (status == "active")
      c.active = n;
    else if (status == "failed")
      c.failed = n;
    else if (status == "paused")
      c.paused = n;
    else if (status == "completed")
      c.completed = n;
    else if (status == "cancelled")
      c.cancelled = n;
  }
  return c;
}

bool process_one(Brain& brain, const std::string& worker_token) {
  // Guarantee non-empty claim token (audit P1-1).
  std::string token = worker_token.empty() ? "worker-default" : worker_token;
  static const std::vector<std::string> types = {"embed", "extract_facts"};
  auto job = claim_job(brain, token, 60000, "default", types);
  if (!job) return false;
  std::string result;
  std::string err;
  bool ok = false;
  if (job->type == "embed")
    ok = handle_embed(brain, *job, result, err);
  else if (job->type == "extract_facts")
    ok = handle_extract_facts(brain, *job, result, err);
  else {
    err = "unknown job type";
    ok = false;
  }
  if (ok)
    complete_job(brain, job->id, token, result.empty() ? "{}" : result);
  else
    fail_job(brain, job->id, token, err.empty() ? "failed" : err);
  return true;
}

int drain_jobs(Brain& brain, int max_jobs, const std::string& worker_token) {
  std::string token = worker_token.empty() ? "worker-default" : worker_token;
  int n = 0;
  for (int i = 0; i < max_jobs; ++i) {
    if (!process_one(brain, token)) break;
    ++n;
  }
  return n;
}

}  // namespace qbrain::jobs

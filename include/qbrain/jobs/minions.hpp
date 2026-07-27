#pragma once
#include "qbrain/core/brain.hpp"
#include <optional>
#include <string>
#include <vector>

namespace qbrain::jobs {

struct Job {
  int64_t id = 0;
  std::string queue = "default";
  std::string type;
  std::string status = "waiting";
  std::string payload_json = "{}";
  std::string result_json;
  std::string error_text;
  int priority = 100;
  int attempts = 0;
  std::string lock_token;
  std::string lock_until;
  std::string created_at;
  std::string updated_at;
};

// Enqueue a waiting job. Returns job id.
int64_t submit_job(Brain& brain, const std::string& type, const std::string& payload_json,
                   const std::string& queue = "default", int priority = 100);

// Claim next waiting job matching types (empty types = any). Token-fenced.
std::optional<Job> claim_job(Brain& brain, const std::string& lock_token, int lock_ms = 30000,
                             const std::string& queue = "default",
                             const std::vector<std::string>& types = {});

bool complete_job(Brain& brain, int64_t job_id, const std::string& lock_token,
                  const std::string& result_json = "{}");
bool fail_job(Brain& brain, int64_t job_id, const std::string& lock_token,
              const std::string& error_text);
bool cancel_job(Brain& brain, int64_t job_id);
// Requeue failed/cancelled/dead → waiting (N13).
bool retry_job(Brain& brain, int64_t job_id);

// N17: clone job to a new waiting row (keeps original). Returns new id or 0.
int64_t replay_job(Brain& brain, int64_t job_id);

struct JobMessage {
  int64_t id = 0;
  int64_t job_id = 0;
  std::string sender;
  std::string payload_json;
  std::string created_at;
};
int64_t send_job_message(Brain& brain, int64_t job_id, const std::string& sender,
                         const std::string& payload_json);
std::vector<JobMessage> list_job_messages(Brain& brain, int64_t job_id, int limit = 50);

// N14: waiting|active → paused (clears lock); paused → waiting.
bool pause_job(Brain& brain, int64_t job_id);
bool resume_job(Brain& brain, int64_t job_id);

// Requeue active jobs whose lock_until expired.
int reclaim_stalled(Brain& brain, const std::string& queue = "default");

std::optional<Job> get_job(Brain& brain, int64_t job_id);
std::vector<Job> list_jobs(Brain& brain, const std::string& status = "", int limit = 50);

struct JobProgress {
  int64_t id = 0;
  std::string type;
  std::string status;
  int attempts = 0;
  std::string lock_until;
  std::string error_text;
};
std::optional<JobProgress> get_job_progress(Brain& brain, int64_t job_id);

struct JobCounts {
  int64_t waiting = 0;
  int64_t active = 0;
  int64_t failed = 0;
  int64_t paused = 0;
  int64_t completed = 0;
  int64_t cancelled = 0;
};
JobCounts count_jobs(Brain& brain);

// Run one claimed job through built-in handlers (embed, extract_facts).
// Returns true if a job was processed.
bool process_one(Brain& brain, const std::string& worker_token = "worker-local");

// Drain up to max_jobs via claim/complete (includes embed + extract_facts).
int drain_jobs(Brain& brain, int max_jobs = 20, const std::string& worker_token = "worker-local");

}  // namespace qbrain::jobs

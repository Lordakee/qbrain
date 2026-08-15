#pragma once
#include "qbrain/core/brain.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qbrain::jobs {

inline constexpr std::size_t kJobMessageSenderMaxBytes = 128;
inline constexpr std::size_t kJobMessagePayloadMaxBytes = 65536;
inline constexpr int kJobMessageDefaultLimit = 50;
inline constexpr int kJobMessageMaxLimit = 200;

enum class JobOperationStatus { success, invalid_argument, not_found, invalid_state };
enum class JobInputField { none, job_id, sender, payload_json };

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

struct ReplayJobResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  JobInputField field = JobInputField::none;
  int64_t original_id = 0;
  int64_t new_id = 0;
};

// N17: atomically clone a failed/completed job to a fresh waiting row.
// SQLite failures propagate to the caller for structured error mapping.
ReplayJobResult replay_job_checked(Brain& brain, int64_t job_id);

// Compatibility wrapper. Returns the new id on success and 0 for domain rejections.
int64_t replay_job(Brain& brain, int64_t job_id);

struct JobMessage {
  int64_t id = 0;
  int64_t job_id = 0;
  std::string sender;
  std::string payload_json;
  std::string created_at;
};

struct SendJobMessageResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  JobInputField field = JobInputField::none;
  int64_t message_id = 0;
};

SendJobMessageResult send_job_message_checked(Brain& brain, int64_t job_id,
                                               const std::string& sender,
                                               const std::string& payload_json);

// Compatibility wrapper. Empty sender/payload are invalid; handlers supply omitted defaults.
int64_t send_job_message(Brain& brain, int64_t job_id, const std::string& sender,
                         const std::string& payload_json);

struct ListJobMessagesResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  JobInputField field = JobInputField::none;
  std::vector<JobMessage> messages;
};

ListJobMessagesResult list_job_messages_checked(Brain& brain, int64_t job_id,
                                                int limit = kJobMessageDefaultLimit);

// Compatibility wrapper. Missing/invalid ids collapse to an empty list.
std::vector<JobMessage> list_job_messages(Brain& brain, int64_t job_id,
                                          int limit = kJobMessageDefaultLimit);

// N14: waiting|active → paused (clears lock); paused → waiting.
bool pause_job(Brain& brain, int64_t job_id);
bool resume_job(Brain& brain, int64_t job_id);

// Requeue active jobs whose lock_until expired, clearing the fence and recording one retry.
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

#pragma once
#include "qbrain/core/brain.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

// --- N34-A: lifecycle (D2) -------------------------------------------------
// Bounded parent/child hierarchy (plan N34): fanout <= 8 children per parent,
// tree depth <= 2 (parent -> children, no grandchildren). jobs.status gains
// one documented value, "waiting_children" (schema v13): a parent that has
// spawned children and is waiting for all of them to reach a terminal status.
// Depth-0 / legacy jobs (parent_id NULL, depth 0) take the exact N12 paths;
// none of the functions below alter behavior for them.

inline constexpr std::size_t kJobMaxChildFanout = 8;
// Child payload bound reuses the existing job-message payload bound.
inline constexpr std::size_t kJobChildPayloadMaxBytes = kJobMessagePayloadMaxBytes;
inline constexpr std::size_t kJobAggregateErrorMaxEntries = 8;

struct ChildSpec {
  std::string type;                   // required, non-empty
  std::string payload_json = "{}";    // valid JSON, <= kJobChildPayloadMaxBytes
  std::string queue;                  // empty -> inherit the parent's queue
  int priority = 100;
};

struct SpawnChildrenResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  int64_t parent_id = 0;
  // Machine-readable rejection reason: "parent_id", "children", "fanout",
  // "depth", "parent_status", "children.type", "children.payload_json".
  std::string reason;
  // Child ids in the same order as the spec span (empty unless success).
  std::vector<int64_t> child_ids;
};

// Atomically (single transaction) move a waiting depth-0 parent to
// waiting_children and insert its waiting children (depth 1). Rejects:
// unknown parent (not_found); parent that is itself a child (invalid_argument
// "depth"); empty span or fanout > kJobMaxChildFanout (invalid_argument
// "children"/"fanout"); parent not in status waiting (invalid_state
// "parent_status"); bad child type/payload (invalid_argument). On any
// rejection no rows are written.
SpawnChildrenResult spawn_children(Brain& brain, int64_t parent_id,
                                   std::span<const ChildSpec> children);

struct AggregateResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  std::string reason;             // "parent_id"|"not_parent"|"not_ready"|...
  bool ready = false;             // all children terminal at call time
  bool aggregated = false;        // this call performed the transition
  std::string aggregate_json;     // canonical aggregation (computed or stored)
};

// Idempotent parent completion: when every child of a waiting_children parent
// has reached a terminal status (completed/failed/cancelled/dead), flip the
// parent to completed and store the deterministic aggregation JSON in
// result_json (schema per plan P2-2):
//   {"child_counts":{"cancelled":K,"completed":N,"failed":M},
//    "errors":[{"child_id":X,"error":"..."}],  // <=8, child_id ascending
//    "order":"child_id"}
// Re-calling after completion is a successful no-op returning the stored JSON.
// Uses the N34-B exactly-once fence; the guarded status transition keeps the
// write single even if a fence was consumed by a crashed attempt.
AggregateResult aggregate_if_ready(Brain& brain, int64_t parent_id);

// Deterministic aggregation body. Returns nullopt when parent_id is unknown,
// the job has no children, or any child is non-terminal. Byte-identical for
// identical child states, independent of call order or time.
std::optional<std::string> compute_aggregate_json(Brain& brain, int64_t parent_id);

struct JobHierarchy {
  int64_t id = 0;
  int64_t parent_id = 0;   // 0 = root/leaf (NULL column)
  int depth = 0;
  int64_t child_count = 0;
  std::vector<Job> children;  // ordered by id ascending (bounded by fanout)
};

// Hierarchy view for get_job integration (D4). nullopt when the id is unknown.
std::optional<JobHierarchy> get_job_hierarchy(Brain& brain, int64_t job_id);

struct CancelTreeResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  std::string reason;
  bool cancelled = false;         // the targeted job itself was cancelled
  int64_t cancelled_children = 0; // propagation (parents only)
};

// Tree-aware cancellation (N34): cancelling a parent cancels every
// non-terminal child in the same transaction; cancelling a child/leaf behaves
// exactly like the N12 cancel_job and never touches siblings.
CancelTreeResult cancel_job_tree(Brain& brain, int64_t job_id);

struct RetryTreeResult {
  JobOperationStatus status = JobOperationStatus::invalid_argument;
  std::string reason;  // "non_terminal_children"|"parent_not_retryable"|...
  bool requeued = false;
};

// Tree-aware retry (N34): retry is leaf-only. A parent is rejected — with
// reason "non_terminal_children" while any child is non-terminal, else
// "parent_not_retryable". A leaf/child keeps the exact N12 retry semantics
// (failed/cancelled/dead -> waiting).
RetryTreeResult retry_job_hierarchy(Brain& brain, int64_t job_id);

// --- N34-B: fence/aggregate-atomicity (D3) ---
// Exactly-once parent aggregation guard (N34). Returns true exactly once per
// parent id: an idempotent fence row (INSERT OR IGNORE keyed on parent_id)
// inside the completing transaction, so concurrent completers of the last
// children see exactly one true and the parent aggregates exactly once.
// The N12 single-job token fence semantics are unchanged.
bool try_begin_aggregation(Brain& brain, int64_t parent_id);

}  // namespace qbrain::jobs

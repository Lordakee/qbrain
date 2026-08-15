#include "qbrain/jobs/minions.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/util/time_util.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <sstream>
#include <string_view>

using json = nlohmann::json;

namespace qbrain::jobs {
namespace {

constexpr size_t kMaxErrorTextBytes = 500;

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

size_t utf8_sequence_length(std::string_view value, size_t offset) {
  const auto byte_at = [&](size_t index) {
    return static_cast<unsigned char>(value[index]);
  };
  const size_t remaining = value.size() - offset;
  const unsigned char first = byte_at(offset);
  if (first <= 0x7Fu) return 1;
  if (first >= 0xC2u && first <= 0xDFu) {
    return remaining >= 2 && is_utf8_continuation(byte_at(offset + 1)) ? 2 : 0;
  }
  if (first == 0xE0u) {
    return remaining >= 3 && byte_at(offset + 1) >= 0xA0u && byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if (first >= 0xE1u && first <= 0xECu) {
    return remaining >= 3 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if (first == 0xEDu) {
    return remaining >= 3 && byte_at(offset + 1) >= 0x80u && byte_at(offset + 1) <= 0x9Fu &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if (first >= 0xEEu && first <= 0xEFu) {
    return remaining >= 3 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2))
               ? 3
               : 0;
  }
  if (first == 0xF0u) {
    return remaining >= 4 && byte_at(offset + 1) >= 0x90u && byte_at(offset + 1) <= 0xBFu &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  }
  if (first >= 0xF1u && first <= 0xF3u) {
    return remaining >= 4 && is_utf8_continuation(byte_at(offset + 1)) &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  }
  if (first == 0xF4u) {
    return remaining >= 4 && byte_at(offset + 1) >= 0x80u && byte_at(offset + 1) <= 0x8Fu &&
                   is_utf8_continuation(byte_at(offset + 2)) &&
                   is_utf8_continuation(byte_at(offset + 3))
               ? 4
               : 0;
  }
  return 0;
}

bool is_valid_utf8(std::string_view value) {
  size_t offset = 0;
  while (offset < value.size()) {
    const size_t length = utf8_sequence_length(value, offset);
    if (length == 0) return false;
    offset += length;
  }
  return true;
}

bool is_valid_message_sender(std::string_view sender) {
  if (sender.empty() || sender.size() > kJobMessageSenderMaxBytes || !is_valid_utf8(sender)) {
    return false;
  }
  for (char value : sender) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte <= 0x1Fu || byte == 0x7Fu) return false;
  }
  return true;
}

std::optional<std::string> canonical_message_payload(std::string_view payload_json) {
  if (payload_json.empty() || payload_json.size() > kJobMessagePayloadMaxBytes ||
      !is_valid_utf8(payload_json)) {
    return std::nullopt;
  }
  try {
    auto parsed = json::parse(payload_json.begin(), payload_json.end());
    auto canonical = parsed.dump();
    if (canonical.size() > kJobMessagePayloadMaxBytes) return std::nullopt;
    return canonical;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

std::string bounded_utf8(std::string_view value) {
  std::string out;
  out.reserve(std::min(value.size(), kMaxErrorTextBytes));
  size_t offset = 0;
  while (offset < value.size()) {
    const size_t length = utf8_sequence_length(value, offset);
    if (length == 0) {
      constexpr std::string_view replacement = "\xEF\xBF\xBD";
      if (out.size() + replacement.size() > kMaxErrorTextBytes) break;
      out.append(replacement);
      ++offset;
      continue;
    }
    if (out.size() + length > kMaxErrorTextBytes) break;
    out.append(value, offset, length);
    offset += length;
  }
  return out;
}

bool has_bare_secret_key(std::string_view lower) {
  size_t offset = 0;
  while ((offset = lower.find("sk-", offset)) != std::string_view::npos) {
    const bool at_boundary = offset == 0 ||
                             !((lower[offset - 1] >= 'a' && lower[offset - 1] <= 'z') ||
                               (lower[offset - 1] >= '0' && lower[offset - 1] <= '9') ||
                               lower[offset - 1] == '_' || lower[offset - 1] == '-');
    if (at_boundary && offset + 3 < lower.size() && lower[offset + 3] != ' ') return true;
    offset += 3;
  }
  return false;
}

std::string safe_progress_error(std::string error) {
  auto lower = error;
  for (char& c : lower) {
    const auto u = static_cast<unsigned char>(c);
    if (u >= 'A' && u <= 'Z') c = static_cast<char>(u - 'A' + 'a');
  }
  static const char* markers[] = {"api_key", "api-key", "api key", "apikey", "authorization",
                                  "bearer ", "password", "secret", "token", "key=", "https://",
                                  "http://", "provider", "model"};
  for (auto* marker : markers) {
    if (lower.find(marker) != std::string::npos) return "[redacted]";
  }
  if (has_bare_secret_key(lower)) return "[redacted]";
  return bounded_utf8(error);
}

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
  const auto bounded_error = bounded_utf8(error_text);
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='failed', error_text=?, result_json=?, lock_token=NULL, "
      "lock_until=NULL, updated_at=? WHERE id=? AND status='active' AND lock_token=?");
  st.bind_text(1, bounded_error);
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

ReplayJobResult replay_job_checked(Brain& brain, int64_t job_id) {
  ReplayJobResult result;
  result.original_id = job_id;
  if (job_id <= 0) {
    result.field = JobInputField::job_id;
    return result;
  }

  auto& database = brain.db();
  // Avoid creating sqlite_sequence=0 for a missing first-ever job while the
  // guarded INSERT still evaluates id and status inside the same transaction.
  database.exec("SAVEPOINT qbrain_replay_job;");
  bool savepoint_active = true;
  try {
    auto source = database.prepare("SELECT status FROM jobs WHERE id=?");
    source.bind_int(1, job_id);
    if (!source.step()) {
      database.exec("RELEASE qbrain_replay_job;");
      savepoint_active = false;
      result.status = JobOperationStatus::not_found;
      return result;
    }
    const auto source_status = source.column_text(0);
    if (source_status != "failed" && source_status != "completed") {
      database.exec("RELEASE qbrain_replay_job;");
      savepoint_active = false;
      result.status = JobOperationStatus::invalid_state;
      return result;
    }

    auto insert = database.prepare(
        "INSERT INTO jobs(queue, type, payload_json, priority) "
        "SELECT queue, type, payload_json, priority FROM jobs "
        "WHERE id=? AND status IN ('failed','completed')");
    insert.bind_int(1, job_id);
    insert.step_done();
    if (database.changes() == 0) {
      database.exec("ROLLBACK TO qbrain_replay_job;RELEASE qbrain_replay_job;");
      savepoint_active = false;
      result.status = JobOperationStatus::invalid_state;
      return result;
    }

    result.new_id = database.last_insert_rowid();
    database.exec("RELEASE qbrain_replay_job;");
    savepoint_active = false;
    result.status = JobOperationStatus::success;
    return result;
  } catch (...) {
    if (savepoint_active) {
      try {
        database.exec("ROLLBACK TO qbrain_replay_job;RELEASE qbrain_replay_job;");
      } catch (...) {
      }
    }
    throw;
  }
}

int64_t replay_job(Brain& brain, int64_t job_id) {
  auto result = replay_job_checked(brain, job_id);
  return result.status == JobOperationStatus::success ? result.new_id : 0;
}

SendJobMessageResult send_job_message_checked(Brain& brain, int64_t job_id,
                                               const std::string& sender,
                                               const std::string& payload_json) {
  SendJobMessageResult result;
  if (job_id <= 0) {
    result.field = JobInputField::job_id;
    return result;
  }
  if (!is_valid_message_sender(sender)) {
    result.field = JobInputField::sender;
    return result;
  }
  auto canonical_payload = canonical_message_payload(payload_json);
  if (!canonical_payload) {
    result.field = JobInputField::payload_json;
    return result;
  }

  auto& database = brain.db();
  // A zero-row INSERT on an AUTOINCREMENT table creates sqlite_sequence=0.
  // Keep the parent preflight and guarded insert in one savepoint instead.
  database.exec("SAVEPOINT qbrain_send_job_message;");
  bool savepoint_active = true;
  try {
    auto parent = database.prepare("SELECT 1 FROM jobs WHERE id=?");
    parent.bind_int(1, job_id);
    if (!parent.step()) {
      database.exec("RELEASE qbrain_send_job_message;");
      savepoint_active = false;
      result.status = JobOperationStatus::not_found;
      return result;
    }

    auto insert = database.prepare(
        "INSERT INTO job_messages(job_id, sender, payload_json) "
        "SELECT ?, ?, ? WHERE EXISTS(SELECT 1 FROM jobs WHERE id=?)");
    insert.bind_int(1, job_id);
    insert.bind_text(2, sender);
    insert.bind_text(3, *canonical_payload);
    insert.bind_int(4, job_id);
    insert.step_done();
    if (database.changes() == 0) {
      database.exec("ROLLBACK TO qbrain_send_job_message;"
                    "RELEASE qbrain_send_job_message;");
      savepoint_active = false;
      result.status = JobOperationStatus::not_found;
      return result;
    }

    result.message_id = database.last_insert_rowid();
    database.exec("RELEASE qbrain_send_job_message;");
    savepoint_active = false;
    result.status = JobOperationStatus::success;
    return result;
  } catch (...) {
    if (savepoint_active) {
      try {
        database.exec("ROLLBACK TO qbrain_send_job_message;"
                      "RELEASE qbrain_send_job_message;");
      } catch (...) {
      }
    }
    throw;
  }
}

int64_t send_job_message(Brain& brain, int64_t job_id, const std::string& sender,
                         const std::string& payload_json) {
  auto result = send_job_message_checked(brain, job_id, sender, payload_json);
  return result.status == JobOperationStatus::success ? result.message_id : 0;
}

ListJobMessagesResult list_job_messages_checked(Brain& brain, int64_t job_id, int limit) {
  ListJobMessagesResult result;
  if (job_id <= 0) {
    result.field = JobInputField::job_id;
    return result;
  }

  auto parent = brain.db().prepare("SELECT 1 FROM jobs WHERE id=?");
  parent.bind_int(1, job_id);
  if (!parent.step()) {
    result.status = JobOperationStatus::not_found;
    return result;
  }

  const int effective_limit = std::clamp(limit, 1, kJobMessageMaxLimit);
  auto st = brain.db().prepare(
      "SELECT id, job_id, sender, payload_json, created_at FROM job_messages "
      "WHERE job_id=? ORDER BY id DESC LIMIT ?");
  st.bind_int(1, job_id);
  st.bind_int(2, effective_limit);
  while (st.step()) {
    JobMessage m;
    m.id = st.column_int(0);
    m.job_id = st.column_int(1);
    m.sender = st.column_text(2);
    m.payload_json = st.column_text(3);
    m.created_at = st.column_text(4);
    result.messages.push_back(std::move(m));
  }
  result.status = JobOperationStatus::success;
  return result;
}

std::vector<JobMessage> list_job_messages(Brain& brain, int64_t job_id, int limit) {
  auto result = list_job_messages_checked(brain, job_id, limit);
  if (result.status != JobOperationStatus::success) return {};
  return std::move(result.messages);
}

bool pause_job(Brain& brain, int64_t job_id) {
  if (job_id <= 0) return false;
  auto st = brain.db().prepare(
      "UPDATE jobs SET status='paused', lock_token=NULL, lock_until=NULL, updated_at=? "
      "WHERE id=? AND status IN ('waiting','active')");
  st.bind_text(1, util::utc_now());
  st.bind_int(2, job_id);
  st.step_done();
  return brain.db().changes() > 0;
}

bool resume_job(Brain& brain, int64_t job_id) {
  if (job_id <= 0) return false;
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
      "UPDATE jobs SET status='waiting', lock_token=NULL, lock_until=NULL, "
      "attempts=attempts+1, updated_at=? "
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
  if (job_id <= 0) return std::nullopt;

  // Keep this read surface limited to the fields permitted by the N14
  // progress contract. In particular, never load payload/result/lock_token
  // merely to construct a progress response.
  auto st = brain.db().prepare(
      "SELECT id, type, status, attempts, COALESCE(lock_until,''), "
      "COALESCE(error_text,'') FROM jobs WHERE id=?");
  st.bind_int(1, job_id);
  if (!st.step()) return std::nullopt;

  JobProgress p;
  p.id = st.column_int(0);
  p.type = st.column_text(1);
  p.status = st.column_text(2);
  p.attempts = static_cast<int>(st.column_int(3));
  p.lock_until = st.column_text(4);
  p.error_text = safe_progress_error(st.column_text(5));
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

// --- N34-A: lifecycle (D2) -------------------------------------------------
// Bounded parent/child hierarchy state machine (plan N34 D2). Everything in
// this section is additive: the N12 functions above are untouched and every
// depth-0 / legacy job (parent_id NULL, depth 0) keeps its exact old path.
//
// State machine:
//   parent:  waiting --spawn_children(txn)--> waiting_children
//            waiting_children --aggregate_if_ready(all children terminal)-->
//            completed (result_json carries the deterministic aggregation)
//            waiting/waiting_children/active --cancel_job_tree--> cancelled
//            (+ every non-terminal child cancelled in the same transaction)
//   child:   waiting -> active -> completed|failed|cancelled (N12 fence path)
//            child spawn is rejected: depth stays <= 1 (no grandchildren)
namespace n34a {

bool is_terminal(std::string_view status) {
  return status == "completed" || status == "failed" || status == "cancelled" ||
         status == "dead";
}

struct ParentRow {
  bool found = false;
  std::string status;
  int64_t parent_id = 0;  // 0 = NULL (root)
  int depth = 0;
  std::string queue = "default";
};

ParentRow load_parent(Brain& brain, int64_t job_id) {
  ParentRow row;
  auto st = brain.db().prepare("SELECT status, parent_id, depth, queue FROM jobs WHERE id=?");
  st.bind_int(1, job_id);
  if (st.step()) {
    row.found = true;
    row.status = st.column_text(0);
    row.parent_id = st.column_is_null(1) ? 0 : st.column_int(1);
    row.depth = static_cast<int>(st.column_int(2));
    row.queue = st.column_text(3);
  }
  return row;
}

int64_t count_children(Brain& brain, int64_t parent_id) {
  auto st = brain.db().prepare("SELECT COUNT(*) FROM jobs WHERE parent_id=?");
  st.bind_int(1, parent_id);
  return st.step() ? st.column_int(0) : 0;
}

int64_t count_non_terminal_children(Brain& brain, int64_t parent_id) {
  auto st = brain.db().prepare(
      "SELECT COUNT(*) FROM jobs WHERE parent_id=? AND status NOT IN "
      "('completed','failed','cancelled','dead')");
  st.bind_int(1, parent_id);
  return st.step() ? st.column_int(0) : 0;
}

}  // namespace n34a

SpawnChildrenResult spawn_children(Brain& brain, int64_t parent_id,
                                   std::span<const ChildSpec> children) {
  SpawnChildrenResult r;
  r.parent_id = parent_id;
  if (parent_id <= 0) {
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "parent_id";
    return r;
  }
  if (children.empty()) {
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "children";
    return r;
  }
  if (children.size() > kJobMaxChildFanout) {
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "fanout";
    return r;
  }
  // Validate every child spec (and canonicalize payloads) before opening the
  // transaction: a rejected spec must leave zero rows behind.
  std::vector<std::string> payloads;
  payloads.reserve(children.size());
  for (const auto& spec : children) {
    if (spec.type.empty()) {
      r.status = JobOperationStatus::invalid_argument;
      r.reason = "children.type";
      return r;
    }
    // Child payload bounds reuse the existing job-message payload contract
    // (kJobChildPayloadMaxBytes == kJobMessagePayloadMaxBytes): valid UTF-8
    // JSON, canonical form within the bound; empty defaults to "{}".
    auto canonical =
        canonical_message_payload(spec.payload_json.empty() ? "{}" : spec.payload_json);
    if (!canonical) {
      r.status = JobOperationStatus::invalid_argument;
      r.reason = "children.payload_json";
      return r;
    }
    payloads.push_back(std::move(*canonical));
  }

  const auto parent = n34a::load_parent(brain, parent_id);
  if (!parent.found) {
    r.status = JobOperationStatus::not_found;
    r.reason = "parent_id";
    return r;
  }
  if (parent.parent_id != 0) {
    // This job is itself a child: tree depth stays <= 2 (no grandchildren).
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "depth";
    return r;
  }

  auto& database = brain.db();
  database.exec("BEGIN IMMEDIATE;");
  bool txn_open = true;
  try {
    // Guarded parent transition: only a still-waiting parent may enter
    // waiting_children. A concurrent claim/cancel makes changes()==0 and the
    // whole spawn (children included) rolls back.
    auto flip = database.prepare(
        "UPDATE jobs SET status='waiting_children', lock_token=NULL, "
        "lock_until=NULL, updated_at=? WHERE id=? AND status='waiting'");
    flip.bind_text(1, util::utc_now());
    flip.bind_int(2, parent_id);
    flip.step_done();
    if (database.changes() == 0) {
      database.exec("ROLLBACK;");
      txn_open = false;
      r.status = JobOperationStatus::invalid_state;
      r.reason = "parent_status";
      return r;
    }
    auto insert = database.prepare(
        "INSERT INTO jobs(queue, type, status, payload_json, priority, parent_id, depth) "
        "VALUES(?,?,'waiting',?,?,?,?)");
    const int child_depth = parent.depth + 1;  // == 1 (parent is depth 0)
    for (size_t i = 0; i < children.size(); ++i) {
      // step_done() does not auto-reset: without reset() the re-bind of a
      // statement in DONE state silently fails (SQLITE_MISUSE) and every
      // child after the first would inherit the first child's bindings.
      insert.reset();
      const auto& spec = children[i];
      insert.bind_text(1, spec.queue.empty() ? parent.queue : spec.queue);
      insert.bind_text(2, spec.type);
      insert.bind_text(3, payloads[i]);
      insert.bind_int(4, spec.priority);
      insert.bind_int(5, parent_id);
      insert.bind_int(6, child_depth);
      insert.step_done();
      r.child_ids.push_back(database.last_insert_rowid());
    }
    database.exec("COMMIT;");
    txn_open = false;
  } catch (...) {
    if (txn_open) {
      try {
        database.exec("ROLLBACK;");
      } catch (...) {
      }
    }
    throw;
  }
  r.status = JobOperationStatus::success;
  return r;
}

std::optional<std::string> compute_aggregate_json(Brain& brain, int64_t parent_id) {
  if (parent_id <= 0) return std::nullopt;
  auto& database = brain.db();
  if (n34a::count_children(brain, parent_id) == 0) return std::nullopt;

  // std::map keeps child_counts keys sorted; nlohmann::json serializes map
  // keys in sorted order, so the dump is byte-deterministic.
  std::map<std::string, int64_t> counts;
  {
    auto st = database.prepare(
        "SELECT status, COUNT(*) FROM jobs WHERE parent_id=? GROUP BY status");
    st.bind_int(1, parent_id);
    while (st.step()) counts[st.column_text(0)] = st.column_int(1);
  }
  for (const auto& [status, n] : counts) {
    (void)n;
    if (!n34a::is_terminal(status)) return std::nullopt;  // not ready
  }

  // Error summaries: failed children, child_id ascending, capped at 8.
  // safe_progress_error redacts credential/path-like content and bounds the
  // text, so the aggregate leaks no local paths or secrets.
  json errors = json::array();
  {
    auto st = database.prepare(
        "SELECT id, COALESCE(error_text,'') FROM jobs WHERE parent_id=? AND "
        "status='failed' ORDER BY id ASC");
    st.bind_int(1, parent_id);
    while (st.step() && errors.size() < kJobAggregateErrorMaxEntries) {
      errors.push_back(json{
          {"child_id", st.column_int(0)},
          {"error", safe_progress_error(st.column_text(1))},
      });
    }
  }

  json out;
  out["child_counts"] = counts;
  out["errors"] = errors;
  out["order"] = "child_id";
  return out.dump();
}

AggregateResult aggregate_if_ready(Brain& brain, int64_t parent_id) {
  AggregateResult r;
  if (parent_id <= 0) {
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "parent_id";
    return r;
  }
  const auto parent = n34a::load_parent(brain, parent_id);
  if (!parent.found) {
    r.status = JobOperationStatus::not_found;
    r.reason = "parent_id";
    return r;
  }
  if (n34a::count_children(brain, parent_id) == 0) {
    r.status = JobOperationStatus::invalid_state;
    r.reason = "not_parent";
    return r;
  }
  if (parent.status == "completed") {
    // Idempotent: aggregation already happened; return the stored JSON.
    r.status = JobOperationStatus::success;
    r.ready = true;
    r.aggregated = false;
    r.aggregate_json = [&] {
      auto job = get_job(brain, parent_id);
      return job ? job->result_json : std::string();
    }();
    return r;
  }
  if (parent.status != "waiting_children") {
    r.status = JobOperationStatus::invalid_state;
    r.reason = "parent_status";
    return r;
  }
  auto aggregate = compute_aggregate_json(brain, parent_id);
  if (!aggregate) {
    // Normal intermediate observation while children are still running.
    r.status = JobOperationStatus::invalid_state;
    r.reason = "not_ready";
    return r;
  }

  auto& database = brain.db();
  database.exec("BEGIN IMMEDIATE;");
  bool txn_open = true;
  try {
    // Exactly-once guard (N34-B contract): consumed inside the completing
    // transaction. The guarded UPDATE below stays authoritative: if a prior
    // attempt crashed after consuming the fence but before committing, this
    // self-heals; if another worker already committed, changes()==0 and the
    // deterministic content makes this a harmless no-op.
    (void)try_begin_aggregation(brain, parent_id);
    auto flip = database.prepare(
        "UPDATE jobs SET status='completed', result_json=?, lock_token=NULL, "
        "lock_until=NULL, error_text=NULL, updated_at=? WHERE id=? AND "
        "status='waiting_children'");
    flip.bind_text(1, *aggregate);
    flip.bind_text(2, util::utc_now());
    flip.bind_int(3, parent_id);
    flip.step_done();
    r.aggregated = database.changes() > 0;
    database.exec("COMMIT;");
    txn_open = false;
  } catch (...) {
    if (txn_open) {
      try {
        database.exec("ROLLBACK;");
      } catch (...) {
      }
    }
    throw;
  }
  r.status = JobOperationStatus::success;
  r.ready = true;
  r.aggregate_json = *aggregate;
  return r;
}

std::optional<JobHierarchy> get_job_hierarchy(Brain& brain, int64_t job_id) {
  if (job_id <= 0) return std::nullopt;
  const auto row = n34a::load_parent(brain, job_id);
  if (!row.found) return std::nullopt;
  JobHierarchy h;
  h.id = job_id;
  h.parent_id = row.parent_id;
  h.depth = row.depth;
  h.child_count = n34a::count_children(brain, job_id);
  if (h.child_count > 0) {
    std::string sql = std::string("SELECT ") + kSelectCols +
                      " FROM jobs WHERE parent_id=? ORDER BY id ASC";
    auto st = brain.db().prepare(sql);
    st.bind_int(1, job_id);
    while (st.step()) h.children.push_back(row_to_job(st));
  }
  return h;
}

CancelTreeResult cancel_job_tree(Brain& brain, int64_t job_id) {
  CancelTreeResult r;
  if (job_id <= 0) {
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "job_id";
    return r;
  }
  const auto row = n34a::load_parent(brain, job_id);
  if (!row.found) {
    r.status = JobOperationStatus::not_found;
    r.reason = "job_id";
    return r;
  }
  if (n34a::count_children(brain, job_id) == 0) {
    // Leaf / child: exact N12 semantics; siblings are never touched.
    const bool cancelled = cancel_job(brain, job_id);
    r.status = cancelled ? JobOperationStatus::success : JobOperationStatus::invalid_state;
    r.cancelled = cancelled;
    return r;
  }
  // Parent: cancel the parent (waiting/active/waiting_children) and every
  // non-terminal child in one transaction.
  auto& database = brain.db();
  database.exec("BEGIN IMMEDIATE;");
  bool txn_open = true;
  try {
    auto kids = database.prepare(
        "UPDATE jobs SET status='cancelled', lock_token=NULL, lock_until=NULL, "
        "updated_at=? WHERE parent_id=? AND status IN ('waiting','active')");
    kids.bind_text(1, util::utc_now());
    kids.bind_int(2, job_id);
    kids.step_done();
    r.cancelled_children = database.changes();
    auto self = database.prepare(
        "UPDATE jobs SET status='cancelled', lock_token=NULL, lock_until=NULL, "
        "updated_at=? WHERE id=? AND status IN ('waiting','active','waiting_children')");
    self.bind_text(1, util::utc_now());
    self.bind_int(2, job_id);
    self.step_done();
    r.cancelled = database.changes() > 0;
    database.exec("COMMIT;");
    txn_open = false;
  } catch (...) {
    if (txn_open) {
      try {
        database.exec("ROLLBACK;");
      } catch (...) {
      }
    }
    throw;
  }
  r.status = (r.cancelled || r.cancelled_children > 0) ? JobOperationStatus::success
                                                       : JobOperationStatus::invalid_state;
  return r;
}

RetryTreeResult retry_job_hierarchy(Brain& brain, int64_t job_id) {
  RetryTreeResult r;
  if (job_id <= 0) {
    r.status = JobOperationStatus::invalid_argument;
    r.reason = "job_id";
    return r;
  }
  const auto row = n34a::load_parent(brain, job_id);
  if (!row.found) {
    r.status = JobOperationStatus::not_found;
    r.reason = "job_id";
    return r;
  }
  if (n34a::count_children(brain, job_id) > 0) {
    // Leaf-only retry policy. The plan's mandatory rejection is the
    // non-terminal-children case; an all-terminal parent is still rejected
    // ("parent_not_retryable") because the aggregated parent is not itself
    // executable.
    r.status = JobOperationStatus::invalid_state;
    r.reason = n34a::count_non_terminal_children(brain, job_id) > 0
                   ? "non_terminal_children"
                   : "parent_not_retryable";
    return r;
  }
  // Leaf / child: exact N12 semantics (failed/cancelled/dead -> waiting).
  const bool requeued = retry_job(brain, job_id);
  r.status = requeued ? JobOperationStatus::success : JobOperationStatus::invalid_state;
  r.reason = requeued ? "" : "not_retryable_status";
  r.requeued = requeued;
  return r;
}

// --- N34-B: fence/aggregate-atomicity (D3) ---------------------------------
// Aggregate-once guard for N34 parent fan-out aggregation (plan D3, points 2
// and 3). The N12 single-job claim/complete token fence above is intentionally
// untouched: siblings MAY be claimed by different workers in parallel.
//
// N34-A's child-completion path calls this guard inside the completing
// transaction. The guard is an idempotent fence row keyed on the parent id
// (INSERT OR IGNORE semantics): when the last two children complete
// concurrently on two worker connections, SQLite serializes the two inserts
// and exactly one caller observes changes()>0, so the parent aggregation
// fires EXACTLY once. Every later call (same connection or any other) sees
// the existing row and returns false. The row is never deleted, so the
// exactly-once property survives crash/reopen (P2-1).
bool try_begin_aggregation(Brain& brain, int64_t parent_id) {
  if (parent_id <= 0) return false;
  auto& database = brain.db();
  // The v13 migration normally creates this table; the guarded CREATE keeps
  // the fence self-sufficient and idempotent on any schema state (including
  // a pre-migration v12 brain opened by an older binary alongside a newer
  // one — column/table additions do not break the older reader).
  database.exec(
      "CREATE TABLE IF NOT EXISTS job_aggregation_fence ("
      "parent_id INTEGER NOT NULL PRIMARY KEY, "
      "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
      ");");
  auto st = database.prepare(
      "INSERT OR IGNORE INTO job_aggregation_fence(parent_id) VALUES(?);");
  st.bind_int(1, parent_id);
  st.step_done();
  return database.changes() > 0;
}

}  // namespace qbrain::jobs

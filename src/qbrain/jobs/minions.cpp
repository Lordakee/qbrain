#include "qbrain/jobs/minions.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/util/time_util.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
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

}  // namespace qbrain::jobs

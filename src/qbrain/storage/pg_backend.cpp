#include "qbrain/storage/pg_backend.hpp"

// N38 D1: libpq PostgreSQL backend for the N35 IStorageBackend contract.
// The concrete PgBackend lives in the anonymous namespace below (the same
// structure rule SqliteBackend follows in database.cpp); the exported
// surface is the factory (make_pg_backend) plus the free functions declared
// in pg_backend.hpp. The pure, libpq-free helpers (DSN redaction, '?'
// placeholder translation, canonical DDL text, FTS query normalization) are
// compiled unconditionally so dialect unit tests run on machines without
// PostgreSQL; everything under QBRAIN_WITH_PG requires the discovered libpq.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "qbrain/util/string_util.hpp"
#include "qbrain/util/time_util.hpp"

#ifdef QBRAIN_WITH_PG
#ifdef _WIN32
#include <windows.h>
#endif
#endif

namespace qbrain::storage {

namespace {

// ---------------------------------------------------------------------------
// Pure helpers shared by both halves (no libpq dependency).
// ---------------------------------------------------------------------------

bool is_ident_start(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}
bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string to_lower_ascii(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

// DSN of either accepted form parsed down to the four whitelisted keys plus
// the password (the password is needed by the runtime scrubber and the
// pg_dump child process environment -- never for display).
struct DsnParts {
  std::string host;
  std::string port;
  std::string dbname;
  std::string user;
  std::string password;
  bool uri = false;
};

// libpq keyword/value form: whitespace-separated key=value pairs; values may
// be single-quoted with '' escaping.
DsnParts parse_dsn_keyword_form(std::string_view dsn) {
  DsnParts p;
  size_t i = 0;
  const size_t n = dsn.size();
  while (i < n) {
    while (i < n && std::isspace(static_cast<unsigned char>(dsn[i]))) ++i;
    if (i >= n) break;
    std::string key;
    while (i < n && !std::isspace(static_cast<unsigned char>(dsn[i])) && dsn[i] != '=') {
      key += dsn[i++];
    }
    if (i < n && dsn[i] == '=') {
      ++i;
      std::string val;
      if (i < n && dsn[i] == '\'') {
        ++i;
        while (i < n) {
          if (dsn[i] == '\'') {
            if (i + 1 < n && dsn[i + 1] == '\'') {
              val += '\'';
              i += 2;
            } else {
              ++i;
              break;
            }
          } else {
            val += dsn[i++];
          }
        }
      } else {
        while (i < n && !std::isspace(static_cast<unsigned char>(dsn[i]))) val += dsn[i++];
      }
      if (key == "host" || key == "hostaddr") {
        if (p.host.empty()) p.host = val;
      } else if (key == "port") {
        p.port = val;
      } else if (key == "dbname") {
        p.dbname = val;
      } else if (key == "user") {
        p.user = val;
      } else if (key == "password") {
        p.password = val;
      }
    }
    // Tokens without '=' are ignored, matching libpq's tolerance.
  }
  return p;
}

// postgresql://[user[:password]@][host[:port][,host2...]][/dbname][?k=v&...]
DsnParts parse_dsn_uri_form(std::string_view dsn) {
  DsnParts p;
  p.uri = true;
  size_t authority = dsn.find("://");
  if (authority == std::string_view::npos) return p;
  size_t i = authority + 3;
  const size_t n = dsn.size();
  size_t at = i;
  while (at < n && dsn[at] != '/' && dsn[at] != '?' && dsn[at] != '@') ++at;
  if (at < n && dsn[at] == '@') {
    std::string_view userinfo = dsn.substr(i, at - i);
    size_t colon = userinfo.find(':');
    if (colon != std::string_view::npos) {
      p.user = std::string(userinfo.substr(0, colon));
      p.password = std::string(userinfo.substr(colon + 1));
    } else {
      p.user = std::string(userinfo);
    }
    i = at + 1;
  }
  size_t end = i;
  while (end < n && dsn[end] != '/' && dsn[end] != '?') ++end;
  std::string_view hostport = dsn.substr(i, end - i);
  // Only the first host of a comma list is displayed (redaction view).
  size_t comma = hostport.find(',');
  if (comma != std::string_view::npos) hostport = hostport.substr(0, comma);
  if (!hostport.empty() && hostport.front() == '[') {
    size_t close = hostport.find(']');
    if (close != std::string_view::npos) {
      p.host = std::string(hostport.substr(1, close - 1));
      if (close + 1 < hostport.size() && hostport[close + 1] == ':')
        p.port = std::string(hostport.substr(close + 2));
    }
  } else {
    size_t colon = hostport.rfind(':');
    if (colon != std::string_view::npos && hostport.find(':') == colon) {
      p.host = std::string(hostport.substr(0, colon));
      p.port = std::string(hostport.substr(colon + 1));
    } else {
      p.host = std::string(hostport);
    }
  }
  i = end;
  if (i < n && dsn[i] == '/') {
    ++i;
    size_t q = dsn.find('?', i);
    p.dbname = std::string(dsn.substr(i, (q == std::string_view::npos ? n : q) - i));
    i = (q == std::string_view::npos ? n : q);
  }
  if (i < n && dsn[i] == '?') {
    ++i;
    while (i < n) {
      size_t amp = dsn.find('&', i);
      std::string_view pair =
          dsn.substr(i, (amp == std::string_view::npos ? n : amp) - i);
      size_t eq = pair.find('=');
      std::string key(eq == std::string_view::npos ? std::string(pair)
                                                   : std::string(pair.substr(0, eq)));
      std::string val(eq == std::string_view::npos ? std::string()
                                                   : std::string(pair.substr(eq + 1)));
      if (key == "host" || key == "hostaddr") {
        if (p.host.empty()) p.host = val;
      } else if (key == "port") {
        p.port = val;
      } else if (key == "dbname") {
        p.dbname = val;
      } else if (key == "user") {
        p.user = val;
      } else if (key == "password") {
        p.password = val;
      }
      if (amp == std::string_view::npos) break;
      i = amp + 1;
    }
  }
  return p;
}

bool looks_like_uri(std::string_view dsn) {
  return dsn.find("://") != std::string_view::npos;
}

DsnParts parse_dsn(std::string_view dsn) {
  return looks_like_uri(dsn) ? parse_dsn_uri_form(dsn) : parse_dsn_keyword_form(dsn);
}

// Canonical v13-equivalent PostgreSQL DDL (plan D2). Translation decisions,
// each documented where the SQLite original differs:
//   - INTEGER PRIMARY KEY AUTOINCREMENT -> bigint GENERATED ALWAYS AS
//     IDENTITY PRIMARY KEY (no production statement reads sqlite_sequence;
//     identity is DDL-level only).
//   - DEFAULT (datetime('now')) -> DEFAULT now() with timestamptz columns.
//     The app layer keeps writing util::utc_now() text via bind_text; PG
//     parses it (session timezone is UTC) and renders reads back in PG's
//     canonical timestamptz text format -- ordering semantics are identical,
//     byte-level text round-trip is not (documented deviation, census
//     global_notes).
//   - COLLATE BINARY -> COLLATE "C" on slug/identifier columns (pages.slug,
//     links.from_slug/to_slug, page_versions.slug, facts.entity_slug,
//     takes.entity_slug) so ORDER BY / comparison byte-order semantics match
//     SQLite BINARY (census: 17 COLLATE occurrences resolved at the source).
//   - embedding BLOB -> bytea (packed/unpacked app-side, transport only).
//   - pages_fts FTS5 external-content table + 3 sync triggers -> one stored
//     generated tsvector column (title weight A, body weight B, 'simple'
//     config to mirror unicode61's no-stemming tokenization) + GIN index;
//     matches the fts_search seam (D0.5 resolution point).
//   - v12's ingest_log rebuild and v13's additive columns are expressed in
//     their final shape (fresh PG databases are born at v13; see
//     pg_ensure_schema's rejection of pre-existing lower versions).
const char* const kPgSchemaSql = R"QBPGSQL(
CREATE TABLE IF NOT EXISTS schema_version (
  version BIGINT NOT NULL PRIMARY KEY,
  applied_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS sources (
  id TEXT PRIMARY KEY,
  name TEXT,
  local_path TEXT,
  config_json TEXT NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now(),
  last_sync_at timestamptz
);

CREATE TABLE IF NOT EXISTS pages (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  source_id TEXT NOT NULL DEFAULT 'default',
  slug TEXT COLLATE "C" NOT NULL,
  type TEXT NOT NULL DEFAULT 'note',
  title TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  frontmatter_json TEXT NOT NULL DEFAULT '{}',
  content_hash TEXT,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  deleted_at timestamptz,
  source_kind TEXT,
  ingested_via TEXT,
  ingested_at timestamptz,
  pages_ftv tsvector GENERATED ALWAYS AS (
    setweight(to_tsvector('simple'::regconfig, coalesce(title, '')), 'A') ||
    setweight(to_tsvector('simple'::regconfig, coalesce(body, '')), 'B')
  ) STORED,
  UNIQUE(source_id, slug),
  FOREIGN KEY(source_id) REFERENCES sources(id)
);

CREATE INDEX IF NOT EXISTS idx_pages_type ON pages(type);
CREATE INDEX IF NOT EXISTS idx_pages_updated ON pages(updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_pages_source ON pages(source_id);
CREATE INDEX IF NOT EXISTS idx_pages_source_slug ON pages(source_id, slug);
CREATE INDEX IF NOT EXISTS idx_pages_ftv ON pages USING gin (pages_ftv);

CREATE TABLE IF NOT EXISTS content_chunks (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  page_id BIGINT NOT NULL,
  chunk_index BIGINT NOT NULL,
  text TEXT NOT NULL,
  embedding bytea,
  dim BIGINT,
  model TEXT,
  UNIQUE(page_id, chunk_index),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_chunks_page ON content_chunks(page_id);
CREATE INDEX IF NOT EXISTS idx_chunks_missing_emb
  ON content_chunks(page_id) WHERE embedding IS NULL;

CREATE TABLE IF NOT EXISTS links (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  source_id TEXT NOT NULL DEFAULT 'default',
  from_slug TEXT COLLATE "C" NOT NULL,
  to_slug TEXT COLLATE "C" NOT NULL,
  link_type TEXT NOT NULL DEFAULT 'related',
  context TEXT NOT NULL DEFAULT '',
  link_source TEXT NOT NULL DEFAULT 'markdown',
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE(source_id, from_slug, to_slug, link_type, link_source)
);

CREATE INDEX IF NOT EXISTS idx_links_from ON links(source_id, from_slug);
CREATE INDEX IF NOT EXISTS idx_links_to ON links(source_id, to_slug);

CREATE TABLE IF NOT EXISTS tags (
  page_id BIGINT NOT NULL,
  tag TEXT NOT NULL,
  PRIMARY KEY(page_id, tag),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS config (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS jobs (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  queue TEXT NOT NULL DEFAULT 'default',
  type TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'waiting',
  payload_json TEXT NOT NULL DEFAULT '{}',
  result_json TEXT,
  priority BIGINT NOT NULL DEFAULT 100,
  attempts BIGINT NOT NULL DEFAULT 0,
  created_at timestamptz NOT NULL DEFAULT now(),
  updated_at timestamptz NOT NULL DEFAULT now(),
  lock_until timestamptz,
  lock_token TEXT,
  error_text TEXT,
  parent_id BIGINT,
  depth BIGINT NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_jobs_claim ON jobs(queue, status, priority, created_at);
CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status, type);
CREATE INDEX IF NOT EXISTS idx_jobs_parent ON jobs(parent_id);

CREATE TABLE IF NOT EXISTS ingest_log (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  source_id TEXT NOT NULL,
  event_type TEXT NOT NULL DEFAULT 'import',
  path TEXT NOT NULL DEFAULT '',
  detail_json TEXT NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now(),
  FOREIGN KEY(source_id) REFERENCES sources(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_ingest_log_source_created
  ON ingest_log(source_id, created_at DESC, id DESC);

CREATE TABLE IF NOT EXISTS job_messages (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  job_id BIGINT NOT NULL,
  sender TEXT NOT NULL DEFAULT 'system',
  payload_json TEXT NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_job_messages_job ON job_messages(job_id, id);

CREATE TABLE IF NOT EXISTS page_versions (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  page_id BIGINT NOT NULL,
  source_id TEXT NOT NULL DEFAULT 'default',
  slug TEXT COLLATE "C" NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  body TEXT NOT NULL DEFAULT '',
  frontmatter_json TEXT NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now(),
  FOREIGN KEY(page_id) REFERENCES pages(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_page_versions_page ON page_versions(page_id);

CREATE TABLE IF NOT EXISTS facts (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  page_id BIGINT,
  entity_slug TEXT COLLATE "C" NOT NULL,
  predicate TEXT NOT NULL DEFAULT 'mentions',
  object_text TEXT NOT NULL DEFAULT '',
  active BIGINT NOT NULL DEFAULT 1,
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_facts_entity ON facts(entity_slug);

CREATE TABLE IF NOT EXISTS takes (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  entity_slug TEXT COLLATE "C" NOT NULL,
  kind TEXT NOT NULL DEFAULT 'fact',
  body TEXT NOT NULL DEFAULT '',
  score DOUBLE PRECISION NOT NULL DEFAULT 0,
  active BIGINT NOT NULL DEFAULT 1,
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_takes_entity ON takes(entity_slug, active);
CREATE INDEX IF NOT EXISTS idx_takes_body ON takes(body);

CREATE TABLE IF NOT EXISTS file_index (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  name TEXT NOT NULL,
  path TEXT NOT NULL,
  size BIGINT NOT NULL DEFAULT 0,
  mime TEXT NOT NULL DEFAULT 'application/octet-stream',
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_file_index_name ON file_index(name);

CREATE TABLE IF NOT EXISTS raw_data (
  id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  key TEXT NOT NULL UNIQUE,
  content_text TEXT NOT NULL DEFAULT '',
  meta_json TEXT NOT NULL DEFAULT '{}',
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_raw_data_key ON raw_data(key);

INSERT INTO sources(id, name) VALUES ('default', 'Default Source')
  ON CONFLICT (id) DO NOTHING;
INSERT INTO schema_version(version)
  VALUES (1),(2),(3),(4),(5),(6),(7),(8),(9),(10),(11),(12),(13)
  ON CONFLICT (version) DO NOTHING;
)QBPGSQL";

}  // namespace

// ---- exported pure helpers (compiled without libpq) ----

std::string pg_redact_dsn(std::string_view dsn) {
  DsnParts p = parse_dsn(dsn);
  std::string host = p.host.empty() ? "localhost" : p.host;
  std::string port = p.port.empty() ? "5432" : p.port;
  std::string user = p.user.empty() ? "(default)" : p.user;
  std::string dbname = p.dbname.empty() ? (p.user.empty() ? "(default)" : p.user) : p.dbname;
  return "host=" + host + " port=" + port + " dbname=" + dbname + " user=" + user;
}

std::string pg_translate_placeholders(std::string_view sql, int* param_count) {
  std::string out;
  out.reserve(sql.size() + 8);
  int max_idx = 0;
  enum class St { Normal, LineComment, BlockComment, SQuote, DQuote, Bracket };
  St st = St::Normal;
  bool e_string = false;   // E'...' variant: backslash escapes active
  size_t i = 0;
  const size_t n = sql.size();
  while (i < n) {
    const char c = sql[i];
    switch (st) {
      case St::LineComment:
        out += c;
        if (c == '\n') st = St::Normal;
        ++i;
        break;
      case St::BlockComment:
        out += c;
        if (c == '*' && i + 1 < n && sql[i + 1] == '/') {
          out += '/';
          i += 2;
          st = St::Normal;
        } else {
          ++i;
        }
        break;
      case St::SQuote:
        out += c;
        if (e_string && c == '\\' && i + 1 < n) {
          out += sql[i + 1];
          i += 2;
          break;
        }
        if (c == '\'') {
          if (i + 1 < n && sql[i + 1] == '\'') {
            out += '\'';
            i += 2;
            break;
          }
          st = St::Normal;
        }
        ++i;
        break;
      case St::DQuote:
        out += c;
        if (c == '"') {
          if (i + 1 < n && sql[i + 1] == '"') {
            out += '"';
            i += 2;
            break;
          }
          st = St::Normal;
        }
        ++i;
        break;
      case St::Bracket:
        out += c;
        if (c == ']') st = St::Normal;
        ++i;
        break;
      case St::Normal:
        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
          out += "--";
          i += 2;
          st = St::LineComment;
        } else if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
          out += "/*";
          i += 2;
          st = St::BlockComment;
        } else if (c == '\'') {
          out += c;
          st = St::SQuote;
          e_string = false;
          ++i;
        } else if ((c == 'e' || c == 'E') && i + 1 < n && sql[i + 1] == '\'' &&
                   (i == 0 || !is_ident_char(sql[i - 1]))) {
          out += c;
          out += '\'';
          i += 2;
          st = St::SQuote;
          e_string = true;
        } else if (c == '"') {
          out += c;
          st = St::DQuote;
          ++i;
        } else if (c == '[') {
          out += c;
          st = St::Bracket;
          ++i;
        } else if (c == '$') {
          // PostgreSQL $tag$ dollar-quoted span: '$' [A-Za-z_][A-Za-z_0-9]*
          // '$' or the empty '$$'. The tag run must NOT include '$' (that
          // is the closer), and a digits-only run is a positional marker,
          // not a tag. Consumed verbatim so bodies inside dollar quotes
          // are never rewritten.
          size_t j = i + 1;
          while (j < n && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_'))
            ++j;
          bool has_tag_ident = false;
          for (size_t k = i + 1; k < j; ++k) {
            if (std::isalpha(static_cast<unsigned char>(sql[k])) || sql[k] == '_') {
              has_tag_ident = true;
              break;
            }
          }
          if (j < n && sql[j] == '$' && (j == i + 1 || has_tag_ident)) {
            std::string tag(sql.substr(i, j - i + 1));
            out += tag;
            size_t close = sql.find(tag, j + 1);
            if (close == std::string_view::npos) {
              out.append(sql.substr(j + 1));
              i = n;
            } else {
              out.append(sql.substr(j + 1, close + tag.size() - (j + 1)));
              i = close + tag.size();
            }
          } else {
            out += c;
            ++i;
          }
        } else if (c == '?') {
          size_t j = i + 1;
          while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) ++j;
          int idx;
          if (j > i + 1) {
            idx = std::atoi(std::string(sql.substr(i + 1, j - i - 1)).c_str());
          } else {
            idx = max_idx + 1;
          }
          if (idx < 1) idx = 1;
          max_idx = std::max(max_idx, idx);
          out += '$';
          out += std::to_string(idx);
          i = j;
        } else {
          out += c;
          ++i;
        }
        break;
    }
  }
  if (param_count) *param_count = max_idx;
  return out;
}

std::string pg_canonical_schema_sql() { return std::string(kPgSchemaSql); }

std::string pg_fts_normalize_query(const std::string& query) {
  auto parts = util::split(query, ' ');
  std::string out;
  for (auto& p0 : parts) {
    std::string p = util::trim(p0);
    p = util::replace_all(p, "\"", "");
    p = util::to_lower(p);
    p = util::trim(p);
    if (p.empty()) continue;
    if (!out.empty()) out += " ";
    out += p;
  }
  return out;
}

#ifdef QBRAIN_WITH_PG

// ===========================================================================
// libpq backend (compiled only when discovery found PostgreSQL).
// ===========================================================================

namespace {

// ---- int rc contract (docs/10 §3): the raw SQLite rc values, no new enum ----
constexpr int kRcOk = 0;          // SQLITE_OK
constexpr int kRcError = 1;       // SQLITE_ERROR      (syntax / general)
constexpr int kRcBusy = 5;        // SQLITE_BUSY       (lock contention, retryable)
constexpr int kRcConstraint = 19; // SQLITE_CONSTRAINT (unique/FK/not-null)

// SQLSTATE -> error class table (the PG column of the docs/10 §3 map):
//
//   SQLSTATE  meaning                  class       rc  message marker
//   55P03     lock_not_available       busy         5  "database is locked: ..."
//   40001     serialization_failure    busy         5  "database is locked: ..."
//   40P01     deadlock_detected        busy         5  "database is locked: ..."
//   23505     unique_violation         constraint  19  "constraint failed: ..."
//   23503     foreign_key_violation    constraint  19  "constraint failed: ..."
//   23502     not_null_violation       constraint  19  "constraint failed: ..."
//   23514     check_violation          constraint  19  "constraint failed: ..."
//   42601     syntax_error             error        1  raw PG text
//   (other)   anything else            error        1  raw PG text
//
// The three classes stay pairwise distinguishable in both the int rc and the
// message text, which is what the G8 contract locks.
int classify_sqlstate(const char* sqlstate) {
  if (!sqlstate || !*sqlstate) return kRcError;
  const std::string_view s(sqlstate);
  if (s == "55P03" || s == "40001" || s == "40P01") return kRcBusy;
  if (s == "23505" || s == "23503" || s == "23502" || s == "23514") return kRcConstraint;
  return kRcError;  // 42601 and everything else
}

std::string trim_copy(std::string s) { return util::trim(s); }

// True when the trimmed sql is exactly a SQLite "BEGIN IMMEDIATE[ TRANSACTION]"
// literal (case-insensitive, trailing ';' tolerated) -- the eager-write-lock
// BEGIN callers send through exec(). See PgBackend::exec for the mapping.
bool is_begin_immediate_literal(const std::string& sql) {
  std::string t = to_lower_ascii(trim_copy(sql));
  while (!t.empty() && t.back() == ';') t.pop_back();
  t = trim_copy(t);
  return t == "begin immediate" || t == "begin immediate transaction";
}

// Compose the class-marked error text (see the table above).
std::string compose_class_error(int rc, const std::string& state, const std::string& msg) {
  if (rc == kRcBusy) return "database is locked: " + msg + " (SQLSTATE " + state + ")";
  if (rc == kRcConstraint) return "constraint failed: " + msg + " (SQLSTATE " + state + ")";
  return state.empty() ? msg : msg + " (SQLSTATE " + state + ")";
}

int parse_int64_text(const char* v, int64_t& out) {
  if (!v) return 0;
  const char* end = v + std::strlen(v);
  auto [p, ec] = std::from_chars(v, end, out);
  if (ec != std::errc() || p != end) return 0;
  return 1;
}

double parse_double_text(const char* v) {
  if (!v || !*v) return 0.0;
  if (std::strcmp(v, "NaN") == 0) return std::numeric_limits<double>::quiet_NaN();
  if (std::strcmp(v, "Infinity") == 0) return HUGE_VAL;
  if (std::strcmp(v, "-Infinity") == 0) return -HUGE_VAL;
  double d = 0.0;
  auto [p, ec] = std::from_chars(v, v + std::strlen(v), d);
  if (ec != std::errc()) return 0.0;
  return d;
}

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decode bytea received in text format (PG >= 9 default output "hex": a
// leading "\x" followed by hex pairs; the legacy escape format uses
// backslash escapes and octal sequences -- both decoded here).
std::vector<uint8_t> decode_bytea_text(const char* v, int len) {
  std::vector<uint8_t> out;
  if (!v || len <= 0) return out;
  std::string_view s(v, static_cast<size_t>(len));
  if (s.size() >= 2 && s[0] == '\\' && (s[1] == 'x' || s[1] == 'X')) {
    out.reserve((s.size() - 2) / 2);
    for (size_t i = 2; i + 1 < s.size(); i += 2) {
      int hi = hex_val(s[i]), lo = hex_val(s[i + 1]);
      if (hi < 0 || lo < 0) return {};
      out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
  }
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\') {
      out.push_back(static_cast<uint8_t>(s[i]));
      continue;
    }
    ++i;
    if (i >= s.size()) break;
    if (s[i] == '\\') {
      out.push_back('\\');
    } else if (s[i] >= '0' && s[i] <= '7') {
      int oct = s[i] - '0';
      for (int k = 0; k < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7'; ++k) {
        ++i;
        oct = oct * 8 + (s[i] - '0');
      }
      out.push_back(static_cast<uint8_t>(oct));
    } else {
      out.push_back(static_cast<uint8_t>(s[i]));
    }
  }
  return out;
}

void noop_notice(void*, const char*) {}

// Parse the target table of a leading INSERT statement, normalized to
// lowercased "schema.table" (default schema "public"; the SQLite "temp."
// qualification arrives as B's "pg_temp." bilingual form). Empty when the
// statement is not an INSERT.
std::string parse_insert_target(std::string_view sql) {
  size_t i = 0;
  const size_t n = sql.size();
  auto skip_blank = [&] {
    for (;;) {
      while (i < n && std::isspace(static_cast<unsigned char>(sql[i]))) ++i;
      if (i + 1 < n && sql[i] == '-' && sql[i + 1] == '-') {
        while (i < n && sql[i] != '\n') ++i;
        continue;
      }
      if (i + 1 < n && sql[i] == '/' && sql[i + 1] == '*') {
        i += 2;
        while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
        i = (i + 1 < n) ? i + 2 : n;
        continue;
      }
      return;
    }
  };
  auto read_word = [&](std::string& w) {
    skip_blank();
    w.clear();
    while (i < n && is_ident_char(sql[i])) w += sql[i++];
    return !w.empty();
  };
  std::string w;
  if (!read_word(w) || to_lower_ascii(w) != "insert") return {};
  for (;;) {
    if (!read_word(w)) return {};
    if (to_lower_ascii(w) == "into") break;
    // Skip the conflict-target words of "INSERT OR IGNORE/REPLACE" forms.
  }
  skip_blank();
  auto read_identifier = [&](std::string& id) {
    id.clear();
    if (i < n && sql[i] == '"') {
      ++i;
      while (i < n) {
        if (sql[i] == '"') {
          if (i + 1 < n && sql[i + 1] == '"') {
            id += '"';
            i += 2;
            continue;
          }
          ++i;
          break;
        }
        id += sql[i++];
      }
      return !id.empty();
    }
    while (i < n && is_ident_char(sql[i])) id += sql[i++];
    return !id.empty();
  };
  std::string first;
  if (!read_identifier(first)) return {};
  skip_blank();
  if (i < n && sql[i] == '.') {
    ++i;
    std::string second;
    if (!read_identifier(second)) return {};
    return to_lower_ascii(first) + "." + to_lower_ascii(second);
  }
  return "public." + to_lower_ascii(first);
}

#ifdef _WIN32
std::string wide_to_utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int need = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (need <= 0) return {};
  std::string out(static_cast<size_t>(need), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), need, nullptr, nullptr);
  return out;
}
std::wstring utf8_to_wide(const std::string& s) {
  if (s.empty()) return {};
  int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (need <= 0) return {};
  std::wstring out(static_cast<size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), need);
  return out;
}
bool file_exists(const std::string& p) {
  DWORD a = GetFileAttributesW(utf8_to_wide(p).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
std::string dir_of(const std::string& p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? std::string() : p.substr(0, s);
}
#else
bool file_exists(const std::string& p) {
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
std::string dir_of(const std::string& p) {
  size_t s = p.find_last_of('/');
  return s == std::string::npos ? std::string() : p.substr(0, s);
}
#endif

std::string quote_ident(const std::string& id) {
  std::string out = "\"";
  for (char c : id) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += '"';
  return out;
}

class PgBackend final : public IStorageBackend {
 public:
  PgBackend() = default;
  ~PgBackend() override { close(); }
  PgBackend(const PgBackend&) = delete;
  PgBackend& operator=(const PgBackend&) = delete;

  // ---- connection lifecycle ----

  void open(const std::string& dsn_arg) override {
    close();
    // N38 D1: the DSN is configuration, never an argv element -- callers
    // pass it explicitly (Brain wiring reads pg_dsn_from_env()); an empty
    // argument falls back to $QBRAIN_PG_DSN, and an empty environment is a
    // hard open failure.
    std::string dsn = dsn_arg.empty() ? pg_dsn_from_env() : dsn_arg;
    if (dsn.empty()) {
      throw std::runtime_error("pg open: no DSN (set QBRAIN_PG_DSN)");
    }
    dsn_ = dsn;
    password_ = parse_dsn(dsn).password;  // scrub key only; never displayed
    PGconn* c = PQconnectdb(dsn.c_str());
    if (!c) throw std::runtime_error("pg open: out of memory");
    if (PQstatus(c) != CONNECTION_OK) {
      std::string msg = trim_copy(PQerrorMessage(c));
      PQfinish(c);
      throw std::runtime_error(
          scrub("pg open: " + msg + " dsn=" + pg_redact_dsn(dsn)));
    }
    conn_ = c;
    // NOTICE/WARNING output stays off stderr (the default notice processor
    // prints there; test/caller output parity with the SQLite path).
    PQsetNoticeProcessor(conn_, noop_notice, nullptr);
    // Session setup. UTF8 client encoding keeps text byte-identical over
    // the wire; UTC rendering makes timestamptz reads deterministic; the
    // lock_timeout default bounds lock waits so contention surfaces as the
    // busy class instead of blocking forever (SQLite fails fast with no
    // busy timeout -- PG has no instant-fail mode, 2000 ms is the
    // documented stand-in, adjustable via set_busy_timeout).
    exec_setup("SET client_encoding = 'UTF8'");
    exec_setup("SET timezone = 'UTC'");
    exec_setup("SET lock_timeout = 2000");
    const std::string user = effective(PQuser(conn_), "(default)");
    descriptor_ = "host=" + effective(PQhost(conn_), "localhost") +
                  " port=" + effective(PQport(conn_), "5432") +
                  " dbname=" + effective(PQdb(conn_), user.c_str()) +
                  " user=" + user;
    last_rc_ = kRcOk;
  }

  void close() override {
    if (conn_) {
      PQfinish(conn_);
      conn_ = nullptr;
    }
    descriptor_.clear();
    last_id_ = 0;
    changes_ = 0;
    last_rc_ = kRcOk;
    stmt_seq_ = 0;
    identity_cache_.clear();
  }

  bool is_open() const override {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
  }

  // ---- direct execution ----

  void exec(std::string_view sql) override {
    last_rc_ = kRcOk;
    if (!conn_) {
      // Matches the legacy null-connection observable ("exec failed").
      last_rc_ = kRcError;
      throw std::runtime_error("exec failed");
    }
    // N38 busy-mapping (census: 12 "BEGIN IMMEDIATE" literals reach exec()
    // from brain/minions/dream/packs/migrate; only the interface method
    // begin_immediate_transaction() is translated by itself). SQLite's
    // eager-write-lock BEGIN has no PG equivalent: map the literal to
    // BEGIN TRANSACTION (deferred). Row locks are taken at the first data
    // statement; contention then surfaces as SQLSTATE 55P03 through the
    // lock_timeout bound -- the busy class callers' retry loops digest.
    // Applied to the whole-string form only (the census sites are
    // single-statement execs), never rewritten mid-string.
    std::string sql_text(sql);
    if (is_begin_immediate_literal(sql_text)) sql_text = "BEGIN TRANSACTION";
    PGresult* r = PQexec(conn_, sql_text.c_str());
    if (!r) {
      last_rc_ = kRcError;
      throw std::runtime_error(scrub("pg exec: " + trim_copy(PQerrorMessage(conn_))));
    }
    ExecStatusType st = PQresultStatus(r);
    if (st == PGRES_FATAL_ERROR || st == PGRES_BAD_RESPONSE) {
      throw_result(r, "pg exec");
    }
    const char* t = PQcmdTuples(r);
    if (t && *t) changes_ = std::atoi(t);
    PQclear(r);
  }

  // RETURNING-based (see header note): updated only by INSERTs routed
  // through PgStatement with an injected "RETURNING id".
  int64_t last_insert_rowid() const override { return last_id_; }

  int changes() const override { return changes_; }

  std::unique_ptr<IStatement> create_statement() override {
    return std::make_unique<PgStatement>();
  }

  // ---- transactions ----

  void begin_transaction() override { exec("BEGIN"); }

  // SQLite BEGIN IMMEDIATE acquires the write lock up front. PG has no
  // eager equivalent: BEGIN TRANSACTION is deferred and the first data
  // statement takes its row locks; a contended acquisition waits up to
  // lock_timeout and then surfaces as SQLSTATE 55P03, classified busy
  // (rc 5, "database is locked") -- exactly what callers' busy-retry
  // loops digest. Documented deviation: the lock is taken later, not
  // earlier, with the same observable failure class on contention.
  void begin_immediate_transaction() override { exec("BEGIN TRANSACTION"); }

  void commit_transaction() override { exec("COMMIT"); }
  void rollback_transaction() override { exec("ROLLBACK"); }

  // ---- busy / classification ----

  int set_busy_timeout(int ms) override {
    if (!conn_) return kRcError;
    // Inversion guard: sqlite3_busy_timeout(0) means "fail instantly"
    // while PG lock_timeout=0 means "wait forever". ms<=0 clamps to 1 ms,
    // the closest PG can get to instant failure.
    const long v = ms > 0 ? static_cast<long>(ms) : 1;
    std::string sql = "SET lock_timeout = " + std::to_string(v);
    PGresult* r = PQexec(conn_, sql.c_str());
    if (!r) {
      last_rc_ = kRcError;
      return kRcError;
    }
    ExecStatusType st = PQresultStatus(r);
    if (st == PGRES_FATAL_ERROR || st == PGRES_BAD_RESPONSE) {
      last_rc_ = classify_result(r);
      PQclear(r);
      return last_rc_;
    }
    PQclear(r);
    last_rc_ = kRcOk;
    return kRcOk;
  }

  int last_error_code() const override { return conn_ ? last_rc_ : kRcOk; }

  // ---- N38 D0.5 interface extensions ----

  std::string backend_file_path() const override { return descriptor_; }

  bool backup_to(const std::string& dest) override {
    if (!conn_) {
      throw std::runtime_error("pre-migration backup open failed: " + dest);
    }
    if (try_pg_dump(dest)) return true;
    copy_export(dest);  // fallback always exists; throws on I/O failure
    return true;
  }

  std::vector<FtsRow> fts_search(const std::string& query, int limit,
                                 const std::string& source_id) override {
    std::vector<FtsRow> out;
    if (!conn_) throw std::runtime_error("prepare: no db");
    // Same result contract as the SQLite FTS5 statement: page_id/slug/
    // title/type/snippet/rank, deleted pages filtered, optional source_id
    // filter, ORDER BY rank then slug. rank is NEGATED ts_rank (title
    // weight A, body weight B) so "rank ASC" and the search layer's
    // score = -rank keep the SQLite bm25 sign conventions (bm25: more
    // negative = better). Ranking parity with bm25 is NOT bit-for-bit --
    // documented; simple exact-term queries return the same top page.
    // Snippet: ts_headline over body with SQLite-snippet-shaped options
    // (no highlight markers, one fragment) -- content parity beyond
    // "contains the matched term" is not asserted.
    const std::string norm = pg_fts_normalize_query(query);
    // Parameter order must match the $N numbering: $1 = query; with a
    // source_id filter $2 = source_id and the limit is $3, without it the
    // limit is $2 (a skipped index would leave $2 untyped -- 42P18).
    std::string sql =
        "SELECT p.id, p.slug, p.title, p.type, "
        "ts_headline('simple', p.body, q.q, "
        "'StartSel=, StopSel=, MaxFragments=1, MinWords=8, MaxWords=24'), "
        "-ts_rank('{0.5,0.3,0.2,0.1}', p.pages_ftv, q.q) AS rank "
        "FROM pages p CROSS JOIN (SELECT plainto_tsquery('simple', $1) AS q) q "
        "WHERE p.deleted_at IS NULL AND p.pages_ftv @@ q.q";
    const char* ordered[3] = {norm.c_str(), nullptr, nullptr};
    int nparams = 1;
    const std::string limit_text = std::to_string(limit);
    if (!source_id.empty()) {
      sql += " AND p.source_id = $2";
      sql += " ORDER BY rank ASC, p.slug COLLATE \"C\" ASC LIMIT $3";
      ordered[1] = source_id.c_str();
      ordered[2] = limit_text.c_str();
      nparams = 3;
    } else {
      sql += " ORDER BY rank ASC, p.slug COLLATE \"C\" ASC LIMIT $2";
      ordered[1] = limit_text.c_str();
      nparams = 2;
    }
    PGresult* r =
        PQexecParams(conn_, sql.c_str(), nparams, nullptr, ordered, nullptr, nullptr, 0);
    if (!r) {
      last_rc_ = kRcError;
      throw std::runtime_error(scrub("step: " + trim_copy(PQerrorMessage(conn_))));
    }
    ExecStatusType st = PQresultStatus(r);
    if (st == PGRES_FATAL_ERROR || st == PGRES_BAD_RESPONSE) {
      throw_result(r, "step");
    }
    const int nt = PQntuples(r);
    for (int i = 0; i < nt && i < limit; ++i) {
      FtsRow row;
      int64_t id = 0;
      parse_int64_text(PQgetvalue(r, i, 0), id);
      row.page_id = id;
      row.slug = PQgetisnull(r, i, 1) ? std::string() : std::string(PQgetvalue(r, i, 1), PQgetlength(r, i, 1));
      row.title = PQgetisnull(r, i, 2) ? std::string() : std::string(PQgetvalue(r, i, 2), PQgetlength(r, i, 2));
      row.type = PQgetisnull(r, i, 3) ? std::string() : std::string(PQgetvalue(r, i, 3), PQgetlength(r, i, 3));
      row.snippet = PQgetisnull(r, i, 4) ? std::string() : std::string(PQgetvalue(r, i, 4), PQgetlength(r, i, 4));
      row.rank = parse_double_text(PQgetvalue(r, i, 5));
      out.push_back(std::move(row));
    }
    PQclear(r);
    return out;
  }

  // ---- non-contract extras (harness / wiring / tests) ----

  const std::string& backup_note() const { return backup_note_; }
  PGconn* conn() const { return conn_; }

  // ---- statement layer ----

  class PgStatement final : public IStatement {
   public:
    ~PgStatement() override {
      clear_result();
      deallocate();
    }

    void prepare(IStorageBackend& db, std::string_view sql) override {
      // Re-prepare in place (legacy Statement semantics): drop the previous
      // server-side statement first.
      clear_result();
      deallocate();
      auto& pg = static_cast<PgBackend&>(db);
      if (!pg.conn_) throw std::runtime_error("prepare: no db");
      db_ = &pg;
      int n = 0;
      sql_ = pg_translate_placeholders(sql, &n);
      nparams_ = n;
      params_.assign(static_cast<size_t>(n), Param{});
      insert_returning_ = false;
      // last_insert_rowid adaptation: inject "RETURNING id" for INSERTs
      // whose target table has an identity id column (catalog-checked, so
      // tags / schema_version / sources / config / fence / temp tables are
      // never rewritten and plain explicit-id inserts keep working).
      const std::string target = parse_insert_target(sql);
      if (!target.empty() && db_->table_has_identity_id(target)) {
        while (!sql_.empty() &&
               (sql_.back() == ';' || std::isspace(static_cast<unsigned char>(sql_.back())))) {
          sql_.pop_back();
        }
        sql_ += " RETURNING id";
        insert_returning_ = true;
      }
      name_ = db_->next_statement_name();
      PGresult* r = PQprepare(db_->conn_, name_.c_str(), sql_.c_str(), 0, nullptr);
      if (!r) {
        db_->last_rc_ = kRcError;
        throw std::runtime_error(
            db_->scrub("prepare: " + trim_copy(PQerrorMessage(db_->conn_))));
      }
      ExecStatusType st = PQresultStatus(r);
      if (st == PGRES_FATAL_ERROR || st == PGRES_BAD_RESPONSE) {
        db_->throw_result(r, "prepare");
      }
      PQclear(r);
      prepared_ = true;
      executed_ = false;
      row_ = 0;
    }

    void reset() override {
      // SQLite reset(): returns the statement to its pre-execution state;
      // the next step() re-executes with the CURRENT bindings (the N34
      // rebind contract). Lazy: the re-execution happens on the next step.
      executed_ = false;
      row_ = 0;
    }

    void clear_bindings() override {
      params_.assign(static_cast<size_t>(nparams_), Param{});
    }

    void bind_int(int idx, int64_t v) override {
      Param& p = param(idx);
      p.null = false;
      p.binary = false;
      p.text = std::to_string(v);
    }

    void bind_double(int idx, double v) override {
      Param& p = param(idx);
      p.null = false;
      p.binary = false;
      // Locale-independent shortest round-trip representation.
      char buf[40];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, v);
      p.text = std::string(buf, ptr);
    }

    void bind_text(int idx, std::string_view v) override {
      Param& p = param(idx);
      p.null = false;
      p.binary = false;
      p.text.assign(v.data(), v.size());
    }

    void bind_blob(int idx, const void* data, int size) override {
      Param& p = param(idx);
      p.null = false;
      p.binary = true;  // bytea in binary format = raw octets
      p.bytes.clear();
      if (data && size > 0) {
        const auto* b = static_cast<const uint8_t*>(data);
        p.bytes.assign(b, b + size);
      }
    }

    void bind_null(int idx) override {
      Param& p = param(idx);
      p = Param{};
    }

    bool step() override {
      if (!prepared_ || !db_ || !db_->conn_) {
        if (db_) db_->last_rc_ = kRcError;
        throw std::runtime_error("step: statement not prepared");
      }
      if (!executed_) execute();
      // INSERT-with-RETURNING keeps SQLite's shape: no rows are exposed to
      // the caller; the id was captured inside execute().
      if (insert_returning_) return false;
      if (res_ && row_ < PQntuples(res_)) {
        ++row_;
        return true;
      }
      return false;
    }

    void step_done() override {
      if (step()) {
        int extra = 1;
        while (step()) ++extra;
        (void)extra;
      }
    }

    int64_t column_int(int i) const override {
      int64_t v = 0;
      if (on_row()) parse_int64_text(PQgetvalue(res_, row_ - 1, i), v);
      return v;
    }

    double column_double(int i) const override {
      if (!on_row()) return 0.0;
      return parse_double_text(PQgetvalue(res_, row_ - 1, i));
    }

    std::string column_text(int i) const override {
      if (!on_row() || PQgetisnull(res_, row_ - 1, i)) return {};
      return std::string(PQgetvalue(res_, row_ - 1, i), PQgetlength(res_, row_ - 1, i));
    }

    std::vector<uint8_t> column_blob(int i) const override {
      if (!on_row() || PQgetisnull(res_, row_ - 1, i)) return {};
      return decode_bytea_text(PQgetvalue(res_, row_ - 1, i), PQgetlength(res_, row_ - 1, i));
    }

    bool column_is_null(int i) const override {
      if (!on_row()) return true;
      return PQgetisnull(res_, row_ - 1, i) != 0;
    }

   private:
    friend class PgBackend;
    struct Param {
      bool null = true;
      bool binary = false;
      std::string text;
      std::vector<uint8_t> bytes;
    };

    Param& param(int idx) {
      const size_t i = static_cast<size_t>(idx - 1);
      if (i >= params_.size()) params_.resize(i + 1);
      return params_[i];
    }

    bool on_row() const { return prepared_ && executed_ && res_ && row_ > 0 && row_ <= PQntuples(res_); }

    void clear_result() {
      if (res_) {
        PQclear(res_);
        res_ = nullptr;
      }
      row_ = 0;
    }

    void deallocate() {
      if (!name_.empty() && db_ && db_->conn_) {
        // Release the server-side plan (the PQprepare equivalent of
        // sqlite3_finalize). Unique names per IStatement keep concurrent
        // live statements from clobbering each other's plans.
        PGresult* r = PQexec(db_->conn_, ("DEALLOCATE " + name_).c_str());
        if (r) PQclear(r);
      }
      name_.clear();
      prepared_ = false;
      executed_ = false;
    }

    void execute() {
      clear_result();
      const int n = static_cast<int>(params_.size());
      std::vector<const char*> vals(static_cast<size_t>(n), nullptr);
      std::vector<int> lens(static_cast<size_t>(n), 0);
      std::vector<int> fmts(static_cast<size_t>(n), 0);
      for (int i = 0; i < n; ++i) {
        const Param& p = params_[static_cast<size_t>(i)];
        if (p.null) continue;
        if (p.binary) {
          vals[static_cast<size_t>(i)] =
              reinterpret_cast<const char*>(p.bytes.data());
          lens[static_cast<size_t>(i)] = static_cast<int>(p.bytes.size());
          fmts[static_cast<size_t>(i)] = 1;
        } else {
          vals[static_cast<size_t>(i)] = p.text.c_str();
          lens[static_cast<size_t>(i)] = static_cast<int>(p.text.size());
        }
      }
      PGresult* r = PQexecPrepared(db_->conn_, name_.c_str(), n,
                                   vals.data(), lens.data(), fmts.data(), 0);
      if (!r) {
        db_->last_rc_ = kRcError;
        throw std::runtime_error(
            db_->scrub("step: " + trim_copy(PQerrorMessage(db_->conn_))));
      }
      res_ = r;
      ExecStatusType st = PQresultStatus(r);
      if (st == PGRES_FATAL_ERROR || st == PGRES_BAD_RESPONSE) {
        res_ = nullptr;  // throw_result clears the PGresult itself
        db_->throw_result(r, "step");
      }
      db_->last_rc_ = kRcOk;
      if (insert_returning_) {
        const int nt = PQntuples(res_);
        if (nt > 0) {
          // Rows arrive in insertion order; SQLite's last_insert_rowid after
          // a multi-row INSERT is the LAST (highest) rowid -- capture that.
          int64_t id = 0;
          if (parse_int64_text(PQgetvalue(res_, nt - 1, 0), id)) db_->last_id_ = id;
        }
        // rows-affected == rows-returned for INSERT ... RETURNING; a
        // skipped ON CONFLICT DO NOTHING insert reports 0, matching
        // sqlite3_changes.
        db_->changes_ = nt;
      } else if (st == PGRES_COMMAND_OK) {
        const char* t = PQcmdTuples(res_);
        if (t && *t) db_->changes_ = std::atoi(t);
      }
      // SELECTs leave changes_ untouched (sqlite3_changes semantics).
      executed_ = true;
    }

    PgBackend* db_ = nullptr;
    std::string name_;
    std::string sql_;
    int nparams_ = 0;
    bool insert_returning_ = false;
    bool prepared_ = false;
    bool executed_ = false;
    PGresult* res_ = nullptr;
    int row_ = 0;
    std::vector<Param> params_;
  };

  // ---- internals ----

  // Extracts the class-marked error, records last_rc_, clears the result and
  // throws. Callers must not hold the PGresult* afterwards (statement-level
  // callers null their copy BEFORE calling).
  [[noreturn]] void throw_result(PGresult* r, const char* what) {
    const char* sf = PQresultErrorField(r, PG_DIAG_SQLSTATE);
    const std::string state = sf ? std::string(sf) : std::string();
    const std::string msg = trim_copy(PQresultErrorMessage(r));
    const int rc = classify_sqlstate(state.c_str());
    last_rc_ = rc;
    const std::string composed =
        scrub(std::string(what) + ": " + compose_class_error(rc, state, msg));
    PQclear(r);
    throw std::runtime_error(composed);
  }

  int classify_result(PGresult* r) const {
    const char* sf = PQresultErrorField(r, PG_DIAG_SQLSTATE);
    return classify_sqlstate(sf);
  }

  void exec_setup(const char* sql) {
    PGresult* r = PQexec(conn_, sql);
    if (!r) {
      std::string msg = trim_copy(PQerrorMessage(conn_));
      PQfinish(conn_);
      conn_ = nullptr;
      throw std::runtime_error(scrub("pg open: session setup failed: " + msg));
    }
    if (PQresultStatus(r) == PGRES_FATAL_ERROR) {
      const char* sf = PQresultErrorField(r, PG_DIAG_SQLSTATE);
      const std::string state = sf ? std::string(sf) : std::string();
      std::string msg = trim_copy(PQresultErrorMessage(r));
      PQclear(r);
      PQfinish(conn_);
      conn_ = nullptr;
      throw std::runtime_error(
          scrub("pg open: session setup failed: " + compose_class_error(kRcError, state, msg)));
    }
    PQclear(r);
  }

  static std::string effective(const char* v, const char* fallback) {
    if (v && *v) return std::string(v);
    return std::string(fallback);
  }

  // Remove the exact password bytes from any message that is about to
  // escape the backend (defense in depth on top of pg_redact_dsn; skipped
  // for trivially short values that would mangle unrelated text).
  std::string scrub(std::string msg) const {
    if (password_.size() >= 6) {
      size_t at = msg.find(password_);
      while (at != std::string::npos) {
        msg.replace(at, password_.size(), "***");
        at = msg.find(password_, at + 3);
      }
    }
    return msg;
  }

  std::string next_statement_name() {
    return "qbp_" + std::to_string(++stmt_seq_);
  }

  // Catalog check for the RETURNING-id injection: does "schema.table"
  // exist with an identity "id" column (public or this session's temp
  // schema)? Cached per backend; a lookup miss refreshes once so tables
  // created after connect (job fences, temp tables) are still detected at
  // their first INSERT prepare.
  bool table_has_identity_id(const std::string& target) {
    auto it = identity_cache_.find(target);
    if (it != identity_cache_.end()) return it->second;
    refresh_identity_cache();
    return identity_cache_.count(target) > 0;
  }

  void refresh_identity_cache() {
    identity_cache_.clear();
    const char* q =
        "SELECT CASE WHEN c.relnamespace = pg_my_temp_schema() THEN 'pg_temp' "
        "            ELSE n.nspname END AS nsp, c.relname "
        "FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace "
        "WHERE c.relkind IN ('r','p') "
        "  AND (n.nspname = 'public' OR c.relnamespace = pg_my_temp_schema()) "
        "  AND EXISTS (SELECT 1 FROM pg_attribute a "
        "              WHERE a.attrelid = c.oid AND a.attname = 'id' "
        "                AND a.attidentity IN ('a','d') AND NOT a.attisdropped)";
    PGresult* r = PQexec(conn_, q);
    if (!r) return;
    if (PQresultStatus(r) == PGRES_TUPLES_OK) {
      for (int i = 0; i < PQntuples(r); ++i) {
        std::string key = std::string(PQgetvalue(r, i, 0)) + "." + PQgetvalue(r, i, 1);
        identity_cache_[to_lower_ascii(key)] = true;
      }
    }
    PQclear(r);
  }

  // ---- backup internals ----

  std::string find_pg_dump() const {
    // 1) Explicit exclusive override (also the seam the harness uses to
    //    force the COPY fallback by pointing at a failing executable).
    if (const char* ovr = std::getenv("QBRAIN_PG_DUMP_EXE")) {
      if (*ovr) return std::string(ovr);
    }
#ifdef _WIN32
    // 2) Next to the loaded libpq DLL.
    HMODULE h = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"libpq.dll", &h) && h) {
      wchar_t wpath[1024];
      DWORD got = GetModuleFileNameW(h, wpath, 1024);
      if (got > 0 && got < 1024) {
        std::string dir = dir_of(wide_to_utf8(std::wstring(wpath, got)));
        std::string cand = dir + "\\pg_dump.exe";
        if (file_exists(cand)) return cand;
      }
    }
    // 3) PATH search.
    wchar_t wfound[1024];
    DWORD got = SearchPathW(nullptr, L"pg_dump.exe", nullptr, 1024, wfound, nullptr);
    if (got > 0 && got < 1024) return wide_to_utf8(std::wstring(wfound, got));
    // 4) Discovery roots, highest version first (numeric major compare:
    //    "9" < "18" lexicographically but not numerically).
    for (const char* root : {"D:\\PostgreSQL", "C:\\Program Files\\PostgreSQL"}) {
      std::string best;
      long best_major = -1;
      WIN32_FIND_DATAW fd;
      HANDLE find = FindFirstFileW(utf8_to_wide(std::string(root) + "\\*").c_str(), &fd);
      if (find != INVALID_HANDLE_VALUE) {
        do {
          if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
          std::string name = wide_to_utf8(fd.cFileName);
          if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) continue;
          long major = std::atol(name.c_str());
          if (major > best_major) {
            best_major = major;
            best = name;
          }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
      }
      if (!best.empty()) {
        std::string cand = std::string(root) + "\\" + best + "\\bin\\pg_dump.exe";
        if (file_exists(cand)) return cand;
      }
    }
#else
    if (const char* p = std::getenv("PATH")) {
      // minimal PATH scan
      std::string path(p);
      size_t start = 0;
      while (start <= path.size()) {
        size_t colon = path.find(':', start);
        std::string dir = path.substr(start, (colon == std::string::npos ? path.size() : colon) - start);
        std::string cand = dir + "/pg_dump";
        if (file_exists(cand)) return cand;
        if (colon == std::string::npos) break;
        start = colon + 1;
      }
    }
#endif
    return {};
  }

  bool try_pg_dump(const std::string& dest) {
    const std::string exe = find_pg_dump();
    if (exe.empty()) {
      backup_note_ = "copy-fallback: pg_dump not found";
      return false;
    }
    // The password travels via the child's PGPASSWORD environment, never
    // on the command line (process listings stay secret-free).
    const std::string conn =
        "host=" + effective(PQhost(conn_), "localhost") +
        " port=" + effective(PQport(conn_), "5432") +
        " dbname=" + effective(PQdb(conn_), "") +
        " user=" + effective(PQuser(conn_), "");
    std::string cmdline = "\"" + exe + "\" --dbname=\"" + conn + "\" --file=\"" + dest +
                          "\" --format=plain --no-owner --no-privileges";
#ifdef _WIN32
    std::wstring wcmd = utf8_to_wide(cmdline);
    // Inherit the current environment plus PGPASSWORD.
    std::wstring env_extra;
    if (!password_.empty()) env_extra = utf8_to_wide("PGPASSWORD=" + password_);
    wchar_t* env_block = GetEnvironmentStringsW();
    if (!env_block) {
      backup_note_ = "copy-fallback: environment snapshot failed";
      return false;
    }
    std::vector<wchar_t> env;
    for (wchar_t* p = env_block; *p; p += lstrlenW(p) + 1) {
      env.insert(env.end(), p, p + lstrlenW(p) + 1);
    }
    FreeEnvironmentStringsW(env_block);
    if (!env_extra.empty()) {
      env.insert(env.end(), env_extra.begin(), env_extra.end());
      env.push_back(L'\0');  // terminate the appended variable...
    }
    env.push_back(L'\0');    // ...and the whole block
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdbuf(wcmd.begin(), wcmd.end());
    cmdbuf.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env.data(),
                             nullptr, &si, &pi);
    if (!ok) {
      backup_note_ = "copy-fallback: pg_dump process start failed";
      return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (rc == 0) {
      backup_note_ = "pg_dump";
      return true;
    }
    backup_note_ = "copy-fallback: pg_dump exited rc=" + std::to_string((long)rc);
#else
    std::string full = cmdline;
    std::string env_prefix;
    if (!password_.empty()) env_prefix = "PGPASSWORD='" + password_ + "' ";
    int rc = std::system((env_prefix + full + " >/dev/null 2>&1").c_str());
    if (rc == 0) {
      backup_note_ = "pg_dump";
      return true;
    }
    backup_note_ = "copy-fallback: pg_dump exited rc=" + std::to_string(rc);
#endif
    std::remove(dest.c_str());  // drop any partial output before the fallback
    return false;
  }

  // Structured COPY export of every public table: psql-restorable SQL text
  // with a header documenting the downgrade from pg_dump.
  void copy_export(const std::string& dest) {
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("pre-migration backup failed: " + dest);
    }
    out << "-- Qbrain PostgreSQL fallback export (COPY path)\n";
    out << "-- note: " << backup_note_ << "; pg_dump unavailable or failed\n";
    out << "-- source: " << (descriptor_.empty() ? backend_file_path() : descriptor_) << "\n";
    out << "-- generated: " << util::utc_now() << "\n";
    // Every BASE TABLE of the public schema, stable order.
    PGresult* tables = PQexec(conn_,
                              "SELECT tablename FROM pg_tables WHERE schemaname = 'public' "
                              "ORDER BY tablename");
    if (!tables || PQresultStatus(tables) != PGRES_TUPLES_OK) {
      if (tables) PQclear(tables);
      out.close();
      throw std::runtime_error("pre-migration backup failed: " + dest);
    }
    const int nt = PQntuples(tables);
    for (int t = 0; t < nt; ++t) {
      const std::string table = PQgetvalue(tables, t, 0);
      const char* cv[1] = {table.c_str()};
      PGresult* cols = PQexecParams(conn_,
                                    "SELECT column_name FROM information_schema.columns "
                                    "WHERE table_schema = 'public' AND table_name = $1 "
                                    "ORDER BY ordinal_position",
                                    1, nullptr, cv, nullptr, nullptr, 0);
      if (!cols || PQresultStatus(cols) != PGRES_TUPLES_OK || PQntuples(cols) == 0) {
        if (cols) PQclear(cols);
        PQclear(tables);
        out.close();
        throw std::runtime_error("pre-migration backup failed: " + dest);
      }
      std::string collist;
      for (int c = 0; c < PQntuples(cols); ++c) {
        if (!collist.empty()) collist += ", ";
        collist += quote_ident(PQgetvalue(cols, c, 0));
      }
      PQclear(cols);
      out << "\nCOPY public." << quote_ident(table) << " (" << collist << ") FROM stdin;\n";
      const std::string copy_sql = "COPY public." + quote_ident(table) + " TO STDOUT";
      PGresult* cr = PQexec(conn_, copy_sql.c_str());
      if (!cr || PQresultStatus(cr) != PGRES_COPY_OUT) {
        if (cr) PQclear(cr);
        PQclear(tables);
        out.close();
        throw std::runtime_error("pre-migration backup failed: " + dest);
      }
      PQclear(cr);
      for (;;) {
        char* buf = nullptr;
        int len = PQgetCopyData(conn_, &buf, 0);
        if (len > 0) {
          out.write(buf, len);
          PQfreemem(buf);
          continue;
        }
        if (buf) PQfreemem(buf);
        if (len == -1) break;         // done
        if (len == -2) {              // error mid-stream
          PQclear(tables);
          out.close();
          throw std::runtime_error("pre-migration backup failed: " + dest);
        }
      }
      out << "\\.\n";
      PGresult* done = PQgetResult(conn_);  // consume the COPY completion
      if (done) PQclear(done);
    }
    PQclear(tables);
    out.close();
    if (!out.good()) {
      throw std::runtime_error("pre-migration backup failed: " + dest);
    }
    backup_note_ += " (COPY export written)";
  }

  // ---- state ----
  PGconn* conn_ = nullptr;
  std::string dsn_;        // full DSN incl. password; never leaves via scrub paths
  std::string password_;   // scrub key
  std::string descriptor_;  // redacted host/port/dbname/user view
  std::string backup_note_ = "no backup yet";
  int64_t last_id_ = 0;
  int changes_ = 0;
  int last_rc_ = kRcOk;
  long stmt_seq_ = 0;
  std::unordered_map<std::string, bool> identity_cache_;
};

// N35 D1: compile-time proof that the concrete backend implements the
// storage contract.
static_assert(std::is_base_of_v<IStorageBackend, PgBackend>,
              "PgBackend must implement the N35 IStorageBackend contract");

}  // namespace

std::string pg_dsn_from_env() {
  const char* v = std::getenv("QBRAIN_PG_DSN");
  return (v && *v) ? std::string(v) : std::string();
}

std::unique_ptr<IStorageBackend> make_pg_backend(const std::string& dsn) {
  auto backend = std::make_unique<PgBackend>();
  backend->open(dsn);
  return backend;
}

PGconn* pg_conn_of(IStorageBackend& backend) {
  auto* pg = dynamic_cast<PgBackend*>(&backend);
  return pg ? pg->conn() : nullptr;
}

std::string pg_backup_note_of(IStorageBackend& backend) {
  auto* pg = dynamic_cast<PgBackend*>(&backend);
  return pg ? pg->backup_note() : std::string("not a PgBackend");
}

void pg_ensure_schema(PGconn* conn) {
  if (!conn || PQstatus(conn) != CONNECTION_OK) {
    throw std::runtime_error("pg_ensure_schema: connection not open");
  }
  if (PQtransactionStatus(conn) != PQTRANS_IDLE) {
    throw std::runtime_error("pg_ensure_schema: connection must be idle (no open transaction)");
  }
  auto run = [&](const char* sql) {
    PGresult* r = PQexec(conn, sql);
    if (!r) {
      throw std::runtime_error("pg_ensure_schema: " + trim_copy(PQerrorMessage(conn)));
    }
    ExecStatusType st = PQresultStatus(r);
    if (st == PGRES_FATAL_ERROR || st == PGRES_BAD_RESPONSE) {
      const char* sf = PQresultErrorField(r, PG_DIAG_SQLSTATE);
      const std::string state = sf ? std::string(sf) : std::string();
      std::string msg = trim_copy(PQresultErrorMessage(r));
      PQclear(r);
      throw std::runtime_error("pg_ensure_schema: " + compose_class_error(kRcError, state, msg));
    }
    PQclear(r);
  };
  // Pre-existing state? A PG database below 13 is rejected: migrations are
  // the SQLite path, PG stores are born at v13 (plan D2).
  PGresult* have = PQexec(conn, "SELECT to_regclass('public.schema_version') IS NOT NULL");
  bool exists = false;
  if (have && PQresultStatus(have) == PGRES_TUPLES_OK && PQntuples(have) == 1) {
    exists = std::strcmp(PQgetvalue(have, 0, 0), "t") == 0;
  }
  if (have) PQclear(have);
  if (exists) {
    PGresult* v = PQexec(conn, "SELECT COALESCE(MAX(version), 0) FROM schema_version");
    int64_t ver = 0;
    if (v && PQresultStatus(v) == PGRES_TUPLES_OK && PQntuples(v) == 1) {
      parse_int64_text(PQgetvalue(v, 0, 0), ver);
    }
    if (v) PQclear(v);
    if (ver == 13) return;  // idempotent second run: no-op
    if (ver > 0) {
      throw std::runtime_error(
          "pg_ensure_schema: pre-existing PG schema at version " + std::to_string(ver) +
          " (< 13); PostgreSQL stores are created fresh at v13 -- drop/recreate the "
          "database or migrate via the SQLite path and re-export");
    }
  }
  run("BEGIN");
  try {
    run(kPgSchemaSql);
    run("COMMIT");
  } catch (...) {
    PGresult* rb = PQexec(conn, "ROLLBACK");
    if (rb) PQclear(rb);
    throw;
  }
}

#endif  // QBRAIN_WITH_PG

}  // namespace qbrain::storage

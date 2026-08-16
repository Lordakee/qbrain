#pragma once
#include <memory>
#include <string>
#include <string_view>

#include "qbrain/storage/backend.hpp"

#ifdef QBRAIN_WITH_PG
#include <libpq-fe.h>
#endif

// N38 D1: libpq PostgreSQL backend for the N35 IStorageBackend contract.
//
// Like SqliteBackend (src/qbrain/storage/database.cpp), the concrete
// PgBackend class is defined entirely in src/qbrain/storage/pg_backend.cpp;
// this header exposes only the factory and the free-function surface other
// layers need (tests, harness, Brain wiring). The libpq-dependent half is
// guarded by QBRAIN_WITH_PG and therefore only visible when the build
// discovered a PostgreSQL installation (CMakeLists.txt /
// scripts/build-cl.ps1; discovery order: $QBRAIN_PG_ROOT ->
// D:\PostgreSQL\<max version> -> C:\Program Files\PostgreSQL\<max version>,
// highest version directory wins). When libpq is absent the pure helpers
// below still compile and link (they need no libpq), so dialect/redaction
// unit tests run on any machine; pg_backend.cpp itself is always compiled.
//
// Deviations from the SQLite backend that callers must know about (each is
// also documented at its implementation site in pg_backend.cpp):
//   - open() takes a DSN (keyword/value or postgresql:// URI), not a file
//     path; an empty argument reads $QBRAIN_PG_DSN (never argv). Every
//     error text that can echo the DSN passes through pg_redact_dsn()
//     (whitelist host/port/dbname/user, drop everything else -- password
//     included), and the exact password value is additionally scrubbed
//     from every message the backend throws.
//   - last_insert_rowid() is RETURNING-based: INSERT statements routed
//     through the prepared-statement layer whose target table has an
//     identity "id" column get " RETURNING id" injected at prepare time
//     (guarded by a catalog check, so tables without an identity id --
//     tags, schema_version, sources, config, job_aggregation_fence, temp
//     tables -- are never rewritten). step()/step_done() swallow the
//     returned row and keep SQLite's "INSERT steps to DONE" shape.
//     exec()-path INSERTs do not update last_insert_rowid(); every
//     production caller of last_insert_rowid() uses prepared statements.
//   - busy semantics: SQLSTATE 55P03 / 40001 / 40P01 map to the busy error
//     class (int rc 5, message marker "database is locked"); see the
//     SQLSTATE table in pg_backend.cpp. PG cannot fail a blocked lock
//     instantly, so open() bounds the wait with lock_timeout (default
//     2000 ms; set_busy_timeout(ms) maps ms<=0 to 1 ms -- PG's
//     lock_timeout=0 means "wait forever", the exact inverse of
//     sqlite3_busy_timeout(0)).
//   - BEGIN IMMEDIATE maps to BEGIN TRANSACTION (deferred): PG takes row
//     locks at the first data statement; contention surfaces through the
//     lock_timeout busy mapping above. Callers' busy-retry loops are the
//     contract-visible behavior and keep working unchanged.
//   - backup_to() prefers pg_dump (found next to the loaded libpq DLL, on
//     PATH, or under the discovery roots; $QBRAIN_PG_DUMP_EXE overrides
//     exclusively). When pg_dump is absent or fails it falls back to a
//     structured COPY export of every public table (SQL text with a header
//     documenting the downgrade) -- the fallback always exists, so the
//     return value is true whenever the export file was written.
//   - ranking parity: fts_search uses tsvector + GIN (ts_rank with
//     title=weight A / body=weight B; rank is reported negated so "ORDER
//     BY rank ASC" and the search layer's score = -rank keep the SQLite
//     bm25 sign conventions). bm25 and ts_rank are different ranking
//     functions; identical relevance ORDER is only asserted for simple
//     exact-term queries (same top page), never bit-for-bit.
//
// Brain wiring point (owned by the parent / slice B, NOT this file):
// Brain::open should, before opening SQLite, call pg_dsn_from_env(); when
// non-empty it constructs the backend via make_pg_backend(dsn), then runs
// pg_ensure_schema(pg_conn_of(*backend)) and refuses to start unless
// SELECT COALESCE(MAX(version),0) FROM schema_version == 13.

namespace qbrain::storage {

// ---- pure, libpq-free helpers (compiled always) ----

// Redact a DSN of either accepted form (keyword/value, postgresql:// URI)
// down to the whitelisted "host=... port=... dbname=... user=..." view.
// Missing components fall back to the libpq defaults displayed literally
// (localhost / 5432). Anything else in the input -- password, passfile,
// options, sslmode, ... -- is dropped, never echoed. Used by every error
// path of PgBackend and unit-tested directly (P2-2 negative-test support).
std::string pg_redact_dsn(std::string_view dsn);

// Mechanical SQLite-to-PostgreSQL placeholder translation: '?' (and '?NNN')
// become '$N'. Quoted-string aware: '?' inside single-quoted strings
// (including '' escapes and E'...' backslash escapes), double-quoted
// identifiers, [bracket] identifiers, -- line comments, /* block */ comments
// and PostgreSQL $tag$ dollar-quoted spans is left untouched. A bare '?'
// takes max-index-so-far + 1 (the SQLite rule); '?NNN' pins index N. When
// param_count is non-null it receives the number of distinct parameters
// (the highest assigned index).
std::string pg_translate_placeholders(std::string_view sql, int* param_count = nullptr);

// The canonical v13-equivalent PostgreSQL DDL body applied by
// pg_ensure_schema (identity PKs, timestamptz, bytea embedding, tsvector
// generated column + GIN, COLLATE "C" on slug/identifier columns, idempotent
// IF NOT EXISTS, seed rows). Exposed as a pure string so DDL snapshot unit
// tests run without a server.
std::string pg_canonical_schema_sql();

// Backend-side FTS query normalization (the PG counterpart of the SQLite
// fts_quote): split on whitespace, strip embedded double quotes, lowercase,
// rejoin with single spaces. The result feeds plainto_tsquery('simple', ?).
std::string pg_fts_normalize_query(const std::string& query);

#ifdef QBRAIN_WITH_PG

// Factory: constructs a PgBackend and opens it against dsn (empty dsn reads
// $QBRAIN_PG_DSN). The DSN comes from the caller / environment, never argv.
std::unique_ptr<IStorageBackend> make_pg_backend(const std::string& dsn);

// $QBRAIN_PG_DSN, or "" when unset/empty.
std::string pg_dsn_from_env();

// Apply the canonical v13-equivalent schema on an open, idle connection:
// CREATE IF NOT EXISTS everything (tables, indexes, tsvector generated
// column + GIN, seed rows; schema_version seeded 1..13) inside one
// transaction. A pre-existing schema_version above 0 but below 13 is
// rejected with guidance (PG databases are born at v13; SQLite is the
// migration path). Idempotent: at version 13 it returns without touching
// anything.
void pg_ensure_schema(PGconn* conn);

// The PGconn behind an IStorageBackend, or nullptr when the backend is not
// a PgBackend. For pg_ensure_schema wiring and test seams only; the backend
// owns the connection's lifetime.
PGconn* pg_conn_of(IStorageBackend& backend);

// Human-readable note describing which backup path the backend used last
// ("pg_dump" or "copy-fallback: <reason>"); "no backup yet" before the
// first call. The downgrade is also recorded in the export file header.
std::string pg_backup_note_of(IStorageBackend& backend);

#endif  // QBRAIN_WITH_PG

}  // namespace qbrain::storage

# Storage Contract (N35)

**Date**: 2026-08-15
**Source of authority**: `docs/nodes/N35-PLAN.md` (approved, round-2 audit PASS), `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` v2.0.0 §4 N35, `docs/RESOLUTION-2026-08-15.md` (N35 definition)
**Contract suite**: `tests/test_n35.cpp` — single registration `n35_contract_suite`, 8 assertion groups (G1–G8)

---

## 1. Purpose

The storage layer is fixed as an **explicit, falsifiable contract** so that
"replace the backend" is a provable boundary instead of a hidden assumption.
This document defines the interface semantics, the SQLite implementation
notes, and the admission rules any future backend must pass before a ledger
`implemented` claim is allowed. N35 itself introduces **no second backend**.

## 2. Contract surface

The contract is the **public** surface everything tests against:

| Surface | Role |
|---------|------|
| `qbrain::storage::Database` | open/close, `exec`, `prepare` (prepared statements with bind/reset/step/column access), transactions via SQL (`BEGIN`/`COMMIT`/`ROLLBACK`) |
| `qbrain::storage::apply_migrations` | versioned schema bring-up (v1..v13 today), idempotent |
| `qbrain::storage::check_schema_integrity` | required-objects assertion (doctor fails closed on violations) |
| `qbrain::Brain` | the API consumers use; delegates to `Database` |

**D1 interface (landed)**: `include/qbrain/storage/backend.hpp` defines
`IStorageBackend` — `open`/`close`/`is_open`/`exec`/`last_insert_rowid`/
`changes`/`create_statement` (returning the nested `IStatement`: prepare/
reset/clear_bindings/bind_\*/step/step_done/column_\*), transactions
(`begin_transaction`/`begin_immediate_transaction`/`commit_transaction`/
`rollback_transaction`, identical semantics to the exec-driven SQL forms),
busy handling (`set_busy_timeout`) and error classification
(`last_error_code` as int SQLite rc) — plus the `make_sqlite_backend()`
factory. `storage::Database` keeps its exact pre-N35 public API and
delegates one-for-one to the interface (a `static_assert` proves the member
type is the interface type); the SQLite adapter is a line-by-line extraction
of the former method bodies, zero logic change. The N35 contract suite
exercises only the public `Database`/`Brain` surface and therefore runs
unmodified against the refactored and pre-refactored implementations alike.
**N38 D0.5 amendment**: `handle()` is no longer part of the contract — the
capabilities that previously escaped through the raw `sqlite3*` are modeled
as `backend_file_path()` / `backup_to()` / `fts_search()` interface methods;
`handle()` survives only as the documented SQLite test hook on
`storage::Database`. The second backend admitted under this amended contract
is PostgreSQL (§8).

## 3. Error semantics — `int` (SQLite rc) → error class mapping (P2-4)

The interface error model returns **`int` result codes (SQLite `rc` values)**;
no predefined enum is introduced. Result codes map to documented classes:

| rc | Symbol | Class | Observable marker (current `Database` surface) | Suite anchor |
|----|--------|-------|------------------------------------------------|--------------|
| 0 | `SQLITE_OK` | ok | no error | all groups |
| 100 | `SQLITE_ROW` | step-has-row | `Statement::step() == true` | G3, G4 |
| 101 | `SQLITE_DONE` | step-complete | `Statement::step() == false` | G3, G4 |
| 5 | `SQLITE_BUSY` (+`SQLITE_BUSY_SNAPSHOT` 517) | **busy — retryable** | `std::runtime_error` carrying `database is locked` | G2, G8 |
| 19 | `SQLITE_CONSTRAINT` (e.g. extended 2063 `SQLITE_CONSTRAINT_UNIQUE`, 787 `SQLITE_CONSTRAINT_FOREIGNKEY`) | **constraint violation** | `UNIQUE constraint failed: <table>.<column>` / `FOREIGN KEY constraint failed` | G8 |
| 1 | `SQLITE_ERROR` (incl. parser failures) | **syntax / general error** | `near "...": syntax error` | G8 |

Rules locked by the suite (G8): the three error classes — constraint
violation, syntax error, busy — are **pairwise distinguishable** observable
outcomes; no class marker leaks into another class's message. On the legacy
`Database` surface failures surface as `std::runtime_error` whose text embeds
`sqlite3_errmsg`/`sqlite3_errstr` output (preserved verbatim by the SQLite
adapter); the `IStorageBackend` contract additionally exposes classification
as `int` SQLite rc via `last_error_code()` (and `set_busy_timeout(ms)`
returns the `sqlite3_busy_timeout` rc). A future backend must keep the
classes distinguishable through whatever its public surface is.

## 4. SQLite implementation notes

- **Connection setup** (`Database::open`): `PRAGMA foreign_keys = ON`,
  `journal_mode = WAL`, `synchronous = NORMAL`. `busy_timeout` is **not** set
  (0): a losing writer fails fast with `SQLITE_BUSY`. Busy is therefore an
  expected, **distinguishable and retryable** state (G2); callers own the
  retry loop (existing pattern: jobs/minions code and `test_n34.cpp`).
- **Transactions**: plain SQL `BEGIN`/`COMMIT`/`ROLLBACK`. Atomicity is
  observable cross-connection: an uncommitted row is invisible to a second
  connection, `ROLLBACK` leaves a byte-exact unchanged dump, `COMMIT` is
  visible everywhere (G1). WAL readers are never blocked by a writer (G2).
- **Prepared statements**: a `Statement` may be re-executed only after
  `reset()` (plus `clear_bindings()`); rebinding then produces a **new** row
  carrying the **new** value — the N34 rebind bug class is locked at contract
  level (G3), including the SELECT-rebind variant.
- **FTS**: `pages_fts` is an FTS5 external-content table
  (`content='pages'`, `content_rowid='id'`, unicode61), maintained by the
  `pages_ai`/`pages_ad`/`pages_au` triggers; MATCH queries return exactly the
  expected rows through the public search path and raw SQL (G4).
- **Indexes**: `idx_pages_source_slug` (migration v2) exists and backs exact
  `(source_id, slug)` lookups (G4). Index creation is idempotent
  (`CREATE INDEX IF NOT EXISTS`, AA6).
- **Embedding blobs**: `content_chunks.embedding BLOB` stores a packed
  little-endian `float32` array (`search::pack_f32`/`unpack_f32`); write via
  `Brain::update_chunk_embedding`, read back **byte-identical** (sha256 over
  the raw blob, G5). **Vector search semantics are out of scope here** — see
  §6.
- **Backup**: performed through the SQLite online backup API
  (`sqlite3_backup_*`). Per adopted P2-3 backup is **not an
  interface-mandated capability**: the contract suite drives the C API
  test-side and proves payload byte-identity after
  `PRAGMA wal_checkpoint(TRUNCATE)` plus a functioning restore-copy (G6).
  Byte-identity means: equal file sizes, every differing byte confined to
  the three self-maintained header history fields (file change counter
  24..27, schema cookie 40..43, version-valid-for 92..95 — per-file
  bookkeeping that the backup commit legitimately rewrites on the
  destination), and identical sha256 over the payload with those 12 bytes
  masked. The N34 pre-migration file backup (`migrate.cpp`) uses the same
  API and converts the copy to `journal_mode=DELETE` so the artifact is one
  self-contained file.
- **Migrations**: versioned steps v1..v13; fresh bootstrap applies the
  embedded canonical v1 schema (`include/qbrain/storage/schema_sql.hpp`,
  single source of truth) then v2..v13 in one call, leaving exactly one
  `schema_version` row per version (13 rows, `MAX(version)==13`); a second
  open is a no-op; a canonical-v1 database upgraded by `apply_migrations`
  reaches v13 with full real DDL applied (G7).

## 5. Future-backend admission rules (hard gate)

A future storage backend (PostgreSQL or any other engine) may be recorded as
`implemented` in `docs/OPS-PARITY-LEDGER.md` **only after all** of the
following are green in the main tree, on both build paths, two consecutive
rounds:

1. The **full existing unit suite** passes through the unmodified public
   `Database`/`Brain` surface (zero test modifications — test expectations
   ARE the contract; P1-4: any failure triggers root-cause analysis, never a
   test-expectation change).
2. `tests/test_n35.cpp` **`n35_contract_suite` passes** (all 8 groups G1–G8).
3. **Migration subset** (G7) passes: fresh bring-up to the current schema
   version in one apply, second open a no-op, oldest-supported-shape →
   current full apply.
4. **Concurrency subset** (G2) passes: two connections, busy distinguishable
   and retryable through a bounded retry loop.

Any single failing group blocks the ledger claim. More complex isolation
scenarios (multi-writer schedules, snapshot isolation guarantees beyond the
G1/G2 observations) are recorded as admission-suite **extensions** to be
added when a concrete second backend is proposed (P2-2).

## 6. Explicit deferrals

| Item | Status | Owner / condition |
|------|--------|-------------------|
| **PostgreSQL backend** | **Delivered by N38 (see §8)** — supersedes the original N35 deferral ("only when product scope and operational requirements justify it"); scope locked `full` by the D0 census (zero UNRESOLVED structural items) + DSN provisioned at approval time (`docs/nodes/n38-evidence/DSN-PROVISIONED.json`). | N38 (`docs/nodes/N38-PLAN.md`) |
| **Vector search contract** | **Deferred to N33 domain** (similarity ranking, ANN/top-k semantics). N35 verifies only the embedding blob storage path (byte-identity, G5). | N33 / future vector-contract node |
| **Complex isolation scenarios** | Deferred with the concurrency admission extensions (§5). | Future second-backend proposal |

## 7. Contract suite map (`n35_contract_suite`)

| Group | Contract assertion |
|-------|--------------------|
| G1 | Transaction atomicity: rollback provably invisible (counts + byte-exact dump, both connections); commit visible everywhere |
| G2 | Busy semantics: exclusive writer + second connection → busy error observable, classified, and digested by a bounded retry loop |
| G3 | Prepared rebind regression lock: same statement, bind/reset/rebind → second row with the new value (insert + select variants) |
| G4 | FTS MATCH returns exactly the expected rows (public search path + raw SQL); `idx_pages_source_slug` exists and returns the exact `(source_id, slug)` row |
| G5 | Embedding blob byte-identity: public write path → raw read sha256-identical → public read path bit-identical round-trip |
| G6 | Backup byte-identity via test-side SQLite backup API (payload sha256 identical; only documented volatile header fields may differ); restore-copy answers row-exact queries and FTS on the restored file |
| G7 | Migration suite: fresh v13 (single row per version), second open no-op, canonical-v1 → v13 full apply, storage-level idempotence |
| G8 | Error classification: constraint / syntax / busy pairwise distinguishable, no marker leakage |

---

## 8. PostgreSQL backend (N38)

**Status**: scope `full` (D0 census `docs/nodes/n38-evidence/SQL-CENSUS.json`:
306 statements, 18 structural, 0 UNRESOLVED — all resolved by the D0.5
interface extensions; DSN provisioned at approval per P0-2). SQLite remains
the default backend; PG is explicit opt-in via `QBRAIN_PG_DSN`
(config: `docs/03-BUILD-WINDOWS.md` PG 节).

### 8.1 Interface coverage

`PgBackend` (`src/qbrain/storage/pg_backend.cpp`, header
`include/qbrain/storage/pg_backend.hpp`) implements the full N35+D0.5
`IStorageBackend` surface:

| Contract member | PgBackend implementation |
|---|---|
| `open(dsn)` / `close` / `is_open` | DSN is keyword/value or `postgresql://` URI; empty argument reads `$QBRAIN_PG_DSN` (never argv) |
| `exec` / `last_insert_rowid` / `changes` | `last_insert_rowid()` is RETURNING-based: prepared INSERTs on identity-`id` tables get ` RETURNING id` injected at prepare time (catalog-guarded; `exec()`-path INSERTs do not update it — every production caller uses prepared statements) |
| `create_statement` → `IStatement` (prepare/reset/clear_bindings/bind_*/step/step_done/column_*) | placeholders translated `?`/`?NNN` → `$N` at prepare (`pg_translate_placeholders`, quoted/comment/dollar-quote aware) |
| `begin_transaction` / `begin_immediate_transaction` / `commit` / `rollback` | `BEGIN IMMEDIATE` maps to deferred `BEGIN TRANSACTION` (PG takes row locks at first data statement; contention surfaces via lock_timeout) |
| `set_busy_timeout(ms)` | maps to `lock_timeout`; `ms<=0` → 1 ms (PG `lock_timeout=0` means wait forever — the inverse of sqlite semantics); open default 2000 ms |
| `last_error_code()` | SQLite-rc int semantics (see §8.2) |
| `backend_file_path()` | connection descriptor (host/dbname), never the password |
| `backup_to(dest)` | pg_dump subprocess preferred; structured COPY export fallback (see §8.4) |
| `fts_search(query, limit, source_id)` | tsvector + GIN (see §8.3) |
| pure helpers (always compiled, no libpq) | `pg_redact_dsn`, `pg_translate_placeholders`, `pg_canonical_schema_sql`, `pg_fts_normalize_query` |
| `handle()` | **not applicable** — removed from the contract by D0.5; SQLite test hook only |

Schema bring-up: `pg_ensure_schema(PGconn*)` applies
`pg_canonical_schema_sql()` idempotently (16 v13 tables, identity PKs,
timestamptz, bytea embedding, tsvector generated column + GIN, `COLLATE "C"`
on slug/identifier columns, seed rows, `schema_version` seeded 1..13) in one
transaction. A `schema_version` above 0 but below 13 is **rejected with
guidance** (PG databases are born at v13; SQLite is the migration path).

### 8.2 Error mapping — SQLSTATE → contract error class

The PG backend maps PostgreSQL SQLSTATEs onto the **same int rc semantics**
the SQLite backend exposes, so callers' classification logic is unchanged:

| PG SQLSTATE | Meaning | Contract class (SQLite rc) | Observable marker |
|---|---|---|---|
| 55P03 / 40001 / 40P01 | lock_not_available / serialization_failure / deadlock_detected | **busy — retryable** (SQLITE_BUSY 5) | `database is locked` |
| 23505 / 23502 / 23503 / 23514 | unique / not-null / FK / check violation | **constraint violation** (SQLITE_CONSTRAINT 19) | `UNIQUE constraint failed: …` style |
| 42601 | syntax_error | **syntax / general error** (SQLITE_ERROR 1) | `syntax error` style |
| anything else | — | general error (SQLITE_ERROR 1) | raw libpq message |

G8-equivalent rule: the three classes stay pairwise distinguishable and no
class marker leaks into another class's message (`tests/test_n38.cpp`
integration G8 asserts this against the live server).

### 8.3 Busy semantics and ranking parity

- **Busy**: PG cannot fail a blocked lock instantly; the backend bounds every
  wait with `lock_timeout` (open default 2000 ms, `set_busy_timeout` maps
  onto it). Busy surfaces as the contract busy class and the callers'
  existing bounded busy-retry loops digest it unchanged (G2-equivalent:
  advisory-lock contention observable, classified, retryable).
- **Transaction identity semantics (documented deviation)**: PG identity
  sequences do not roll back with the transaction — a rolled-back INSERT
  still consumes its identity value, so the next insert's `id` skips ahead
  (asserted as `"1:alpha;3:gamma;"` in the PG integration G1; SQLite's
  suite keeps its own `1;2` shape). Row-level atomicity and visibility are
  identical (rollback invisible, commit visible cross-connection).
- **Ranking parity (bm25 vs ts_rank)**: SQLite FTS5 ranks with `bm25(...)`;
  PG ranks with `ts_rank(...)` over a tsvector (title weight A, body weight
  B; rank reported negated so `ORDER BY rank ASC` and `score = -rank` keep
  the SQLite sign conventions). These are **different ranking functions**;
  the contract asserts identical behavior only for simple exact-term queries
  (same top page), never bit-for-bit score parity. Multi-term relevance
  ordering may legitimately differ between backends.

### 8.4 Backup paths

`backup_to(dest)` prefers a **pg_dump** subprocess (plain SQL): pg_dump.exe
is looked up next to the loaded libpq DLL, on PATH, or under the discovery
roots; `$QBRAIN_PG_DUMP_EXE` overrides exclusively. When pg_dump is absent
or fails, the backend falls back to a **structured COPY export** of every
public table (SQL text whose header documents the downgrade). The fallback
always exists, so the return value is `true` whenever the export file was
written; `pg_backup_note_of(backend)` reports which path was used
(`"pg_dump"` or `"copy-fallback: <reason>"`). Restorability semantics are
therefore weaker than SQLite's byte-identity backup (§4): the PG contract
asserts a written, content-carrying, restorable-in-principle artifact; the
test prints which path was exercised (`[N38-G6]`).

### 8.5 Admission result (§5 gate applied to N38)

Per §5, a backend is recorded as `implemented` only when the full unit
suite + contract G1–G8 equivalents + migration + concurrency subsets are
green in the main tree on both build paths, two consecutive rounds. The N38
admission evidence lives in `docs/nodes/n38-evidence/`
(`C-VERIFY-NODSN.txt`: 40 registered, 39 SQLite items unmodified + `n38`
unit group green + visible `[SKIP-PG]`; `C-VERIFY-DSN.txt`: integration
group G1–G8 + product smoke green against the provisioned DSN). The ledger
backend note (`docs/OPS-PARITY-LEDGER.md`) states exactly what those runs
proved — storage-contract level + the exercised product-level smoke subset —
and nothing more.

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
`IStorageBackend` — `open`/`close`/`is_open`/`handle` (documented escape
hatch)/`exec`/`last_insert_rowid`/`changes`/`create_statement` (returning the
nested `IStatement`: prepare/reset/clear_bindings/bind_\*/step/step_done/
column_\*), transactions (`begin_transaction`/`begin_immediate_transaction`/
`commit_transaction`/`rollback_transaction`, identical semantics to the
exec-driven SQL forms), busy handling (`set_busy_timeout`) and error
classification (`last_error_code` as int SQLite rc) — plus the
`make_sqlite_backend()` factory. `storage::Database` keeps its exact pre-N35
public API and delegates one-for-one to the interface (a `static_assert`
proves the member type is the interface type); the SQLite adapter is a
line-by-line extraction of the former method bodies, zero logic change. The
N35 contract suite exercises only the public `Database`/`Brain` surface and
therefore runs unmodified against the refactored and pre-refactored
implementations alike.

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
| **PostgreSQL backend** | **Deferred** — no current product-scope or ops justification (RESOLUTION-2026-08-15 N35 clause: "only when product scope and operational requirements justify it"). The ledger claims **no** backend parity beyond SQLite; absence is explicit, not implicit. | Future requirements-driven **Phase-3 proposal** |
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

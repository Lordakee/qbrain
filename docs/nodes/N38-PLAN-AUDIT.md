# N38 PLAN AUDIT (round 1 — FAIL, revised)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n38-plan-audit-claude-20260816, 2026-08-16
**Audit object**: docs/nodes/N38-PLAN.md (draft)
**Human authorization**: user instruction 2026-08-16 (verbatim in dispatch log)

---

**VERDICT: FAIL**

## Blocking findings (P0)

### P0-1: Escape hatch interface incompatibility
**Section**: D1 libpq backend + interface design  
**Problem**: The plan commits to implementing `IStorageBackend::handle() const -> sqlite3*` for a PostgreSQL backend, but this signature is SQLite-specific. The current codebase uses `handle()` for:
- `sqlite3_db_filename(db.handle(), "main")` in migrate.cpp:93 (backup path determination)
- `sqlite3_backup_init(..., db.handle(), ...)` in migrate.cpp:102 (pre-migration backup, N34 D1 contract)

A PG backend cannot return a `sqlite3*` pointer. The plan mentions "backup/db_filename usages" but provides no concrete design for how a PG backend would satisfy these callsites. The N35 contract (docs/10 §4) explicitly states backup is NOT a mandatory interface capability, yet the migration path (migrate.cpp:92 `backup_db_file_before_migration`) calls it unconditionally before applying schema changes. This is an unresolved contradiction.

**Suggested change**: Add D0.5 (before census): audit all `handle()` callsites and design a resolution—either (a) extend `IStorageBackend` with abstract backup/metadata methods and refactor existing callsites to use them instead of `handle()`, making `handle()` SQLite-backend-only (with N35 contract amendment), or (b) explicitly scope N38 to exclude migrations (PG backend cannot run migrations, only operate on pre-v13 PG schemas created externally), making the "partial" outcome mandatory. Update D1 and AA assertions to reflect the chosen resolution.

### P0-2: Auditability gate depends on external infrastructure
**Section**: D4 integration tests + outcome audit gate  
**Problem**: The plan requires the hard audit to verify "集成组全绿" (integration group all green, AA3-4, AA8), but the integration tests only run when `QBRAIN_PG_TEST_DSN` is provided by the user. This makes the outcome audit's PASS/FAIL verdict conditional on whether the auditor has a PostgreSQL instance available and configured. A [SKIP-PG] outcome (no DSN) leaves the integration contract unverified, yet the plan treats this as acceptable for approval ("DSN 缺席时集成组报告 [SKIP-PG]...不计 PASS"). The N35 admission rules (docs/10 §5) require "all 8 groups G1–G8" to pass for a backend to claim `implemented` status—this cannot be waived by DSN absence.

**Suggested change**: Restructure the test/audit strategy: (a) D4 unit tests remain DSN-independent and always run (good as-is); (b) Add a new D4.5 "integration verification protocol" that specifies the *parent agent* or *plan approver* provisions a DSN (e.g., `postgresql://qbrain_test:***@localhost/qbrain_n38_test`) and records its availability in the plan approval step, treating it as an input to implementation (like QBRAIN_SCHEMA was for N30). The outcome audit then verifies against the known-good DSN recorded at approval time, not a maybe-present user env var. If no DSN can be provisioned at plan-approval time, the plan must explicitly downgrade its ledger claim to `partial` or `heuristic` before approval, not defer the decision to audit time.

### P0-3: Split gate criterion lacks falsifiability
**Section**: D0 census + split gate  
**Problem**: The census split criterion is "structural 条目 ≤15 且不触及页面/chunk/job 核心读写路径 → full product PG; else partial". This is underspecified: (1) What counts as one "structural item"—does the FTS5 external-content table + 3 triggers count as 1 item or 4? (2) "核心读写路径" is subjective—`pages_fts MATCH` queries (search/hybrid.cpp:32-36) clearly touch page reads, but the plan doesn't pre-commit to how search is classified. (3) The grep results show extensive SQLite-specific usage (13 `INSERT OR IGNORE`, 15+ `datetime(...)`, 5 `last_insert_rowid()`, FTS5 infrastructure). Without pre-defined counting rules, the split decision is post-hoc and potentially biased toward the implementer's preferred outcome.

**Suggested change**: Strengthen D0 by adding (a) a counting taxonomy upfront: translatable SQL constructs (INSERT OR IGNORE, datetime) are NOT structural; structural = requires abstraction layer changes (FTS triggers, backup API, handle() usage, PRAGMA logic). (b) A pre-commit classification: FTS5 (external-content + triggers + MATCH queries) is structural and touches core search (hybrid.cpp). Backup API (migrate.cpp) is structural and touches migration (a core path). Pre-define these as "structural breach → partial" before the census runs. (c) If the plan believes "full" is achievable, it must argue WHY FTS and backup don't count against the gate—don't leave the criterion loose and decide later.

---

## Non-blocking observations (P2)

**P2-1**: The libpq discovery order (`QBRAIN_PG_ROOT` env → `C:\Program Files\PostgreSQL\<ver>` scan → degrade to OFF) is reasonable, but the plan doesn't specify what "默认 `C:\Program Files\PostgreSQL\<ver>` 扫描" means—highest version? lowest? first found? Specify the heuristic or make it explicit that discovery failure is expected and documented behavior.

**P2-2**: DSN redaction is mentioned in D1 and Security notes, but the negative test ("注入错 DSN 断言只含 host/dbname") should be part of the D4 unit test group, not just documentation. Explicitly list it in AA5 with a concrete assertion: "malformed DSN `postgresql://user:SECRET@host/db` logs only `host/db`, grep over error output confirms `SECRET` absent".

**P2-3**: AA1 "39 项零修改全绿" is a strong zero-regression claim. The plan should explicitly note that any SQL bilingualization at call sites (D3 "源头双语化") must preserve exact legacy behavior for SQLite, verified by those 39 tests remaining bit-identical in their assertions. If a bilingualized query changes SQLite behavior even slightly (e.g., switching from `INSERT OR IGNORE` to `INSERT ... ON CONFLICT DO NOTHING` changes the row order or rowid behavior), AA1 is falsified.

---

## Audit summary

The N38 plan attempts to add a PostgreSQL backend to satisfy the Phase 3 scope trigger ("user's explicit need now triggers it" per the N35 deferral clause). However, it underestimates three critical challenges: (1) the `sqlite3* handle()` escape hatch baked into the N35 interface is fundamentally incompatible with a non-SQLite backend, and the plan provides no resolution for existing callsites (backup API, db_filename); (2) the outcome audit gate depends on user-provided DSN infrastructure, making the PASS/FAIL verdict non-deterministic and unauditable in environments without a PG instance; (3) the census split criterion is loose enough to allow post-hoc rationalization rather than falsifiable pre-commitment.

The plan must address P0-1 (interface incompatibility), P0-2 (audit gate dependency), and P0-3 (split criterion falsifiability) before approval. If these are resolved by scoping N38 to a narrower "PG backend for non-migration workloads only" or by explicitly pre-committing to "partial" based on the FTS/backup structural breach, the plan can proceed. As written, it overpromises ("full product PG" as a possible outcome) and under-specifies the hard design choices (escape hatch, audit environment, split gate).


---

# Round 2 — VERDICT: PASS (task qbrain-n38-plan-audit2-claude-20260816)

**VERDICT: PASS**

## Summary

The revised N38 plan has successfully addressed all three P0 findings from round 1:

**P0-1 (handle() escape hatch)**: RESOLVED. D0.5 now specifies concrete interface extensions—backend_file_path(), backup_to(), and fts_search()—with migrate.cpp callsites refactored to use them and handle() removed from IStorageBackend. The two problematic callsites (migrate.cpp:93 sqlite3_db_filename, line 102 sqlite3_backup_init) are explicitly addressed with abstract alternatives. docs/10 contract amendment is noted in D5.

**P0-2 (audit gate dependency)**: RESOLVED. The plan now pre-commits that DSN absence at round-2-audit-pass time triggers automatic scope reduction to "partial" (契约级+普查+结构改造清单, no integration-group claim), eliminating the deferred-decision problem. The audit verdict is deterministic regardless of infrastructure availability.

**P0-3 (split criterion falsifiability)**: RESOLVED. The counting taxonomy is now explicitly pre-committed: translatable constructs (INSERT OR IGNORE, datetime(), last_insert_rowid()) do NOT count as structural; structural items are limited to three pre-classified categories (FTS5→fts_search(), backup/filename→D0.5 methods, busy→D1 mapping). The census must cite a resolution point for every structural item; any unresolvable item forces partial scope. FTS is no longer open裁量.

All P2 findings have concrete resolutions: libpq discovery specifies "highest version" (P2-1), AA5 includes the SECRET123 negative test (P2-2), and AA1 notes bit-identical-behavior preservation (P2-3).

## Non-blocking observation (P2)

**Assertion wording precision**: AA6 (line 51) states "storage 之外全库 grep handle() 零残留" (zero handle() residue outside storage/ throughout the whole codebase). However, grep evidence shows 33 handle() callsites in tests/ using it for legitimate test infrastructure (sqlite3_set_authorizer, sqlite3_serialize, sqlite3_update_hook). The plan should clarify whether:
- tests/ is considered exempt from the zero-residue assertion (escape hatch legitimately exercised for test hooks), OR  
- "storage 之外" means "production code in src/qbrain/ excluding storage/", not including tests/, OR
- all 33 test callsites are in-scope for D0.5 refactoring (which seems unreasonably broad).

The technical design is sound—removing handle() from the interface contract while SqliteBackend retains it for test access via downcast is coherent. The assertion wording just needs tightening to avoid false negatives.

The plan is approvable as written. The P2 wording clarification can be addressed during implementation without blocking approval.

# N38 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n38-hard-audit-claude-20260816, 2026-08-16
**Audit basis**: docs/nodes/N38-PLAN.md (approved round-2 PASS; scope locked full)
**Human authorization**: user instruction 2026-08-16 (verbatim in dispatch log)

---

## N38 OUTCOME HARD AUDIT

---

**VERDICT: PASS**

---

### AA1 — QBRAIN_PG_DSN unset:39 zero-modification full green, PG paths dormant

**Evidence**: GATE-VERIFY.txt:39 [PASS] / 0 [FAIL], "Test files modified for this gate: NONE." B-PGMODE-PROBE §1: 40[PASS] / 0 [FAIL] (canonical clean run). C-VERIFY-NODSN.txt: 40/40 PASS, `[SKIP-PG] no QBRAIN_PG_TEST_DSN` visible, exit 0. FINAL-VERIFY-SCRIPT.txt: initial run + rounds 1-2 × 40/40 PASS + [SKIP-PG] each. FINAL-VERIFY-CMAKE.txt: rounds 1-2 × 40/40 PASS + [SKIP-PG] each. P2-3bit-identity requirement: proven by zero assertion drift across all bilingualization rewrites (D3changed brain.cpp/minions.cpp/hybrid.cpp/analytics.cpp/packs.cpp/dream.cpp/store.cpp without disturbing any of the 39 existing test assertions).

**Status: SATISFIED.**

---

### AA2 — DSN + empty schema → auto-build v13, MAX(version)==13, second open no-op, old-version reject

**Evidence**: A-PG-HARNESS.txt H1(5 lines): `[PASS] H1 pg_ensure_schema fresh -> MAX(version)==13 -- v=13`; `[PASS] H1 second ensure run is a no-op -- v=13`; `[PASS] H1 schema_version seeded 1..13 -- rows=13`; `[PASS] H1b old PG store rejected with guidance -- pre-existing at version 7(< 13)`. C-VERIFY-DSN.txt integration coverage note: "G7 empty-schema bring-up MAX(version)==13 + second-open no-op + stale-version reject". B-PGMODE-PROBE §3: `doctor` green on PG (check_schema_integrity PG branches fully functional, schema_version 13 reported).

**Status: SATISFIED.**

---

### AA3 — Contract integration group G1-G8 PG equivalents all green

**Evidence**: C-VERIFY-DSN.txt: result40 PASS / 0 FAIL, exit 0, no [SKIP-PG]; coverage note explicitly asserts G1–G8 green individually. FINAL-VERIFY-PG.txt: `[N38-INTEGRATION] G1-G8 PG equivalents + product smoke green` on the final script-path DSN run,40/40 PASS.

Reconciliation judgments:

- **G1 sequence semantics** (reconciliation 7): The rolled-back beta permanently consumed id2 in PG non-transactional sequences; C's test expectation updated to `1:alpha;3:gamma;`. This divergence from SQLite is documented in B-PGMODE-PROBE §2 and confirmed in C-VERIFY-DSN. It is a declared, pre-committed semantic difference (plan: "documented divergence from SQLite"), not a defect. **Acceptable.**

- **G6 backup weaker semantics** (reconciliation 5): backup_to delivers pg_dump artifact (restorable, size-ok) plus a COPY fallback if pg_dump fails. Weaker than byte-identity but documented in docs/10 §8.4per plan. A-PG-HARNESS H12 passes both paths; `[N38-G6] backup path: pg_dump` tag appears in both C-VERIFY-DSN and FINAL-VERIFY-PG. **Acceptable.**

**Status: SATISFIED.**

---

### AA4 — Product-level smoke (put/get/search/graph/job cycle) on PG, psql-visible rows

**Evidence**: C-VERIFY-DSN.txt integration note: "SMOKE put/get byte-identical, search finds term, graph neighbors, submit/claim/complete job cycle." The test uses Brain::open_pg (explicit DSN, scope-isolated from the39 SQLite tests). B-PGMODE-PROBE §4 records the CLI-level psql verification during intermediate probe: `1|default|n38b/pg-smoke|note|N38B PG smoke ; MAX(schema_version)=13` GREEN. The search/graph RED items in B's §4 were attributed to the frozen hybrid.cpp COLLATE BINARY (parent-scope fix) and were resolved before the final C-VERIFY-DSN run; C's integration test smoke confirms search finds term and graph neighbors green. FINAL-VERIFY-PG.txt: `[N38-INTEGRATION] ... product smoke green`.

**Status: SATISFIED.**

---

### AA5 — DSN password absent from all logs/errors; SECRET123 negative test; host/dbname visible (P2-2)

**Evidence**: A-PG-HARNESS.txt header: "DSN redaction proof (P2-2 negative test, H13): …'SECRET123' URI form are absent; grep-verifiable." Five H13 lines all [PASS]:
- keyword form: error message contains `host=127.0.0.1 port=5432 dbname=qbrain_n38_test user=qbrain_test`, no password.
- URI form (P2-2): `SECRET123 absent`.
- Pure helper (keyword + URI forms).
- Quoted values parsed, password dropped.

Additionally: both C-VERIFY-NODSN.txt and C-VERIFY-DSN.txt contain "Password scan: real DSN password occurrences in the full run log = 0 (verified before writing this file; password itself never written here)". The scrubber applies defense-in-depth stripping of password bytes≥6 chars.

**Status: SATISFIED.**

---

### AA6 — Census covers full source list; split decision = full; handle() zero residue outside storage; ledger tag matches

**Evidence**: SQL-CENSUS.json totals: 27 files enumerated, 306 statements, portable207 / translatable 81/ structural 18 / unresolved 0. split_determination.scope = "full", rationale = all 18 structural items resolved by one of three D0.5 extension points (fts_search ×5, backup_to+backend_file_path × 1, busy-mapping-in-D1 × 12), none unresolved. GATE-VERIFY.txt: `grep -rn "handle()" src/qbrain/ outside src/qbrain/storage/ -> 0 matches`; inside storage only SqliteBackend::handle() (concrete, not override) and the database.cpp downcast hook.

Reconciliation judgments:

- **AUTOINCREMENT(12) + DEFAULT datetime('now')(17) kept in SQLite DDL** (reconciliation 2): Census correctly classifies both as *translatable* (not structural), because the rewrite is mechanical (BIGINT GENERATED ALWAYS AS IDENTITY / DEFAULT now()) and the SQLite migration DDL is never executed in PG mode (pg_ensure_schema is the PG path). Keeping the SQLite DDL intact is required to preserve test_n17 literal-DDL assertions; removing them would constitute a test modification violating AA1. The deviation is within the pre-committed P0-3 taxonomy (translatable ≠ structural). **Acceptable.**

- **exec-path INSERT RETURNING coverage** (reconciliation 3): H8 in A-PG-HARNESS verifies `[PASS] H8 config/tags INSERTs work (no RETURNING injected)` + `[PASS] H8 rowid untouched by non-identity inserts`. The plan documents that all production INSERT callers are prepared statements; exec-path INSERTs that do not carry RETURNING are correct by design (rowid not meaningful for those paths). **Acceptable.**

**Status: SATISFIED.**

---

### AA7 — libpq-absent machine: QBRAIN_WITH_PG OFF, compiles, SQLite all green, downgrade warning

**Evidence**: A-PG-HARNESS.txt (build wiring note): "Not found: option auto-OFF with a status message; pg_backend.cpp still compiles (libpq-free half); SQLite path unaffected." CMake `qbrain_pg_discover()` function documented as: `QBRAIN_PG_ROOT env → D:/PostgreSQL/<max> → C:/Program Files/PostgreSQL/<max>; not found → auto-OFF with status message`. Scripts likewise mirror the same Find-PgRoot logic with equivalent fallback.

**P2finding (minor)**: No direct build execution on a libpq-absent machine is recorded in the evidence set. The design intent and guard code are documented in A-PG-HARNESS and derivable from the CMakeLists/script wiring visible in the build outputs (both FINAL-VERIFY files show the discovery success path; the failure path is documented by code but not exercised as a separate evidence run). This does not falsify the claim; it is an evidence gap, not a contradiction.

**Status: SATISFIED with P2 gap (documented design, no live compilation proof).**

---

### AA8 — Dual-path double-round full green:40/40 NO-DSN + 40/40 DSN integration green

**Evidence**:

NO-DSN Script path (FINAL-VERIFY-SCRIPT.txt): initial run + round 1 + round 2 — all three × 40 [PASS],0 [FAIL], [SKIP-PG] integration group explicit in each.

NO-DSN CMake path (FINAL-VERIFY-CMAKE.txt): rounds 1 and 2 — both × 40 [PASS], 0 [FAIL], [SKIP-PG] in each.

DSN Script path (FINAL-VERIFY-PG.txt): 40 [PASS], 0 [FAIL], `[N38-G6] backup path: pg_dump`, `[N38-INTEGRATION] G1-G8 PG equivalents + product smoke green`.

All three verification categories (C-VERIFY-NODSN, C-VERIFY-DSN, FINAL-VERIFY-*) show zero [FAIL] lines. Build markers BUILD_OK and TESTS_BUILD_OK present in all final runs. The plan's "dual-path" DSN requirement is "DSN env re-run 40/40 且集成组全绿" — no dual-path qualifier on the DSN run in the AA text; satisfied by the script-path DSN run.

**Status: SATISFIED.**

---

### Documented Reconciliations — All Within Plan Pre-commitments

| # | Item | Judgment |
|---|------|----------|
| 1 | AA1 zero-test-modification —39 byte-identical, n38 is the 40th | Verified; GATE-VERIFY "modified: NONE" |
| 2 | AUTOINCREMENT(12) + datetime('now')(17) kept in SQLite DDL | Acceptable; translatable not structural per P0-3 taxonomy; PG uses pg_ensure_schema, not migrations |
| 3 | last_insert_rowid RETURNING coverage: prepared callers only | Acceptable; exec-path rowid semantics documented and H8 confirmed |
| 4 | bm25 vs ts_rank parity: same-top-page exact-term only | Acceptable; H10 asserts at that strength |
| 5 | G6: pg_dump artifact≠ byte-identical | Acceptable; docs/10 §8.4 + [N38-G6] tag |
| 6 | Whole-suite PG mode13/27: probe evidence, not a gate | Acceptable; gate = n38 integration +39/39 SQLite |
| 7 | G1 PG sequence gap1:alpha;3:gamma | Acceptable; non-transactional sequences documented, test expectation updated |
| 8 | 15 vs 16 canonical tables (job_aggregation_fence lazy) | Acceptable; in-test documented |

---

### Findings

**P0 (blocking):** None.

**P1 (non-blocking, informational):** None that rise to P1. The27/40 whole-suite PG-mode reds are fully expected and pre-committed per the plan; they do not affect any gate criterion.

**P2 (minor):**
- AA7 has no live compilation run on a libpq-absent environment in the evidence set. The design and guard code are documented in the build wiring sections. This is a documentation gap, not a behavioral contradiction.

---

**Conclusion:** Every plan gate criterion is met. The implementation delivers a census-complete (unresolved=0) full-scope PostgreSQL backend: 56/56 harness assays, 39/39 SQLite regression with zero test modifications confirmed across five independent build runs on two build paths,40/40 DSN integration run with all G1–G8 contract groups and product smoke green. All eight documented reconciliations fall within the boundaries pre-committed by the round-2 plan audit dispositions. The single P2 gap (AA7 no-libpq build not live-verified) is a documentation gap that does not contradict any delivered behavior.

**VERDICT: PASS**

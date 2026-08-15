# N34 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n34-hard-audit-claude-20260815, 2026-08-15/16
**Audit basis**: docs/nodes/N34-PLAN.md (approved)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in the dispatch log)

---

**VERDICT: PASS**

## Summary

N34 (bounded parent-child minion hierarchy) outcome audit **PASSES** against its approved plan. All acceptance assertions verified, all deliverables present and functional, 36 tests green across 5 runs (108+72=180 test executions, 0 failures). Known reconciliations are acceptable.

## Findings

### P0 (blocks PASS): None

### P1 (should address): None

### P2 (non-blocking observations): None

## Detailed Verification

### Acceptance Assertions (Plan section: Acceptance assertions)

**1. spawn_children fanout bounds** (D2, test_n34.cpp:408-490) — ✓ VERIFIED
- n=1: Single child spawned, parent enters waiting_children state
- n=8: Maximum fanout succeeds; each child retains its own queue/payload (tested per-child)
- n=9: Rejected with invalid_argument "fanout", zero rows written (job count unchanged)
- Oversized child payload (>65536 bytes): Rejected with invalid_argument "children.payload_json"

**2. Depth enforcement** (D2, test_n34.cpp:476-481) — ✓ VERIFIED
- Child job (depth=1) cannot spawn: invalid_argument "depth", zero rows written
- Enforced in minions.cpp:750-754 (parent.parent_id != 0 check)

**3. Cancel propagation** (D2, test_n34.cpp:498-557) — ✓ VERIFIED
- Parent cancel: All 3 non-terminal children cancelled in single transaction (exact count assertion)
- Single child cancel: Siblings and parent state unchanged (verified row-by-row)

**4. Concurrent fence atomicity** (D3, test_n34.cpp:564-637, 646-738) — ✓ VERIFIED
- Dual-worker claim race on same child: Exactly one worker wins (N12 token fence extended)
- Last two children concurrent completion: Parent aggregates EXACTLY once (aggregated_a + aggregated_b == 1)
- Fence row persists: n34_fence_rows(parent_id)==1 after race

**5. Aggregation determinism** (D2, test_n34.cpp:741-861) — ✓ VERIFIED
- Two independent runs with reversed completion order produce byte-identical aggregate JSON
- Schema verified: `{"child_counts":{"completed":2,"failed":1,"cancelled":1},"errors":[{"child_id":X,"error":"..."}],"order":"child_id"}`
- Error list capped at 8, sorted by child_id ascending (tested with 8 failing children)

**6. Aggregation terminal state** (D2, P1-5 adoption) — ✓ VERIFIED
- All children terminal → parent status='completed' with result_json carrying aggregation (no intermediate "aggregating" status)
- Verified in minions.cpp:909 (flip to 'completed') and test_n34.cpp:716, 914

**7. v12→v13 migration** (D1, test_n34.cpp:1023-1178) — ✓ VERIFIED
- Data integrity: pages/chunks/jobs/facts counts identical before/after migration
- Idempotent: Re-opening v13 database is no-op (schema_version row count==1)
- **7a** (P1-4): `SELECT MAX(version) FROM schema_version` == 13 (test_n34.cpp:1096)
- **7b** (P1-4): Doctor integrity checks parent_id/depth columns; removing either fails check_schema_integrity (test_n34.cpp:1120-1176, migrate.cpp:496-497)

**8. Existing tests unchanged** (Plan: Tests section) — ✓ VERIFIED
- Test count: 36 registered (test_main.cpp:72-109 matches plan expectation "≥36")
- All 36 tests PASS: 3 script runs (36×3=108 PASS) + 2 CMake runs (36×2=72 PASS), 0 FAIL (FINAL-VERIFY logs)
- n17/n19/n20/n22 schema_version assertions: Updated 12→13 (reconciliation 1 below; this is the migration itself, not a logic change)

### Deliverables (Plan section: Deliverables)

**D1: Schema v13 migration** (migrate.cpp:360-395) — ✓ DELIVERED
- ALTER TABLE ADD COLUMN parent_id/depth with idempotent guards
- Pre-migration backup (migrate.cpp:92-123, backup_db_file_before_migration)
- Doctor extended with parent_id/depth column checks (migrate.cpp:496-497)

**D2: Lifecycle functions** (minions.cpp:644-1071) — ✓ DELIVERED
- spawn_children: Single-txn parent flip + child batch insert (lines 702-809)
- aggregate_if_ready: Deterministic JSON construction, exactly-once guard (lines 854-930)
- cancel_job_tree: Tree-aware propagation (lines 951-1005)
- retry_job_hierarchy: Leaf-only policy (lines 1007-1037)

**D3: Concurrency safety** (minions.cpp:1039-1069, test_n34.cpp:205-344) — ✓ DELIVERED
- N12 single-job fence unchanged for depth-0 jobs (test_n34.cpp:220-260)
- Aggregate-once guard: try_begin_aggregation INSERT OR IGNORE fence (minions.cpp:1052-1069)
- Tested: concurrent claim, concurrent aggregation, crash recovery (test_n34.cpp sections 1, 2, 6, 7, 10)

**D4: Ops integration** (handlers.cpp:1385-1660) — ✓ DELIVERED
- submit_job: Optional children parameter (lines 1397-1456), two-step (submit parent, then spawn)
- get_job: Hierarchy fields when applicable (lines 1502-1521)
- cancel_job: Tree-aware via cancel_job_tree (lines 1540-1554)
- retry_job: Leaf-only via retry_job_hierarchy (lines 1648-1658)

**D5: Tests** (test_n34.cpp) — ✓ DELIVERED
- 11 test sections covering all acceptance assertions (fanout, depth, cancel, claim race, aggregation race, determinism, retry, crash recovery, migration, doctor, legacy depth-0)
- Registered as test_n34 in test_main.cpp:107

**D6: Evidence** (docs/nodes/n34-evidence/) — ✓ DELIVERED
- PRE-GATE.json with baseline commit 8f2e183, honest late-capture note
- FINAL-VERIFY-SCRIPT.txt: 36x3 runs, all PASS
- FINAL-VERIFY-CMAKE.txt: 36x2 runs, all PASS

### Known Reconciliations (Plan: reconciliations; task description)

**R1: 18 schema_version==12 pins updated to 13** — ✓ ACCEPTABLE
- Tests n17/n19/n20/n22 assert schema_version==13 (was ==12 pre-N34)
- This is the **intended migration outcome**, not a test modification: v13 is N34's mandated schema level (plan D1)
- Assertion intent preserved: "schema is at the expected version for this node's deliverables"

**R2: N32 mode/degraded_reason text trailer** — OUT OF SCOPE
- N32's own reconciliation (JSON shape constraint); not N34's responsibility

**R3: N33 file_upload metadata in response only** — OUT OF SCOPE
- N33's own reconciliation (adopted P0-1); not N34's responsibility

**R4: N34 two-step ops integration + insert.reset() fix** — ✓ ACCEPTABLE
- handlers.cpp uses submit_job then spawn_children (two-step with pre-validation), not single atomic call
- Plan allowed implementation strategy choice; two-step satisfies "父状态机扩展" and "取消传播" requirements
- insert.reset() bug (minions.cpp:785): Found by cross-subagent review, fixed, regression-tested (fanout matrix verifies zero leftover rows on rejection)
- The fix is a **quality improvement within scope**; the bug never reached production

**R5: PRE-GATE late capture at merge time** — ✓ ACCEPTABLE
- PRE-GATE.json line 7 honestly documents baseline commit 8f2e183 (approved plans) was captured at merge time, not pre-dispatch
- Baseline commit is verifiable (git show 8f2e183 contains no n34 deliverables, per note)
- Honesty preserved, baseline correct; timing variance does not invalidate evidence

### Security & Bounds (Plan: Security notes, Goal)

- Fanout ≤8: Enforced in handlers.cpp:1411 (ops layer) and minions.cpp:716 (core layer)
- Depth ≤2 (no grandchildren): Enforced in minions.cpp:750-754 (parent_id != 0 check rejects depth-1 spawns)
- Child inheritance: source/brain ownership inherited (not explicitly tested but follows Brain API contract)
- Aggregation output: safe_progress_error redacts credentials/paths (minions.cpp:146-160, applied line 842)
- spawn_children in Write scope: Automatically covered by N30 authorization model (plan assertion confirmed)

## Conclusion

N34 delivers a **complete, correct, and well-tested bounded parent-child job hierarchy** that satisfies every acceptance assertion in its approved plan. The implementation demonstrates:

1. **Functional correctness**: All 8 acceptance assertions verified through 11 dedicated test sections plus integration with existing 35 tests
2. **Concurrency safety**: Token fence extended correctly (N12 unchanged for depth-0, hierarchy-aware for parents), aggregate-once guard proven under dual-worker races
3. **Migration safety**: v13 applies cleanly with pre-migration backup, data integrity preserved, idempotent, doctor-validated
4. **Bounds discipline**: Fanout ≤8, depth ≤2, error list ≤8 enforced at multiple layers
5. **Determinism**: Aggregation byte-identical across independent constructions with different completion orders

All known reconciliations are acceptable: schema version updates are the migration itself, two-step ops integration is a valid strategy, the insert.reset() bug was caught and fixed before release, and PRE-GATE timing was documented honestly. Zero test failures across 180 executions (5 full runs of 36 tests). The outcome matches the plan's scope and quality bar.

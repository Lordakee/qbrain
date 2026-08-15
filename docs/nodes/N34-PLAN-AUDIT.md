# N34 PLAN AUDIT (round 1)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n34-plan-audit-claude-20260815, 2026-08-15
**Audit object**: docs/nodes/N34-PLAN.md (draft as dispatched)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in dispatch log)

---

**VERDICT: PASS**

## Findings

### P1 (Should address before implementation)

**P1-1: Ledger terminology imprecision** (Section: Ledger rows moved to implemented)
- **Problem**: The header "Ledger rows moved to implemented" lists operations (`submit_job`, `get_job`, `list_jobs`, `cancel_job`, `retry_job`, `complete_job`) that already exist in the codebase (confirmed in `include/qbrain/jobs/minions.hpp` and `src/qbrain/jobs/minions.cpp`). The phrase "moved to implemented" falsely implies these operations were previously unimplemented. They are being *extended* with hierarchical capabilities, not newly implemented.
- **Suggested change**: Retitle section to "Ledger rows enhanced/extended" or add explicit clarification: "N34: hierarchy support added to existing operations" to distinguish enhancement from initial implementation.

**P1-2: Missing state machine details** (Section: Deliverables D2)
- **Problem**: D2 mentions "父 waiting→waiting_children" and "waiting_children→聚合就绪" but `waiting_children` and "聚合就绪" are not defined as job statuses. The current `Job` struct (minions.hpp:23) uses string status field with known values (waiting/active/completed/failed/cancelled/paused). It's unclear whether `waiting_children` is: (a) a new status value requiring schema documentation, (b) internal state only, or (c) descriptive prose not matching implementation.
- **Suggested change**: Explicitly state whether new status values are added to the jobs table's status column. If `waiting_children` is a new status, add it to acceptance assertions and ensure schema/migration documents the valid status enum. If it's internal-only state derived from (parent_id IS NOT NULL AND children exist with non-terminal status), document that mapping clearly.

**P1-3: Token fence hierarchical extension underspecified** (Section: Goal, D3)
- **Problem**: The plan states "沿用 N12 token-fence 扩展到层级检查" and D3 mentions "token fence 扩展 N12 测试模式到层级" but provides no technical detail on what the hierarchical extension entails. Does it prevent concurrent claim of sibling jobs? Does it check parent_id during fence validation? Current minions.cpp uses lock_token for single-job claim atomicity; how this extends to parent-child relationships is unspecified.
- **Suggested change**: Add explicit technical requirement: "Token fence ensures: (1) sibling jobs of same parent cannot be claimed concurrently by different workers [OR specify actual constraint]; (2) parent aggregation is atomic even when last two children complete concurrently; (3) existing N12 fence behavior (single job claim atomicity) preserved."

**P1-4: Schema version assertion gap** (Section: Acceptance assertions)
- **Problem**: Assertion 7 addresses v12→v13 migration data consistency but doesn't explicitly assert that the `schema_version` table will record `version=13` after migration success, nor that `check_schema_health` will correctly validate the new `parent_id` and `depth` columns.
- **Suggested change**: Add explicit assertions: "7a. After v13 migration, `SELECT MAX(version) FROM schema_version` returns 13. 7b. Schema health check (migrate.cpp doctor function) validates presence of columns `jobs.parent_id` and `jobs.depth`; removal of either column causes health check to fail."

**P1-5: Aggregation state transition ambiguity** (Section: D2, Acceptance assertion 6)
- **Problem**: D2 describes "最后一个子完成→父 waiting_children→聚合就绪" but assertion 6 says "全部子终态后父聚合就绪状态正确" without defining what "聚合就绪" means operationally. Is this: (a) parent transitions to `completed` status immediately, (b) parent remains in intermediate state requiring explicit aggregation call, or (c) parent enters a new status awaiting handler pickup?
- **Suggested change**: Clarify terminal state: "After last child reaches terminal status (completed/failed/cancelled), parent job transitions to [specify exact status: 'completed' with aggregation in result_json, OR 'waiting' for handler re-claim, OR new status 'aggregating']."

### P2 (Non-blocking, recommend addressing)

**P2-1: Crash recovery test informality** (Acceptance assertion, Tests section)
- The crash recovery test description "进程内模拟：创建子后'重启'即重开 brain，状态一致可继续" uses informal language. For rigorous outcome audit, specify: which exact state fields (parent status, child status, attempt counts, lock tokens) must survive close/reopen cycle, and what "可继续" means (parent re-aggregation succeeds? children can still complete?).

**P2-2: Aggregation JSON schema undefined** (Section: D2)
- D2 describes aggregation content ("child 计数按状态、错误摘要 ≤8 条、顺序按子 id") but doesn't provide the exact JSON schema. For byte-identical determinism verification (assertion 5), consider documenting the schema explicitly, e.g., `{"child_counts": {"completed": N, "failed": M, ...}, "errors": [{child_id: X, error: "..."}], ...}`.

**P2-3: Test count arithmetic** (Section: Tests)
- Plan claims "全套件 ≥36" while current baseline (test_main.cpp) is 29, implying +7 tests from N34. Verify this count is realistic: D5 specifies test_n34.cpp covering ~8 test categories (fanout matrix, depth, cancel, retry, fence, aggregation, crash recovery, existing tests). Count appears reasonable but should be confirmed during implementation.

**P2-4: Doctor health check update timing** (Section: Parallelism notes)
- Plan correctly notes "v13 迁移需同步扩展 doctor 完整性清单（本节点内完成）" but doesn't specify whether this happens in D1 (migration) or D5 (tests). For clarity, specify that migrate.cpp's `check_schema_health` function (lines 345-415) will be updated in D1 to include `jobs.parent_id` and `jobs.depth` in the required columns check.

**P2-5: Subagent ownership boundary precision** (Section: Parallelism notes)
- Subagent slice proposal "A=迁移+生命周期；B=并发/fence+聚合+test_n34" could specify exact file ownership: A owns migrate.cpp + jobs/minions.cpp spawn/aggregate functions + D2 subset; B owns jobs/minions.cpp fence/concurrency subset + tests/test_n34.cpp + D3/D5. This would help prevent intra-node merge conflicts.

## Audit Summary

N34-PLAN.md presents a **well-bounded, falsifiable, and parallelism-aware plan** that correctly identifies dependencies, addresses security concerns (inheritance of source/brain ownership, Write scope for spawn operations, no path/credential leakage in aggregation), and aligns with master plan v2.0.0 §4 N34 requirements. The plan correctly claims exclusive ownership of disjoint file regions (storage/migrate.cpp v13 segment, jobs/** core, tests/test_n34*) while acknowledging shared handlers.cpp requires parent-agent serial merge per AMD-3. Template compliance is complete, rollback strategy is adequate (backup + optional parameter design allows safe reversion), and acceptance assertions are predominantly falsifiable.

**Five P1 precision issues** warrant clarification before implementation to ensure the outcome audit has unambiguous success criteria: ledger terminology (enhancement vs. new implementation), state machine details (whether new status values exist), token fence hierarchical extension mechanics, schema version validation completeness, and aggregation terminal state definition. These are **clarity gaps, not fundamental architectural flaws**—the plan is implementable as written, but the outcome audit would benefit from resolving these ambiguities upfront. The **P2 observations** are refinements that would improve auditability but don't block approval.

The plan demonstrates appropriate scope discipline (fanout ≤8, depth ≤2, no external dependencies), realistic test coverage (≥36 total, +7 for hierarchy features), and honest acknowledgment of bounded subset status relative to gbrain's full minion capabilities.

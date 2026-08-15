# N30 PLAN AUDIT

**Auditor**: Claude Code (claude-opus-5, effort max) via watchdog dispatch task qbrain-n30-plan-audit-claude-20260815
**Date**: 2026-08-15
**Audit object**: docs/nodes/N30-PLAN.md (draft as dispatched)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in dispatch log)

---

**VERDICT: PASS**

The N30 plan is a well-structured, evidence-aware corrective baseline closure that correctly identifies and proposes to fix the P0 security and P1 correctness gaps confirmed by code inspection. The plan properly addresses the three-party resolution requirements (AMD-1 through AMD-11) and provides falsifiable acceptance criteria.

## P0 Findings: None (all gaps are correctly identified as work-to-be-done)

The plan correctly diagnoses the baseline defects it intends to fix:
- Authorization bypass (registry doesn't enforce declared Scope)
- Path disclosure in health/file operations
- HTTP substring routing vulnerability
- CLI uncaught exceptions
- Build divergence (CMakeLists.txt vs build-cl.ps1)
- Incomplete schema health checks
- Non-atomic pack writes

## P1 Findings (Recommend mitigation before implementation)

### P1-1: Central Authorization Enforcement Algorithm Underspecified
**Section**: Deliverable D3 (中央授权策略)  
**Problem**: The plan states that `registry.cpp` will enforce scope centrally and deny Write/Admin remotely without explicit authorization. However, D3's description doesn't specify the enforcement algorithm. The current `registry.cpp::call` (lines 31-52) only checks `local_only && ctx.remote && !ctx.allow_write`. The plan should clarify:
1. How will `Scope` (Read/Write/Admin) be enforced against request context?
2. What is "显式授权能力" - is it a token scope, an authenticated identity claim, or a capability object?
3. Will the check be `if (op->scope >= Write && ctx.remote && !has_capability(ctx)) deny;`?

**Suggested Change**: Add to D3: "Enforcement: `registry.cpp::call` checks `op->scope`. If scope is Write or Admin and `ctx.remote == true`, deny unless `ctx.authenticated_capability` explicitly permits the operation. `--allow-write` remains local-only (never set for remote). Negative matrix covers all 8+ Write/Admin ops: put_page, delete_page, file_upload, put_raw_data, submit_agent, doctor_remediate, schema_apply_mutations, takes_calibration, chronicle_backfill."

### P1-2: N24-N28 Stub Audit Correction Scope Ambiguous
**Section**: Deliverable D2, Acceptance 2  
**Problem**: AMD-7 requires "全新全标准审计或明确的 deferral 决定" for N24-N28 stub audits. The plan states this in the goal and D2, but D2's deliverable description only says "N24-N28 各获全新全标准审计或显式 deferral" without defining "全标准审计" observable structure. The current stubs (10-17 lines, minimal assertion tables) are far below N21's 100+ line structure with 断言表+发现+结论+证据哈希. 

**Suggested Change**: Add to D2 or Acceptance 2: "全标准审计 means: ≥50 lines; explicit assertions table comparing plan deliverables to evidence; findings section (even if empty); conclusion referencing specific test output hashes; evidence file references. If deferral chosen, record must state: reason (e.g., 'retrospective baseline, insufficient to re-audit'), affected ledger rows, and owner for future closure."

### P1-3: Temporal Gate Execution Timing Unclear
**Section**: Deliverable D12 (PRE-GATE.json)  
**Problem**: The plan describes "实施前时间基线 PRE-GATE.json：计划/审计哈希、范围文件清单、交付物缺席证明、构建身份、隔离数据根证明" but doesn't specify WHEN this gate runs. The text "实施前门（时间基线+双构建）必须先行完成（串行）" suggests it's a prerequisite, but the deliverable list order (D12 is last) and the phrasing "实施期产生" are contradictory.

**Suggested Change**: Move PRE-GATE.json creation to a new "Pre-implementation Gate" section before Deliverables (like N20-PLAN.md:29 structure). State: "After plan-audit PASS and status=approved but BEFORE any D1-D11 implementation edit, capture PRE-GATE.json to establish the temporal boundary between frozen baseline and N30 corrective work."

## P2 Findings (Non-blocking observations)

### P2-1: Build Script MSVC Discovery Pattern Not Specified
**Section**: D7  
**Observation**: The plan requires "MSVC 稳健发现" but doesn't specify the discovery pattern (vswhere.exe? registry? known paths with fallback?). This is an implementation detail, but explicit guidance would reduce iteration.

**Suggestion**: Consider adding to D7 or a reference: "Use vswhere.exe first, fallback to known BuildTools paths, error clearly if not found."

### P2-2: Parallelism Slice Shared-File Risk
**Section**: Parallelism notes  
**Observation**: Subagent B and C share `http_server.cpp` and `test_n30.cpp` with function-block ownership (B: authorization/redaction; C: routing parser/negative tests). This is permitted by AMD-3 ("所有权矩阵须按函数块划分") but carries merge-conflict risk.

**Suggestion**: Non-blocking. The plan acknowledges "冲突块由父代理串行合并". If conflicts prove frequent, serialize B→C instead.

### P2-3: Changeset Disposition "Rejected" Category Undefined
**Section**: D1  
**Observation**: CHANGESET-MANIFEST.json includes disposition category "rejected（不删除用户工作，拒绝须理由）". The "不删除" constraint is good, but the plan doesn't state what happens to rejected files (leave uncommitted? document-only record?).

**Suggestion**: Clarify in D1: "Rejected files remain in working tree uncommitted, with rationale recorded in manifest; they are not included in any N30 commit."

## Audit Summary

The N30 plan is a comprehensive, evidence-grounded baseline reconciliation that correctly identifies the P0 authorization, disclosure, and correctness gaps exposed by three-party resolution review. It properly addresses all amendments (AMD-1: N20/N21/N23 disposition; AMD-2: tiered evidence; AMD-4: changeset manifest; AMD-7: N24-N28 stub correction; AMD-9: no blind commits; AMD-10: security gates mandatory). The acceptance assertions are concrete and falsifiable. The deliverables are well-scoped, and the rollback strategy is non-destructive.

The three P1 findings are clarification requests, not blocking defects: the central authorization algorithm should specify the enforcement check; the N24-N28 audit standard should define observable structure; and the temporal gate timing should be unambiguous. With these minor clarifications, the plan provides a sound foundation for Phase 2 security and governance closure. Recommend approval after controller reviews P1 suggestions and decides whether to incorporate, mitigate, or accept as written.

# N35 PLAN AUDIT (round 1 — FAIL, revised)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n35-plan-audit-claude-20260816, 2026-08-16
**Audit object**: docs/nodes/N35-PLAN.md (draft)
**Human authorization**: user instruction 2026-08-15 (verbatim in dispatch log)

---

**VERDICT: FAIL**

---

## Blocking Findings (P1)

**P1-1: Vector behavior not addressed (契约套件完整性缺口)**  
**Section**: D2 契约套件, Acceptance assertions  
**Problem**: Resolution §4 N35 explicitly requires "FTS/vector behavior" coverage, but the plan only specifies FTS query assertions. Qbrain has `content_chunks.embedding` blob storage and embedding-based search (per N33). The contract suite must either (a) include vector storage/retrieval assertions (e.g., embedding blob write/read byte-identical, cosine-search path exercises stored embeddings), or (b) explicitly defer vector contract testing with rationale (e.g., "vector embeddings use SQLite blob with no backend-specific semantics; search integration is N33 domain; N35 contract verifies blob column persistence only").  
**Suggested change**: Add to D2: "向量行为：embedding blob 列读写字节一致性（或显式 deferral：向量搜索契约属 N33，N35 仅验证 blob 存储通路）" and corresponding AA entry or deferral note in Ledger rows section.

**P1-2: Test registration count imprecise (违反生成计数规则)**  
**Section**: Tests section, AA7  
**Problem**: Plan says test_n35.cpp adds "1-2 个注册项" but describes 7+ contract dimensions (transaction atomicity, busy semantics, prepared rebind, FTS, backup, error classification, migration idempotence). The vague "1-2" range conflicts with AMD-6 requirement: "测试计数必须由当前注册的可执行套件生成；散文不得携带未验证的历史计数." Before implementation, the plan should state the exact registration structure (e.g., "1 注册项 `n35_contract_suite` containing 7 sub-assertions" or "2 注册项: `n35_contract_basic` + `n35_contract_concurrency`") or explain why the count cannot be determined pre-implementation (but then AA7 "=36+新注册数" is premature).  
**Suggested change**: Revise Tests section to specify: "test_n35.cpp 注册 N 项（明确 1 或 2；若为 1 则说明内部子断言分组；若为 2 则说明维度拆分）；全套件 = 36 + N，双路径两轮全绿。精确 N 值由实现确定后 outcome audit 验证。" And revise AA7 to match.

**P1-3: Index behavior coverage implicit, not explicit (契约边界模糊)**  
**Section**: D2 契约套件, AA assertions  
**Problem**: Resolution requires "indexes" coverage but plan does not explicitly state how index behavior is verified. Migration idempotence (AA6) covers index **creation** (e.g., idx_jobs_parent from v13), but not index **usage correctness** or query-plan verification. While prepared-statement tests (AA3) may implicitly exercise indexes, the contract suite should explicitly state: "索引可用性：迁移创建全部必需索引（AA6 覆盖）；查询正确性依赖索引的语句产生预期结果（prepared/FTS 测试隐式覆盖）" or add a standalone index-usage assertion.  
**Suggested change**: Add to D2 or AA section: "索引行为：v2/v3/v13 索引创建幂等（AA6）；索引支持的查询返回正确结果（prepared/FTS 测试覆盖；或：新增 idx_pages_source_slug 查询断言）"

**P1-4: Adapter refactor risk not acknowledged (等价性保证不足)**  
**Section**: D1 Deliverables, Rollback  
**Problem**: Plan claims "零行为变化——全部 36 测试不动即证明" but the internal delegation refactor from direct SQLite C API calls (`sqlite3_exec`, `sqlite3_prepare_v2`) to `IStorageBackend->exec()` / `backend->prepare()` carries risk of subtle behavior divergence (error-handling paths, state cleanup order, transaction nesting, resource lifecycle). The 36-test pass is **verification** after refactor, not a **guarantee** before. The plan should acknowledge this risk and specify mitigation: e.g., "SQLite 后端实现为当前 Database 方法的逐行迁移，保持 check() 错误处理、resource RAII、transaction 语义字节一致；任何测试失败需根因对比分析并修正接口或后端实现，而非静默修改测试预期."  
**Suggested change**: Add to D1: "适配风险：内部委托可能引入行为漂移（错误路径、状态顺序）；缓解：SQLite 后端逐方法提取现有 Database 实现，零逻辑变更；36 测试全绿为事后等价证明；任何失败触发逐断言根因分析." And revise AA1 from "零修改全绿" to "零修改全绿（等价性验证；任何失败需根因修正）."

---

## Non-Blocking Suggestions (P2)

**P2-1: "迁移套件" scope unclear**  
D2 and D3 mention "迁移套件" as a separate gate for future backends ("同一契约套件全绿 + 迁移套件"), but it's unclear if AA6 migration-idempotence test **is** the migration suite or if a separate comprehensive migration test artifact (all v1-v13 apply cleanly, rollback artifacts verified) is expected. Clarify in D2 whether AA6 alone satisfies the migration-suite requirement or if additional deliverable is needed.

**P2-2: Concurrency suite scope**  
D3 准入规则 mentions "并发套件" but plan only specifies one concurrency assertion (AA2: busy-retry path). Is that the complete concurrency suite for storage contract, or should it cover transaction isolation, read-while-write, or multiple worker scenarios? (Job-level concurrency is N34's domain, but storage-level concurrent access is N35's.) Clarify coverage or note that AA2 **is** the concurrency suite.

**P2-3: Backup method not specified**  
AA4 verifies backup-restore byte-identity but doesn't specify the mechanism: does `Database` gain a `backup(path)` method? Or does test_n35.cpp call SQLite backup API directly? Auditor needs to verify the interface supports this. Add to D1: "Database::backup(dest) 方法（或：契约套件直接使用 SQLite backup API，非接口强制能力）."

**P2-4: Error classification return type ambiguous**  
AA5 says errors are distinguishable "不同错误码或分类枚举" but D1 doesn't specify if `IStorageBackend` methods return raw `int` (SQLite codes) or a new `StorageError` enum. If raw codes, future PostgreSQL backend must map PG errors to SQLite-equivalent codes (fragile). If enum, define it now. Clarify: "接口错误语义：返回 int（SQLite rc）+ 文档映射表，或定义 StorageError{Ok, Constraint, Syntax, Busy, Other} 枚举."

---

## Audit Summary

The plan correctly interprets the resolution's PostgreSQL explicit-deferral requirement and proposes a sound adapter architecture. Acceptance assertions are falsifiable. Rollback is adequate (no schema change, single-commit refactor). The plan is auditable for a hard outcome gate.

**However**, the contract suite has four gaps that must be closed before approval: (1) vector behavior not addressed despite resolution requirement; (2) test count imprecise, violating the generated-count rule; (3) index coverage implicit rather than explicit; (4) refactor risk not acknowledged, creating false confidence in "zero behavior change" claim.

Fix P1-1 through P1-4, address or defer P2 suggestions, then resubmit for plan audit. The node is well-scoped and the deferral treatment is correct; the issues are completeness and precision, not fundamental design.


---

# Round 2 — VERDICT: PASS (task qbrain-n35-plan-audit2-claude-20260816)

**VERDICT: PASS**

---

## Findings

None. All round-1 findings correctly resolved.

---

## Round-2 Verification Summary

All 4 P1 blocking findings from round 1 are correctly resolved in the revised plan:

**P1-1 (Vector behavior)**: Line 11 disposition + line 30 D2 addition confirm embedding blob read/write byte-consistency assertion added, with explicit deferral of vector search contract to N33 domain (line 25 Ledger entry). Resolution requirement satisfied.

**P1-2 (Test count precision)**: Lines 12, 30, 35, 47 now consistently state exact registration structure: single item `n35_contract_suite` with 7 sub-assertions, total suite = 37. Vague "1-2" replaced with precise "36+1=37". AMD-6 compliance achieved.

**P1-3 (Index coverage)**: Line 13 disposition + line 30 D2 addition make index behavior explicit: creation idempotence (AA6) + query correctness via prepared/FTS coverage + new `idx_pages_source_slug` query assertion. No longer implicit.

**P1-4 (Adapter risk)**: Lines 14, 41 acknowledge refactor risk and specify mitigation: line-by-line extraction from existing Database methods, zero logic change, 36-test pass as *post-hoc* equivalence verification, and crucially: "任何失败触发根因分析而非改测试预期" (any failure triggers root-cause analysis, not test-expectation modification). Core concern addressed.

All 4 P2 suggestions addressed (line 15): migration suite = AA6 + v1→v13 full apply; concurrency suite = AA2 storage-level busy-retry; backup via SQLite API directly (not interface-mandatory); error semantics = int return + docs/10 mapping table (no enum).

No new blocking issues introduced. Acceptance assertions remain falsifiable (lines 39-48). PostgreSQL explicit-deferral treatment maintained correctly (lines 19, 25, 48). Scope boundaries precise: storage contract boundary definition, SQLite implementation extraction, contract test suite; no second backend in N35 per resolution constraint.

The plan is now complete, precise, auditable, and ready for implementation gate.

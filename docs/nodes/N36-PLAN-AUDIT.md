# N36 PLAN AUDIT (round 1 — FAIL, revised)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n36-plan-audit-claude-20260816, 2026-08-16
**Audit object**: docs/nodes/N36-PLAN.md (draft)
**Human authorization**: user instruction 2026-08-15 (verbatim in dispatch log)

---

**VERDICT: FAIL**

The plan is architecturally sound and follows the established node pattern, but contains one blocking resolution-compliance gap and several P1 clarity/robustness issues that must be addressed before approval.

---

## Findings

### P0 — Blocking

**P0-1: Resolution clause reconciliation missing (§Goal, new subsection needed)**

**Problem**: Resolution line 238 states "tenant, brain, source, file, and job isolation is proven with cross-tenant negative tests" for implemented token auth, but the plan implements only operation-level scopes (read/write/admin) without tenant/brain/source identity. The plan's single-tenant loopback-only boundary may satisfy the resolution if multi-tenancy itself is explicitly deferred, but the plan doesn't defend this interpretation or cite the resolution's "或显式 deferral" language (master plan line 95).

**Suggested change**: Add a "Resolution compliance" subsection after Goal explaining: (1) N36 implements bounded static token-scoped auth per the resolution's "实现" branch; (2) the loopback-only boundary (127.0.0.1, no TLS) makes this a single-tenant product — tenant/brain/source isolation is not applicable because there are no cross-tenant requests to isolate; (3) multi-tenant identity (OAuth, dynamic user stores, per-token brain/source restrictions) is explicitly deferred to Phase 3 per the resolution's "或显式 deferral" option for the multi-tenant dimension; (4) token scopes (read/write/admin) satisfy the resolution's "scopes map to operations" requirement; (5) "cross-tenant negative tests" become "cross-scope privilege escalation tests" in the single-tenant context. Cite resolution line 238 and master plan §4 N36.

---

### P1 — Must fix before approval

**P1-1: N35 dependency unjustified (§Status line 4)**

**Problem**: Plan declares "Depends on: N35 done" but doesn't explain the serialization point. N36 touches only `mcp/auth.*` and `http_server.cpp` authorization logic; N35 is storage adapter/backend boundary. Master plan AMD-3 requires serialization only for overlapping schema/migration/authorization/hot files. Either they're disjoint (no dependency) or there's a shared file (justify).

**Suggested change**: If N35 and N36 are truly disjoint, remove the N35 dependency and note "Parallelizable with N35 per file ownership matrix: N36={mcp/auth.*, http_server.cpp bearer logic}, N35={storage/*}". If they share `registry.cpp` or authorization code, explain the conflict and keep the dependency.

**P1-2: Token config format ambiguity (§D1 line 21, §AA1 line 35)**

**Problem**: D1 says "name/token/scope 逗号列表" but AA1 example shows `:read` (single scope, colon-separated fields). Unclear if a token can have multiple scopes (`alice:token123:read,write`) or only one. Also, no maximum token length despite character-by-character comparison (DoS risk if attacker controls env and sets 1MB token).

**Suggested change**: Clarify format as `name:token:scope[,scope]` where scope list is comma-separated if multiple (e.g., `alice:....:read,write`). Add token length bounds: "≥16 字节, ≤256 字节; 超过上限的条目启动警告+跳过". Update AA1 example if multi-scope is allowed, or state "单 scope 每 token" if not.

**P1-3: Malformed request-time header behavior unspecified (§D2 line 22, new clause needed)**

**Problem**: D1 specifies config-parsing error handling ("非法条目启动警告+跳过") but D2 doesn't specify what happens with malformed `Authorization` headers at request time (e.g., "Bearer" with no space, multiple Bearers, non-ASCII bytes, token >256 bytes). Existing `check_auth` may handle some cases, but plan should be explicit.

**Suggested change**: Add to D2: "Malformed Authorization header (missing Bearer, no space, duplicate headers, token length >256 bytes) → 401. Non-ASCII bytes in token field → 401 (comparison treats as mismatch)."

---

### P2 — Non-blocking, recommend fixing

**P2-1: Constant-time assertion falsifiability vague (§AA3 line 37)**

**Problem**: AA3's parenthetical says "以代码结构断言替代" timing tests but doesn't specify what that means (grep for function name? AST inspection?).

**Suggested change**: Replace parenthetical with: "测试断言：认证路径调用 constant_time_compare 函数（通过符号可见性或调用计数验证），而非脆弱的时序均匀性测试。"

**P2-2: Test granularity unclear (§Tests line 30, §D4 line 24)**

**Problem**: Plan says "单注册项 `n36_token_scope`" but D4 lists ~8 distinct test cases (正矩阵3种scope, 负矩阵4种, 常数时间, stdio, 审计行). If it's one registered test with sub-assertions, that's fine but should be explicit; if it should be multiple registered tests, update the count.

**Suggested change**: Clarify: "单注册项 `n36_token_scope` 含子断言（正/负矩阵、常数时间存在性、stdio不变性、审计行格式）；或拆为 n36_positive_matrix / n36_negative_matrix / n36_audit_log 等多项（更新全套件计数）。"

**P2-3: Rollback scope incomplete (§Rollback line 44)**

**Problem**: Rollback mentions D1-D3 but not D4-D6 (tests, docs, evidence). Unclear if rolled-back feature leaves its tests/docs in place.

**Suggested change**: Expand: "D1-D3 单提交可回退；D4 测试在未配置 token 时全通过（零行为差异）故可保留或回退；D5 文档回退 token 配置节，保留 deferral 记录；D6 证据目录整体回退。"

**P2-4: Audit log hash prefix too short (§D3 line 23, §AA5 line 39)**

**Problem**: "sha256 前 8 字符" = 4 bytes = 32 bits. Collision risk in logs with many tokens. 16 字符 (8 bytes, 64 bits) is more robust.

**Suggested change**: Change to "sha256 前 16 字符" in D3 and AA5.

**P2-5: Invalid scope handling unspecified (§D1 line 21)**

**Problem**: D1 says "scope ∈ {read, write, admin}" but doesn't say what happens if config contains "superadmin" or "readonly".

**Suggested change**: Add to D1: "无效 scope 值（非 read/write/admin）的条目视为非法，启动警告+跳过。"

**P2-6: Token encoding/character set unspecified (§D1 line 21)**

**Problem**: "≥16 字节" but unclear if tokens are ASCII-only, UTF-8, or arbitrary bytes (URL-unsafe chars in env var?).

**Suggested change**: Add to D1: "Token 字段限 ASCII 可打印字符 [0x21-0x7E]（env 安全）；16 字节 = 16 ASCII 字符。"

**P2-7: Privilege escalation matrix incomplete (§AA2 line 36)**

**Problem**: AA2 covers "read token → write op" but doesn't explicitly list all 3 escalation paths (read→write, read→admin, write→admin).

**Suggested change**: Expand AA2: "read token 调 write → write_denied; read token 调 admin → write_denied; write token 调 admin → write_denied（三向提权全拒）。"

**P2-8: "替换接缝" terminology misleading (§Goal line 10, §Status line 4)**

**Problem**: Plan says N36 "替换 authenticated_capability 接缝" but actually *populates* the existing seam (the string pointer in OpContext stays; N36 adds token→capability mapping logic).

**Suggested change**: Change to "实现 authenticated_capability 的 token 填充逻辑" or "为 authenticated_capability 接缝提供 token 数据源".

---

## Audit Summary

N36 adopts a reasonable bounded scope: static token-based authentication with read/write/admin operation-level authorization, loopback-only (no TLS), explicitly deferring multi-tenant identity and OAuth to Phase 3. The plan follows the established node structure (deliverables, falsifiable acceptance, rollback, security) and reuses N30's central authorization gate rather than creating a bypass. However, **P0-1 is blocking**: the plan must explicitly reconcile its single-tenant scope-based approach with the resolution's "tenant, brain, source isolation" clause by clarifying that loopback-only single-tenant operation makes cross-tenant isolation non-applicable, and citing the resolution's deferral option for the multi-tenant dimension. The three P1 findings (N35 dependency, config format ambiguity, malformed-header behavior) must also be addressed before approval to ensure the plan is implementable without hidden assumptions. P2 findings are clarity/robustness improvements that strengthen falsifiability but don't block approval if time-constrained.


---

# Round 2 — VERDICT: PASS (task qbrain-n36-plan-audit2-claude-20260816)

**VERDICT: PASS**

All round 1 findings resolved. One trivial documentation inconsistency (line 23 Goal paragraph retains "前 8 字符" while D3/AA5 correctly specify "前 16 字符") does not affect implementation correctness since authoritative deliverable and acceptance criterion both specify 16-character prefix.

---

## Verification Summary

**P0-1 (resolution compliance) — RESOLVED**: Lines 17-19 add "决议合规" section establishing single-tenant loopback boundary makes cross-tenant isolation non-applicable, multi-tenant identity explicitly deferred to Phase-3, token scopes satisfy "scopes map to operations", cross-tenant tests become cross-scope escalation tests (AA2).

**P1-1 (N35 dependency) — RESOLVED**: Line 12 justifies dependency via AMD-3 serialization rule for shared authorization code path (http_server/registry consumption point); N35 already merged at 05077c9.

**P1-2 (token format) — RESOLVED**: Lines 13, 23 specify `name:token:scope[,scope]` format, 16-256 ASCII printable [0x21-0x7E], out-of-bounds entries skipped with warning.

**P1-3 (malformed headers) — RESOLVED**: Lines 14, 35 specify malformed Authorization headers (missing Bearer/no space/duplicate/token>256B/non-ASCII) → 401, treated as mismatch in comparison.

**P2-1 (constant-time assertion) — RESOLVED**: Lines 15, 37, 50 clarify assertion verifies `constant_time_compare` function presence/call-count, not timing uniformity.

**P2-2 (test granularity) — RESOLVED**: Lines 15, 42 explicit: single registration `n36_token_scope` with sub-assertion groups (positive/negative matrix, constant-time existence, stdio invariance, audit format, config bounds).

**P2-3 (rollback scope) — RESOLVED**: Lines 15, 58-59 extend rollback coverage to D4-D6 (tests preservable due to zero-behavior-delta when unconfigured; docs revert token section, keep deferral record; evidence dir rolls back entirely).

**P2-4 (hash prefix length) — MOSTLY RESOLVED**: Line 23 Goal paragraph retains stale "前 8 字符" but authoritative specs D3 (line 36) and AA5 (line 52) correctly specify "前 16 字符". Implementation will be correct; documentation inconsistency trivial.

**P2-5 (invalid scope) — RESOLVED**: Lines 15, 23 specify invalid scope values trigger startup warning and entry skip.

**P2-6 (token charset) — RESOLVED**: Lines 13, 23 constrain tokens to ASCII printable [0x21-0x7E], env-safe.

**P2-7 (escalation matrix) — RESOLVED**: Lines 15, 19, 37, 49 expand to three-way matrix: read→write, read→admin, write→admin all rejected with N30 write_denied shape.

**P2-8 (seam terminology) — RESOLVED**: Lines 4, 15, 23 reword from "替换" to "提供 token 数据源（填充而非替换）" / "喂给 N30 中央授权门".

---

No new blocking issues. Plan establishes falsifiable acceptance criteria (AA1-8), bounded scope (static token config, loopback-only, explicit multi-tenant/TLS deferrals), reuses N30 authorization gate rather than creating bypass, specifies rollback path, and addresses all substantive round 1 concerns. Ready for implementation.

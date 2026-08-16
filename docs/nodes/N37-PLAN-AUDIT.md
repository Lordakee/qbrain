# N37 PLAN AUDIT (round 1 — FAIL, revised)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n37-plan-audit-claude-20260816, 2026-08-16
**Audit object**: docs/nodes/N37-PLAN.md (draft)
**Human authorization**: user instruction 2026-08-15 (verbatim in dispatch log)

---

**VERDICT: FAIL**

## Blocking Findings (P0)

### P0-1: Exit-criterion coverage mapping incomplete (§Goal, §Deliverables)
**Problem**: Master plan v2.0.0 §6 defines 11 exit criteria. N37-PLAN.md does not explicitly map each criterion to either (a) a prior-node deliverable that already satisfied it, or (b) an N37 deliverable/AA. Criterion 8 illustrates the gap: master plan requires "MCP matrix proves remote default-deny... transaction rollback, no-secret behavior," which N30 delivered as a comprehensive Write/Admin negative matrix. N37 D4 delivers 5 smoke scenarios (CLI, stdio, HTTP auth, storage recovery, path redaction), **not** the full matrix. The plan should state: "Criterion 8 satisfied by N30 negative matrix (docs/nodes/n30-evidence/MCP-MATRIX.md or equivalent); D4 smoke verifies system behavior remains intact post-integration."

**Suggested change**: Add a new subsection **"Exit-criteria mapping"** after §Goal listing all 11 criteria with the responsible node or N37 deliverable:
1. Clean worktree → N30 (dependency)
2. Master plan coverage → D6/AA6
3. Node audit completeness → D6/AA5
4. No stale audits → D6/AA5
5. Registry/inventory sync → N31 (verified by test count AA8: 39 = ops-inventory-derived)
6. Dual-path builds → D1/AA1
7. Suite repeatability → AA1/AA8
8. MCP security matrix → **N30 (D4 smoke verifies subset)**
9. Storage integrity → **N30 schema health + N35 atomic writes (D4 smoke verifies recovery path)**
10. Gbrain parity closure → D6/AA6
11. Project-level audit → Explicit gate after node PASS (not an AA)

### P0-2: Version source ambiguity and current-state mismatch (D2, AA4)
**Problem**: 
- D2 states "version 取自 CMakeLists project VERSION **或**新增 include/qbrain/version.hpp" (OR is ambiguous).
- CMakeLists.txt:2 currently declares `VERSION 0.1.0`, but D2 and AA4 reference `2.0.0`.
- AA4 uses conditional phrasing "CMakeLists VERSION（若声明）" when VERSION **is** declared.

**Suggested change**: 
1. D2: Declare single source of truth: "CMakeLists.txt `project(qbrain VERSION 2.0.0 ...)` is authoritative; `include/qbrain/version.hpp` defines `QBRAIN_VERSION_MAJOR/MINOR/PATCH` constants derived from it, with a CMake configure step or manual sync verified by AA4 consistency check."
2. AA4: Remove conditional; state "CMakeLists.txt VERSION and version.hpp constants both declare 2.0.0; `qbrain --version` outputs `Qbrain 2.0.0` or equivalent."
3. Deliverables: Add explicit CMakeLists.txt VERSION update from 0.1.0 → 2.0.0 to D2 or as a separate line item.

### P0-3: Reproducibility criterion escape hatch too vague (D1, AA2)
**Problem**: AA2 states "两次打包 zip 字节一致性验证（可复现性；若 zip 时间戳导致不稳定则 manifest 哈希一致 + zip 内容清单一致为判据并记录）". The fallback "或记录的等价可复现判据" in D1 and the parenthetical in AA2 are both vague. What exactly is "manifest 哈希一致"? The manifest JSON content must be bitwise identical (same file list, same per-file sha256 values, same order), not just the manifest's own hash.

**Suggested change**: AA2 rewrite: "两次执行 `package.ps1` 产出的 MANIFEST.json 文件内容完全一致（逐字段 sha256 数组匹配）且 zip 内文件清单（路径+sha256 对）一致；若 zip 元数据时间戳导致 zip 整体哈希不同，manifest 内容一致+清单一致判定为可复现并记录该限制于 n37-evidence/REPRODUCIBILITY-NOTE.md；否则 zip 整体 sha256 必须一致。"

### P0-4: D6 references non-existent master plan section (D6)
**Problem**: D6 states "master plan §7 状态终版", but `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` v2.0.0 has no §7 (it has §0-§8: Changelog, 当前基线, 分层证据, Phase 1, Phase 2 DAG, 全局硬门, 完成定义, 风险, 基线). This is likely a typo for "§7 风险与缓解" but that section doesn't have a "状态" field to update.

**Suggested change**: Clarify what gets updated. If the intent is to mark Phase 2 complete in §2 (当前基线) or add a completion timestamp to the version/status header, state that explicitly: "D6: master plan v2.0.0 header status 更新为 'Phase 2 COMPLETE (2026-08-16)'; §3 Phase 2 DAG 注记 N30-N37 done；docs/09 Phase-2 completion narrative。"

### P0-5: CI build command unspecified (D5)
**Problem**: D5 says "CI 用 CMake 路径" but doesn't specify the exact workflow steps. The deliverable is `.github/workflows/ci.yml`, but what does it run? `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build` or something else? The two-path reality (scripts/build.ps1 vs CMake) needs explicit CI choice documentation.

**Suggested change**: D5 append: "workflow 内容：windows-latest runner → `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`；产物为 `build/Release/qbrain.exe` 和 `build/Release/qbrain_tests.exe` 上传为 artifact。初次运行证据要求：Actions 标签页显示 workflow 触发记录或 pending/queued 状态截图存于 n37-evidence。"

## Non-Blocking Issues (P2)

### P2-1: D3 test gap description too vague (D3)
**Problem**: "既有 util 测试核对，缺口补" doesn't specify what the gap is. Is it that `qbrain_root()` is tested but per-brain path resolution isn't? Or that Windows `%LOCALAPPDATA%` expansion isn't tested under non-default conditions?

**Suggested improvement**: "D3: docs/03 补充数据根解析规则（%LOCALAPPDATA%\Qbrain\brains\<id>\<schema-file>）；tests/test_n37.cpp 断言 `qbrain::util::paths::brain_root(brain_id)` 返回预期路径结构且 `<id>` 字节受控（不含 `..` / absolute drive）；若 test_util 已覆盖 qbrain_root 则仅补 per-brain 断言。"

### P2-2: D4 smoke evidence format unclear (D4, AA3)
**Problem**: D4 says "输出记录至 n37-evidence" but AA3 only checks "exit 0". Should there be captured stdout/stderr showing actual responses (e.g., the JSON-RPC initialize response, the 401 body, the doctor FAIL message)?

**Suggested improvement**: AA3 append: "；n37-evidence/SMOKE-{CLI,STDIO,HTTP,RECOVERY,REDACT}.txt 各捕获一次代表性输出（非空，含预期关键字：CLI 含 retrieved slug，stdio 含 `result` 对象，HTTP 401 含 `Unauthorized`，doctor 含 `FAIL` 判据，health 响应不含 `C:\` 或 `D:\` 盘符）。"

### P2-3: D7 secrets-scan tool unspecified (D7)
**Problem**: "全仓（含 dist 清单）模式扫描零命中记录" doesn't say what tool or pattern set. `git secrets`, `trufflehog`, `gitleaks`, or manual `rg` patterns?

**Suggested improvement**: "D7: 使用 `gitleaks detect --no-git --source .` 或等价工具（模式：API key/token/password/private key 正则）扫描全仓及 dist/ 内容；零真实正例（模式命中逐条评估记为 false-positive 或真实泄露）；结果与评估记录存 n37-evidence/SECRETS-SCAN.txt。"

### P2-4: Test count generation claim needs evidence pointer (AA8)
**Problem**: AA8 says "精确值来自可执行输出" but doesn't specify where that output is captured. Without a pointer, the count could be prose.

**Suggested improvement**: AA8 append: "（两轮输出文件：n37-evidence/FINAL-VERIFY-SCRIPT-RUN{1,2}.txt 与 CMAKE-RUN{1,2}.txt，每个文件末行含 `X/X tests passed` 且 X=39）。"

---

## Audit Summary

N37-PLAN.md delivers a credible packaging, CI, and documentation-reconciliation scope appropriate for a Phase-2 closeout node, with realistic Windows-native dual-path builds, version stamping, smoke testing, and governance-index verification. However, **four blocking gaps** prevent approval: (1) exit-criteria 8/9 are implicitly satisfied by prior nodes (N30/N35) but the plan doesn't map this explicitly, creating ambiguity about whether N37's 5-scenario smoke script is intended as the **sole** evidence or a **verification** of prior comprehensive matrices; (2) version-source ambiguity (CMakeLists 0.1.0 vs. plan's 2.0.0, "OR" vs. single source of truth); (3) reproducibility fallback criterion too vague (manifest content-equality not clearly defined); (4) D6 references non-existent master plan §7. Additionally, P0-5 (CI build command unspecified) is a minor blocker because `.github/workflows/ci.yml` content is underspecified. Four P2 issues (test-gap vagueness, smoke evidence format, secrets-scan tool, test-count evidence pointer) are quality improvements but non-blocking. Once the P0 findings are addressed—particularly the exit-criteria mapping showing N37 **verifies** rather than **re-delivers** N30's security matrices and N35's storage integrity proofs—the plan will meet the master plan's N37 definition and Phase-2 closure requirements.


---

# Round 2 — VERDICT: PASS (task qbrain-n37-plan-audit2-claude-20260816)

**VERDICT: PASS**

All 9 round-1 findings (5 P0 + 4 P2) resolved. No new blocking issues.

## Resolution verification

**P0 findings:**
1. **Exit-criteria mapping** (P0-1) — ✓ Lines 18-32 provide complete table mapping all 11 master-plan criteria to responsible nodes/deliverables; N30's comprehensive matrices for criteria 8/9 explicitly noted as primary evidence with D4 smoke positioned as post-integration verification subset.
2. **Version source** (P0-2) — ✓ Single source of truth declared: CMakeLists.txt VERSION (0.1.0→2.0.0 update explicit in D2 line 47); version.hpp synced; AA4 consistency check unconditional (line 65).
3. **Reproducibility criterion** (P0-3) — ✓ Precise definition added: MANIFEST.json content bitwise equality (per-field) + zip file-list (path+sha256 pairs) equality; zip-level sha256 as stronger criterion; timestamp fallback documented in REPRODUCIBILITY-NOTE.md (lines 46, 63).
4. **Master plan section** (P0-4) — ✓ D6 corrected to update plan header status "Phase 2 COMPLETE (2026-08-16)" + §3 DAG annotations + docs/09 narrative (lines 14, 51); non-existent §7 reference removed.
5. **CI commands** (P0-5) — ✓ Exact workflow steps specified: windows-latest, `cmake -G "Visual Studio 17 2022" -A x64`, `--build --config Release`, `ctest -C Release --output-on-failure`, artifact upload; first-run evidence = Actions trigger/queue status, no fabrication (lines 15, 50).

**P2 findings:**
1. **D3 test gap** (P2-1) — ✓ Disposition specifies brain-path structure + id-byte-control assertions (line 16); D3 references existing util tests + gap补充 (line 48).
2. **Smoke evidence format** (P2-2) — ✓ Five SMOKE-*.txt files with specific keyword requirements documented: CLI (retrieved slug), stdio (result object), HTTP (unauthorized), doctor (FAIL), redact (no drive letters) (lines 49, 64).
3. **Secrets-scan tool** (P2-3) — ✓ gitleaks or equivalent specified with pattern set (API key/token/password/private key regex); per-hit evaluation record in SECRETS-SCAN.txt (lines 16, 52, 68).
4. **Test-count evidence** (P2-4) — ✓ Four pointer files added: FINAL-VERIFY-{SCRIPT,CMAKE}-RUN{1,2}.txt for dual-path dual-run evidence (lines 16, 69).

## Summary

The revised N37 plan successfully addresses all audit findings. The exit-criteria mapping (P0-1 adoption) resolves the most critical ambiguity by explicitly positioning N37's smoke tests as verification of N30/N35's comprehensive security and storage-integrity matrices rather than re-delivery. Version management, reproducibility criteria, documentation reconciliation scope, and CI workflow are now precisely specified with falsifiable acceptance assertions. The 39-test baseline, 2.0.0 version target, dual-path build requirements, five-category smoke coverage, governance-index completeness check, and secrets-scan procedure form a coherent Phase-2 closeout scope consistent with master-plan §4 (N37 definition) and §6 (11-criterion exit gate). Evidence file structure supports post-execution audit. Plan approved for implementation.

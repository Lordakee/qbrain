# N32 PLAN AUDIT (round 1)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n32-plan-audit-claude-20260815, 2026-08-15
**Audit object**: docs/nodes/N32-PLAN.md (draft as dispatched)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in dispatch log)

---

---

## N32-PLAN.md Plan Audit — AST Code Intelligence

**Auditor**: Claude Code (claude-opus-5/max)
**Date**: 2026-08-15
**Scope**: Read-only; no files modified, created, or deleted.

---

## VERDICT: FAIL

Three P1 findings block approval. Two P2 issues are non-blocking.

---

### P1-1 — False ownership claim in Parallelism section violates AMD-3

**Section**: Parallelism notes
**Problem**: N32-PLAN.md asserts `"N33/N34 不触碰 handlers.cpp"`. This is factually wrong on both counts. N33-PLAN.md explicitly states it edits the `files/raw_data/search_by_image` section of handlers.cpp (`"handlers.cpp 内files/raw_data/search_by_image 相关行仅本节点改"`). N34-PLAN.md explicitly states it edits the jobs section of handlers.cpp (`"handlers.cpp仅 jobs 段"`). All three nodes touch the same physical file. AMD-3 requires an approved plan to contain a *proven-disjoint* file ownership matrix. A false matrix could cause a parallel subagent reading N32's plan to assume exclusive handlers.cpp access, clobbering the other nodes' edits at merge time.

The underlying disjointness is real (code ops section≠ files/search section ≠ jobs section), so the design is sound — but the plan text is an incorrect description of it.

**Suggested fix**: Replace `"N33/N34 不触碰 handlers.cpp"` with:

> N33 touches handlers.cpp files/raw_data/search_by_image segment; N34 touches the jobs segment; N32 touches only the code ops output field additions. All three segments are structurally disjoint. Merge is serialized by the parent agent (AMD-3); each subagent must own only its declared segment.

---

### P1-2 — astlite interface unspecified; AA6 is ambiguous and the security model is incomplete

**Section**: D1, D2, Security notes, AA6
**Problem**: D1 describes astlite as a per-file bounded parser producing a `单文件符号表`. D2 says scan.cpp adds a structured path that calls into astlite. But neither deliverable specifies whether astlite's public API accepts **(a) a content string** or **(b) a filesystem path**. This ambiguity has three concrete consequences:

1. **AA6** (`"解析限制于请求的 source 根内（路径逃逸 fixture → 拒绝）"`) is unauditable. If astlite takes content strings (the architecturally consistent choice — all existing scan paths receive page bodies from `brain.list_pages_for_source()`, never open files themselves), the path-escape constraint belongs to scan.cpp/handlers.cpp, not to astlite, and the fixture belongs to those layers, not to D1/D3. If astlite opens filesystem paths, it bypasses the brain storage layer entirely, which is an architectural break.

2. The security claim `"无路径外访问"` is unverifiable without knowing which layer is responsible for path isolation.

3. D1's time-budget description (`"每文件 ≤50ms"`) implies astlite knows it's operating on a "file", but a content-string API would be described in terms of byte or symbol counts, not files.

**Suggested fix**: Add to D1:
> astlite.hpp exposes `parse_content(std::string_view body, Language lang) -> SymbolTable` — content-in, no filesystem access, no path parameter. Language is `Cpp` or `TypeScript`.

Revise AA6 to:
> Path-escape enforcement resides in scan.cpp/handlers.cpp (source-root check before page retrieval). The astlite unit tests verify correct parsing of content strings; the scan-layer integration tests verify that a `source_id` resolving to a path outside the workspace root is rejected before any page content is retrieved.

---

### P1-3 — astlite.cpp missing from build system deliverables

**Section**: D1, D2, D4
**Problem**: D1 introduces `src/qbrain/codeintel/astlite.cpp` as a new production source file. D4 states test_n32.cpp must be registered in `"test_main + 双构建源列表"` — acknowledging both build paths for the test binary. But neither D1 nor D2 mentions updating `CMakeLists.txt` or `build-cl.ps1` (and `build-tests-cl.ps1`) for the *production* astlite translation unit. If astlite.cpp is absent from either build path, the linker fails when scan.cpp references it. N30's outcome audit documented exactly this class of divergence for store.cpp (CMake target missing the file while the PowerShell script had it); the same risk applies here to a new file in an existing module.

D4's "双构建源列表" applies to test sources; the production source update must be called out separately.

**Suggested fix**: Add to D2 (or as sub-deliverable D2b):
> Update `CMakeLists.txt` (qbrain_lib or equivalent codeintel target) and `scripts/build-cl.ps1` to include `astlite.cpp`; update `scripts/build-tests-cl.ps1` and the CMake test target to include `test_n32.cpp`. Verify a clean double-path build (CMake + direct MSVC) compiles and links both the production binary and the test binary before integration.

---

### P2-1 — Timing enforcement mechanism for the 50 ms limit is unspecified

**Section**: D1
**Problem**: D1 states `"总时间每文件 ≤50ms（超限即降级 heuristic 并标注）"` as a hard runtime limit, but doesn't specify how or when the time is sampled. In a single-threaded scan loop there is no preemption; time can only be checked between iterations. Without periodic sampling (e.g., every N lines), a pathological nested structure could run well past50 ms before any check fires. AA3 covers structural over-limit cases (>64 nesting, >2 MiB, >10000 symbols) but does not include a timing-boundary test.

**Suggested fix**: Specify in D1: time is checked every 1000 lines (or every bracket-depth transition) via `std::chrono::steady_clock`; if elapsed≥ 50 ms, astlite immediately returns a partial result with `degraded_reason: "timeout"` and structured mode is bypassed. Add a timing-limit fixture to AA3/D4.

---

### P2-2 — Regex fallback ReDoS exposure not acknowledged in security notes

**Section**: Security notes
**Problem**: The security notes correctly state `"源码视为不可信输入"` and enumerate size/depth/time limits for astlite. They do not note that the *fallback path* (the existing `DefinitionMatcher` with 6 `std::regex` patterns per symbol, plus `call_tokens_on_line`) is bounded by the N22 per-page body limit (`kN22MaximumPageBodyBytes` = 16 KiB) already in scan.cpp. Without this note, a reviewer cannot confirm the fallback is safe against crafted inputs.

**Suggested fix**: Add one sentence to Security notes: `"regex fallback 受 N22 既有 16 KiB/page 上限约束，为正则操作提供有界输入；astlite 路径完全不使用正则。"`

---

## Audit Summary

The N32 plan is honestly scoped (explicitly disclaims compiler-level type resolution, explicitly retains labeled heuristic fallback, claims no new external dependencies), and its acceptance assertions are largely falsifiable. The ledger rows and test count claim (33current registered tests + 1 →≥34) are arithmetically correct against the live `test_main.cpp`. The rollback strategy is adequate. The deliverable decomposition (D1 astlite core / D2 scan integration / D3 fixtures / D4 tests / D5 evidence) is logically coherent. Three P1 issues block approval: the parallelism section falsely states N33/N34 do not touch `handlers.cpp` when both explicitly do (in disjoint sections), violating AMD-3's requirement for an accurate ownership matrix; the astlite API interface is unspecified, making AA6's path-escape assertion ambiguous and leaving the security model's "no out-of-path access" claim unverifiable; and `astlite.cpp` is not listed as an explicit addition to either CMake or PowerShell build paths, a concrete link-failure risk with documented precedent in this project. The controller should resolve these three P1 points — the underlying design is sound and corrections are straightforward — then resubmit for approval.


---

# Round 2 — VERDICT: PASS (task qbrain-n32-plan-audit2-claude-20260815)

## N32 Plan Audit — Round 2

**Auditor**: Claude Code (claude-opus-5, effort max)
**Date**: 2026-08-15
**Audit object**: docs/nodes/N32-PLAN.md (revised after round-1 FAIL)
**History**: docs/nodes/N32-PLAN-AUDIT.md (round1)
**Cross-referenced**: N33-PLAN.md, N34-PLAN.md

---

## VERDICT: PASS

All three P1 blocking findings are correctly resolved. Both P2 non-blocking findings are addressed. No new blocking issues were introduced. One P2 residual (partial P2-1 adoption) noted below.

---

### P1-1 — Parallelism ownership matrix✅ RESOLVED

**Revised text (line 54)**: `"N33 触碰 files/raw_data/search_by_image 段、N34 触碰 jobs 段，三段结构不相交（P1-1 采纳）；handlers.cpp 的合并由父代理串行执行（AMD-3 共享热文件），各子代理仅拥有其声明段"`

Cross-checked against siblings:
- N33-PLAN.md parallelism: `"handlers.cpp 内 files/raw_data/search_by_image 段仅本节点改；N32 改 code ops 段、N34 改 jobs 段，三段结构不相交"` — ✓ consistent
- N34-PLAN.md parallelism: `"handlers.cpp 仅 jobs 段——与 N32/N33 的触碰区不相交，父代理串行合并"` — ✓ consistent

The three-segment matrix is now accurate and mutually consistent across all three node plans. AMD-3 serial-merge enforcement is stated in all three. ✓

---

### P1-2 — astlite API and AA6 security model ✅ RESOLVED

**D1 API spec (line 20)**: `"API（P1-2 采纳）：parse_content(std::string_view body, Language lang) -> SymbolTable，内容入参、无文件系统访问、无路径参数"` — matches the suggested signature exactly. ✓

**AA6 (line 38)**: `"路径隔离由 scan.cpp/handlers.cpp 层强制（source 根检查先于页内容获取；越界 source → 拒绝 fixture 在集成层测试）；astlite 单元层仅验证内容串解析正确"` — correctly relocates path-escape enforcement to the scan/handler layer, makes astlite a pure content-in unit with no filesystem concern, and specifies a falsifiable integration-layer fixture. ✓

The `"无路径外访问"` claim is now verifiable: astlite takes no path parameter; the enforcement layer is explicitly named; the fixture is at the right layer. The D1 time-budget description reads "总时间预算 ≤50ms/内容块" (not "per file"), which now aligns with a content-string API. ✓

---

### P1-3 — astlite.cpp build system registration ✅ RESOLVED

**D2b (line 21)**: `"astlite.cpp 加入 CMakeLists.txt 生产目标与 scripts/build-cl.ps1；tests/test_n32.cpp 加入 CMake 测试目标与 scripts/build-tests-cl.ps1；集成前先双路径干净构建通过（生产+测试二进制均链接成功）"` — addresses both build paths for production and test, with a pre-integration clean-build gate. ✓

D5 evidence (line 24) includes `"双路径两轮全绿"`, which now necessarily covers the D2b build verification. ✓ No link-failure risk remains.

---

### P2-1 — Timing enforcement mechanism ✅ SUBSTANTIVELY RESOLVED (minor residual)

**D1 (line 20)**: `"P2-1 采纳：std::chrono::steady_clock 每 1000 行采样一次，超限即返回 partial 结果 + degraded_reason: \"timeout\" 并降级 heuristic"` — matches the suggested sampling mechanism and return semantics. ✓

**Residual (P2, non-blocking)**: The P2-1 suggestion also called for an explicit timeout-trigger fixture in D3/AA3. D3's fixture list (line 22) enumerates `">64 嵌套、模板、overload、注释陷阱、字符串含代码、深度嵌套超限"` but does not explicitly list a timing-limit fixture (e.g., a synthetically deep structure constructed to fire the50 ms branch). AA3 (line 35) says `"超限 fixture → 受控降级 heuristic + 输出含降级原因"` without naming timeout in the fixture list. D4's `"畸形/超限降级"` implicitly covers it, and the mechanism is fully specified, so this is a minor coverage gap rather than a blocking defect. Suggested addition: add `"timeout-trigger（合成深层结构：耗时 >50ms）"` to the D3 fixture enumeration and to AA3's fixture list to make the timeout branch explicitly tested.

---

### P2-2 — Regex fallback ReDoS acknowledgement ✅ RESOLVED

**Security notes (line 49)**: `"astlite 路径完全不使用正则；regex fallback 受 N22 既有 16 KiB/page 上限约束，输入有界（P2-2 采纳）"` — matches the suggested sentence in substance (reordered, not weakened). ✓

---

### New Issues Check

No new blocking issues introduced:

- **AA6 falsifiability**: Now correctly falsifiable — integration fixture at scan.cpp layer, astlite unit tests scoped to content-string parsing only.
- **D2b scope**: Build wiring deliverable is concrete, time-gated before integration, and aligns with the PRE-GATE evidence requirement in D5.
- **Parallelism ownership completeness**: N32 explicitly owns `codeintel/** + tests/test_n32* + fixtures` (exclusive) plus the code-ops segment of handlers.cpp. This is non-overlapping with N33 (files/image_meta.\*, ai/embed, tests/test_n33\*, fixtures/img, files/raw_data/search_by_image segment) and N34 (storage/migrate.cpp v13 segment, jobs/\*\*, tests/test_n34\*, jobs segment). No new shared-file gap.
- **Test count AA8**: `"≥34 注册"` is arithmetically consistent with the round-1 audit's verification of33 current tests + test_n32 contributing≥1. N33 and N34 use the same baseline (33 + their own test file). No conflict.
- **Rollback section**: Unchanged and adequate — astlite is a new file (removable), D2 integration is gated behind a language-detection switch.

---

### Summary

The revised N32-PLAN.md correctly adopts all three P1 round-1 findings. P1-1's false ownership claim is replaced with an accurate, cross-consistent three-segment disjoint matrix (verified against N33 and N34). P1-2's API ambiguity is resolved with an explicit `parse_content(string_view, Language)` content-in signature and a correctly relocated AA6 security assertion at the scan-layer. P1-3's build-system gap is closed by the new D2b sub-deliverable covering both CMake and PowerShell build paths with a pre-integration clean-build gate. Both P2 findings are addressed: timing sampling is fully specified in D1, and the regex-fallback bound is documented in the security notes. The sole residual is a P2-level gap from P2-1: the D3 fixture list and AA3 don't explicitly name a timeout-trigger fixture, though the degradation path is covered implicitly under "超限降级". All acceptance assertions are falsifiable, the parallelism ownership matrix is accurate across all three sibling plans, and no new blocking issues were introduced. The plan is approved for implementation.

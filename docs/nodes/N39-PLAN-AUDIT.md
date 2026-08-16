# N39 PLAN AUDIT (round 1 — FAIL, revised)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n39-plan-audit-claude-20260816, 2026-08-16
**Audit object**: docs/nodes/N39-PLAN.md (draft)
**Human authorization**: user instruction 2026-08-16 (verbatim in dispatch log)

---

VERDICT: **FAIL**

---

## P1 — Blocking Findings

### P1-1 · D2/D4 Interface Contradiction — `resolve_api_key` Signature Unresolvable as Written

**Plan sections**: D2, D4, AA4

**Problem**: D4 specifies "resolve_api_key 增 rerank 用途分支" producing order `rerank_api_key → OPENAI → QBRAIN → chat_api_key`. The current signature is `resolve_api_key(const Config&, bool for_chat)` — a boolean that cannot express three purposes (embed / chat / rerank). A third branch requires either (a) an enum/tri-value signature change touching `brain.hpp`, `chat.cpp`, `embed.cpp`, and every call site, or (b) a new overload. Neither is mentioned. More critically, `chat_complete` (which `request_llm_reorder` calls) internally calls `resolve_api_key(cfg, true)` — i.e. the `for_chat=true` branch — unconditionally. If D4 adds a separate rerank branch to `resolve_api_key` but `chat_complete` never calls it, the branch is dead code.

The only internally consistent, no-signature-change design is to handle all three field fallbacks (model, base_url, api_key) entirely inside `rerank_config()` by mapping to `copy.chat_*` slots, so `chat_complete` receives the fully resolved copy. Under that design, when `rerank_api_key` is empty, `copy.chat_api_key = original.chat_api_key`, and `resolve_api_key(copy, true)` returns `chat_api_key` directly — bypassing env vars — which **contradicts D4's stated order** (`OPENAI_API_KEY` should have higher priority than `chat_api_key` in the rerank branch). The two deliverables describe incompatible semantics for the case where `rerank_api_key` is absent but `chat_api_key` and `OPENAI_API_KEY` are both set.

**Concrete fix**: Plan must explicitly choose one of:
- **Option A (pure-function only)**: `rerank_config()` maps all three fields (`copy.chat_api_key = rerank_api_key.empty() ? c.chat_api_key : c.rerank_api_key`); drop D4 as a separate `resolve_api_key` change; accept that env vars never override a configured `chat_api_key` in the rerank path (matching the existing chat/embed convention where config key beats env).
- **Option B (signature change)**: Rename `bool for_chat` to `enum class ApiKeyPurpose { embed, chat, rerank }`; update declaration in `brain.hpp`, all call sites in `chat.cpp` and `embed.cpp`; `chat_complete` must accept `ApiKeyPurpose` or a pre-resolved key; call `resolve_api_key(cfg, ApiKeyPurpose::rerank)` at the `request_llm_reorder` site before constructing the Config copy. Scope this work explicitly — it is not "三行接线."

---

### P1-2 · D5(e) False Premise — Chat Layer Never Exposes `base_url` in Errors

**Plan sections**: D5(e), AA1 (by implication)

**Problem**: D5(e) states "经现有 http 层错误信息暴露目标 host 的既有事实断言" as an available test seam. Verified against the actual code: `http_client.cpp` produces only generic WinHTTP strings ("WinHttpConnect failed", "HTTP request failed", "HTTP {status}: {body[:300]}") — the `base_url` is never concatenated into any error string. `chat.cpp` propagates `resp.error` verbatim. `redact_provider_error` (N33) exists only in `embed.cpp` and is never called from the chat path. The premise is false independently of N33: there is nothing to redact from `chat.cpp` errors because the URL was never included in the first place. An implementer who follows the D5(e) "inject a fake base_url and assert it appears in the error" approach will find zero signal and no assertion fires, silently producing a passing-but-vacuous test.

**Concrete fix**: Strike the "http 层错误信息暴露目标 host" sentence from D5(e) entirely. Declare the fallback **the sole approach**: D2 is a pure function — assert its output fields directly (AA1–AA4 already cover this). For structural proof that `request_llm_reorder` uses `rerank_config(cfg)`, add one line to the D5 audit note: "code-path walkthrough: the sole non-test branch at rerank.cpp:{line} reads `rerank_config(cfg)` — reviewer confirms at hard-audit." If a behavioral seam is desired, leverage the existing `RerankerOpts::llm_fn_for_test` — but note that this callback currently bypasses `request_llm_reorder` entirely (see lines 363–365 of rerank.cpp), so it does not exercise the config-reading path. Consider adding a `cfg_capture_for_test` field to `RerankerOpts` as a thin production-side hook if behavioral proof is required. This adds ~3 lines of production code and one new test assertion, and should be decided at plan time.

---

## P2 — Non-Blocking Issues

### P2-1 · `save_config_value` Key-Exclusion List

**Plan section**: D1 (omission)

`save_config_value` (brain.cpp ~308) has an explicit exclusion to prevent API key leakage to the file plane: `if (key != "embedding.api_key" && key != "chat.api_key")`. The plan adds `rerank_api_key` as a persisted field but never mentions adding `rerank.api_key` to this exclusion. Following existing convention, `rerank.api_key` must be excluded; omitting it would silently write the key to the JSON file, violating the audit note ("Never persist API keys to the file plane"). State this explicitly in D1.

### P2-2 · `Brain::load_config()` DB Flat-Key Overlay Not Addressed

**Plan section**: D1 (ambiguity)

D1 says "load_file_config（brain.cpp）解析 … 扁平键 rerank.model 等." The DB flat-key overlay lives in `Brain::load_config()` (lines 285–298), not in `load_file_config`. AA5 tests "嵌套段与扁平键两种写法解析结果一致" — but without `rerank.model` / `rerank.base_url` / `rerank.api_key` branches in the `while (st.step())` loop, values set via `save_config_value("rerank.model", ...)` are silently dropped on reload. The plan must explicitly call out the DB overlay loop update alongside the JSON file parser. If AA5's "flat key" refers specifically to the DB table keys, the test description should say so to avoid confusion with a hypothetical JSON-level flat key format.

---

## Audit Summary

The plan's architecture (pure-function `rerank_config()` + D3 call-site swap) is sound, minimal, and correctly addresses the user goal. The rollback posture, zero-modification regression proof (AA3), and scope-exclusion recording (AA7/Security Notes) are all adequate. Two blocking issues prevent green-light: **D4 as written is incompatible with `chat_complete`'s fixed interface**, producing either dead code or an undocumented signature-change cascade; and **D5(e) relies on a testable seam that does not exist in the chat layer** (the URL never enters the error string). Both are fixable by explicit resolution in the plan — Option A (pure-function key handling, drop the `resolve_api_key` extension description) is the lower-effort path. Once the plan text is amended on those two points, implementation scope remains genuinely small and single-commit reversible.


---

# Round 2 — VERDICT: FAIL (D5(e) text not updated)

**VERDICT: FAIL**

**P1 — Blocking Finding**

### P1-2 Resolution Incomplete — D5(e) Text Contradicts Disposition

**Problem**: The Round 1 audit disposition (line 12) states P1-2 was adopted: "D5(e) 'http 错误暴露 host' 接缝声明删除...行为证明改用**薄生产钩子**：`RerankerOpts` 增 `cfg_capture_for_test`...~3 行生产代码". However, D5(e) actual text (line 32) still contains the false premise verbatim: "验证 `request_llm_reorder` 产生的请求目标 URL 来自 rerank 段（**经现有 http 层错误信息暴露目标 host 的既有事实断言**，不新增生产代码）". The disposition claims the premise was deleted; D5(e) still proposes using it as a mock-assertion approach. More critically, **D5(e) never mentions `cfg_capture_for_test`** — the thin production hook described in the disposition as the adopted solution. An implementer reading D5(e) would attempt the false-premise approach first, discover it doesn't work (because chat errors never contain base_url), then fall back to "D2 纯函数断言 + 代码路径走查" — never encountering the `cfg_capture_for_test` hook that the disposition says was adopted.

**Fix**: Rewrite D5(e) to match the disposition. Strike the parenthetical "（经现有 http 层错误信息暴露目标 host 的既有事实断言，不新增生产代码）" entirely. Replace the "mock 型断言" paragraph with: "行为证明：`RerankerOpts` 增 `cfg_capture_for_test` 字段（const Config* 指针，默认 nullptr）；`request_llm_reorder` 在调用 `chat_complete` 前若该指针非空则将实际使用的 Config 副本拷入；测试通过该钩子断言 rerank 路径消费的 model/base_url/key 字段来源正确（~3 行生产代码，零网络依赖）。"

---

**Summary**: P1-1 (resolve_api_key) correctly resolved via Option A pure-function approach with documented config-beats-env semantics; P2-1 (save_config_value exclusion) and P2-2 (DB overlay loop branches) adequately addressed through AA5 binding assertions. P1-2 blocks approval because the deliverable text still proposes the false error-seam approach and omits the adopted `cfg_capture_for_test` hook, creating implementation misdirection despite the disposition claiming resolution. No new blocking issues; once D5(e) text aligns with its disposition, the plan will be implementable as single-commit reversible scope.

---

# Round 3 — VERDICT: PASS

**Reading complete. Performing Round 3 audit.**

---

## Round 3 Checklist

**Primary gate: D5(e) rewrite**

Plan line 32— D5(e) current text:

>行为证明：`RerankerOpts` 增 `cfg_capture_for_test` 字段（const Config* 指针，默认 nullptr）；`request_llm_reorder` 在调用 `chat_complete` 前若该指针非空则将实际使用的 Config 副本拷入；测试通过该钩子断言 rerank 路径消费的 model/base_url/key 字段来源正确（~3 行生产代码，零网络依赖）。

✅ **Stale sentence gone.** The "http 层错误信息暴露目标 host / 经现有 http 层错误信息暴露目标 host 的既有事实断言" language is absent from D5(e) and from every other deliverable section. The only occurrence of "http 错误暴露 host" in the document is in the **disposition block** (line 12), correctly used as a reference to what was *deleted* ("接缝声明删除") — that is appropriate audit-trail metadata, not a live requirement.

✅ **D5(e) ↔ Disposition match.** Disposition P1-2 (line 12) specifies: `cfg_capture_for_test`（const Config* 指针，默认 nullptr；非空时 request_llm_reorder 在调用 chat_complete 前把实际使用的 Config 副本拷给它，~3 行生产代码）. D5(e) body mirrors this exactly. No gap.

✅ **AA1 cross-reference is consistent.** Line 43: "rerank 调用目标 base_url/model 为rerank 段值（**按 D5(e) 的既定证明方式**）" — correctly points at the hook, not the defunct error-seam approach.

---

**Round 1 P1/P2 adoptions — all verified present:**

| Finding | Adoption location | Status |
|---|---|---|
| P1-1 Option A (pure-function, drop resolve_api_key change) | D4撤销 (line 31) + D2 rewrite (line 29) + AA4 (line 46) | ✅ |
| P1-2 cfg_capture_for_test | D5(e) (line 32) + disposition (line 12) + AA1 (line 43) | ✅ |
| P2-1 save_config_value exclusion for rerank.api_key | Disposition (line 13) + AA5 (line 47) | ✅ (P2-residual: D1 body text still doesn't spell it out verbatim, but AA5 assertion is explicit and falsifiable — non-blocking) |
| P2-2 Brain::load_config() DB overlay loop | Disposition (line 14) + AA5 line 47 "DB 配置表扁平键…经 Brain::load_config overlay 循环" | ✅ |

---

**Falsifiability scan — AA1–AA7:**

All seven assertions are falsifiable with concrete expected values or executable predicates (field equality, matrix coverage, diff absence). No assertion reduces to a tautology. AA6 defers exact count to executable output ("精确值来自可执行输出") — correct practice.

---

**New blocking issues scan:** None found. The pointer-type notation (`const Config*` where an actual copy-in would need `Config*`) is a minor description imprecision but the intent (capture hook for test-time introspection of the merged config) is unambiguous and consistent across disposition, D5(e), and AA1. Implementer has enough information to resolve it; not a plan-level blocking issue.

---

VERDICT: **PASS**

D5(e) has been correctly rewritten: all stale false-premise text is gone, the cfg_capture_for_test specification matches the P1-2 disposition exactly, and AA1 references it correctly. All Round 1 P1 and P2 findings are adopted and traceable in the plan. No new P0 or P1 issues. One minor P2 residual (D1 body text doesn't explicitly name the `save_config_value` exclusion, covered by disposition + AA5) — non-blocking. The plan is cleared for implementation.

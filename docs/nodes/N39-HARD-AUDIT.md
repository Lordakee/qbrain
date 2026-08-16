# N39 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), task qbrain-n39-hard-audit-claude-20260816, 2026-08-16
**Audit basis**: docs/nodes/N39-PLAN.md (approved round-3 PASS)
**Human authorization**: user instruction 2026-08-16 (verbatim in dispatch log)

---

Good - no UA/header spoofing anywhere in touched files. All AA7 checks complete. I now have all the evidence needed to issue the verdict.

---

**VERDICT: PASS**

**P0 findings (blocks done): none**

**P1 findings: none**

**P2 findings: none**

---

**AA-by-AA verdict:**

**AA1** — `Config` in `types.hpp:96-98` adds `rerank_model`, `rerank_base_url`, `rerank_api_key` (all `std::string`, default empty). Exact match to D1 spec. ✓

**AA2** — `rerank_config(const Config& c)` declared in `brain.hpp:267`, defined in `brain.cpp:271-283`. Pure function: copies `c`, overlays non-empty rerank fields into `chat_*` slots of the copy, preserves original `rerank_*` slots on the copy. All three fallback directions (model/base_url/api_key) handled independently. Exact match to D2 / Option A disposition. ✓

**AA3 (zero-modification of pre-existing 40 tests)** — `test_main.cpp` gained exactly two lines: the forward declaration `void test_n39_rerank_config();` (line 70) and the entry `{"n39_rerank_config", test_n39_rerank_config}` (line 118). This follows the same pattern as every prior node (N38 added lines 69 + 117, etc.). All 40 pre-existing test registrations are unmodified. The CMake evidence at lines 2744-2746 shows `[PASS] n38_pg_backend` followed by `[PASS] n39_rerank_config` as the 40th and 41st entries respectively — zero regression. Per the plan's explicit precedent ruling, this constitutes zero modification. ✓

**AA4** — `rerank_config` in `brain.cpp:271-283` implements Option A key semantics: `rerank_api_key` non-empty → `copy.chat_api_key = c.rerank_api_key`; empty → `copy.chat_api_key` stays at `c.chat_api_key` (which already beats env since configured chat key is never overridden by env in `resolve_api_key`). `resolve_api_key` is entirely unmodified (`brain.cpp:285-296`). ✓

**AA5** — `load_file_config` in `brain.cpp:239-244` parses nested `"rerank"` section (model/base_url/api_key). `Brain::load_config` at lines 314-316 adds flat-key branches `rerank.model`, `rerank.base_url`, `rerank.api_key` to the DB overlay loop. Both parse paths verified. ✓

**AA5 (save_config_value exclusion)** — `brain.cpp:331-334` adds `rerank.api_key` to the exclusion guard alongside `embedding.api_key` and `chat.api_key`, preventing the key from being mirrored to the file plane. Exact match to D1 / P2-1 disposition. ✓

**AA5 (D3 rerank wiring)** — `rerank.cpp:354-355`: `const Config effective = rerank_config(cfg);` then `if (opts.cfg_capture_for_test) *opts.cfg_capture_for_test = effective;`. The production LLM call at line 376 uses `effective` not the raw `cfg`. ✓

**AA6 (test suite 41/41 two-path)** — FINAL-VERIFY-CMAKE.txt line 2746: `[PASS] n39_rerank_config` (CMake path, round 1 — 41 registered, 0 fail; `cmake_build_exit=0` at line 204). FINAL-VERIFY-SCRIPT.txt line 105 shows `TESTS_BUILD_OK` and the script path executes the same binary with `[PASS] n39_rerank_config` visible. Both paths clean. Evidence matches "41x2 PASS 0 FAIL" and "41x3 PASS 0 FAIL" claims in the task brief. ✓

**AA7 (no UA/header spoofing changes)** — Verified by grepping rerank.cpp, chat.cpp, and all touched files for User-Agent, impersonation, spoofing, and custom HTTP header patterns. Zero matches. The plan's Security notes exclusion is honored. ✓

**test_n39.cpp coverage** — The file (190 lines) covers: all 8 partial fallback combinations (2³-1 partial + empty + full), key-beats-env proof via `resolve_api_key`, nested JSON file parse via scoped `LOCALAPPDATA` + real `Brain`, DB flat-key round-trip via `save_config_value`, `rerank.api_key` file-plane exclusion negative, and behavioral capture (with and without rerank section). All plan D5(a)–(e) coverage points satisfied. ✓

**Conclusion:** N39 is a clean, minimal increment. All seven acceptance assertions (AA1–AA7) are satisfied by direct inspection of the implementation. The three fields are correctly added to `Config`, `rerank_config()` is a pure function that precisely implements Option A fallback semantics, `save_config_value` excludes `rerank.api_key` from the file plane, the DB overlay covers all three flat keys, `apply_reranker` consumes the effective config and fires the `cfg_capture_for_test` hook before any LLM branch, `test_n39.cpp` provides the full planned matrix proof, and 41/41 PASS across both build paths with zero regression on the prior 40 tests. No P0/P1/P2 findings identified.

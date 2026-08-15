# N30 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n30-hard-audit-claude-20260815, 2026-08-15
**Audit basis**: docs/nodes/N30-PLAN.md (approved; plan audit PASS with adopted dispositions)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in the dispatch log)

---

## N30 HARD AUDIT — Baseline Reconciliation & Security/Build/Storage Closure

**VERDICT: PASS**

N30 successfully delivers the baseline reconciliation and security closure required by Phase 2 governance. All 11 acceptance assertions are met with verifiable evidence, the implementation faithfully executes the approved plan (including all adopted P1 dispositions), and both build paths produce green test suites.

---

## Security Review (D3/D4/D6 — Authorization, Disclosure, CLI Safety)

**Central authorization enforcement (registry.cpp:64-93)**: The implementation correctly enforces scope at the single choke point. Lines 79-82 implement the exact algorithm specified in the adopted P1-1 disposition: `(scope == Write || Admin) && ctx.remote && !capability_permits()` denies remote mutations without an authenticated capability. The `--allow-write` flag is correctly relegated to local-only convenience (lines 85-88) and never authorizes remote Write/Admin. The audited N1 MCP default-deny is preserved (lines 85-88).

**Negative matrix coverage (test_n30.cpp:529-574)**: 36 mutating operations enumerated programmatically from the registry (not hardcoded), all denied remotely with and without `--allow-write` set. The five P0-bypass operations identified by resolution review (takes_calibration, file_upload, submit_agent, put_raw_data, schema_apply_mutations) are registered as Write/Admin scope and centrally denied. Evidence: FINAL-VERIFY-SCRIPT.txt lines 139-141 (n30_b_auth_redaction PASS), FINAL-VERIFY-CMAKE.txt lines 241-243.

**Path disclosure redaction (handlers.cpp, test_n30.cpp:615-686)**: `get_health`, `file_list`, and `file_url` all redact local absolute paths, db paths, and `file:///` URLs from remote responses while preserving full info for local callers. The predicate is `ctx.remote || ctx.via_mcp` per the controller's merge decision (any non-CLI caller gets redacted output). Test coverage includes drive-letter scanning and %LOCALAPPDATA% pattern detection.

**CLI port parsing (commands.cpp:59-69, test_n30.cpp:691-701)**: `parse_port_value` uses `std::from_chars` with exact decimal validation, range-checks 1-65535, and rejects malformed input (leading/trailing whitespace, signs, non-digits, overflow). 16 negative test cases confirm controlled errors without crashes.

---

## HTTP Routing & Build (D5/D7 — Exact Routing, Dual-Path Alignment)

**Exact request-line parsing (http_server.cpp:141-178, mcp/server.hpp:180-188)**: `parse_http_request_line` validates method tokens (RFC 7230 tchar), enforces exact HTTP-version format (8 bytes, `HTTP/\d.\d`), and rejects malformed input. `route_http_request` uses exact path matching (`request.path == "/ingest"`) rather than substring contains. Test coverage (test_n30.cpp:182-242): `/ingestx` returns 404 NotFound (not routed as `/ingest`), malformed method tokens/version strings/targets all fail parse, and case-sensitive method matching rejects `get` for GET paths.

**Loopback e2e negatives (test_n30.cpp:246-332)**: Real socket path confirms `/ingestx` → 404, duplicate/invalid Content-Length → 400, oversized body → 400, unsupported method → 405. Sanity checks confirm `/health` and `/` (JSON-RPC) remain reachable.

**Dual-build alignment (CMakeLists.txt:188, scripts/build-cl.ps1, FINAL-VERIFY-SCRIPT.txt, FINAL-VERIFY-CMAKE.txt)**: CMakeLists.txt includes `tests/test_n30.cpp` at line 188. Both paths produce working binaries: script-path `rm -rf build/cl; build-cl.ps1; build-tests-cl.ps1` exits 0 (lines 1-89), CMake-path `rm -rf build/cmake-final; cmake + msbuild` exits 0 (CMAKE lines 1-172). Test suites: 31 registered tests (up from 29 baseline), two rounds green in both paths. No stale 18/18 counts remain in docs (generated values only).

---

## Storage Integrity (D8/D9 — Schema Completeness, Atomic Pack Writes)

**Schema v12 completeness (migrate.cpp:150+, test_n30.cpp:336-394)**: `check_schema_integrity` now covers 16 tables (including page_versions, facts, takes, file_index, raw_data per v5/v8/v9/v10/v11 migrations), 18 indexes (including idx_facts_entity, idx_raw_data_key, idx_takes_body), and 6 column sets (pages.source_kind, file_index.path). 11 corruption fixtures each remove one required object; doctor reports FAIL with the missing object name in `integrity.missing[]`. Evidence: test lines 350-393, PASS in both build paths.

**Atomic pack mutations (packs.cpp, test_n30.cpp:396-455)**: `apply_mutations` writes to a bounded temp file in the same directory, flushes, closes, then atomically replaces via `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`. Read-only injection test (lines 423-435): target file made read-only, mutation fails, sha256 unchanged byte-for-byte, no temp siblings survive, retry after permission restore succeeds. Evidence: PASS in both build paths.

---

## Governance & Evidence (D1/D2/D10/D12)

**Changeset manifest (CHANGESET-MANIFEST.json)**: 270 files (93 M + 177 ??); secrets scan 99 raw hits all assessed non-secret (per-file exemptions embedded); 4 disposition categories (41 product, 40 test, 188 docs, 1 stale). Zero rejected files. Acceptance 1: MET.

**Node reconciliation matrix (NODE-RECONCILIATION-MATRIX.json)**: 30 rows (N1-N29 + supplementary N2.5); N20/N21/N23 each have controller disposition records (N20/N23 corrective-closure with fresh audits dispatched in outcome phase; N21 supersede). N24-N28 AMD-7 deferrals recorded with reason/rows/owner. Tier annotations present. Acceptance 2: MET.

**Doc sync (docs/09, docs/03, OPS-PARITY-LEDGER.md)**: Test counts are generated values (31/31), no stale prose counts. Acceptance 10: MET.

**Evidence**: PRE-GATE.json captures 18 scoped-file hashes, deliverables-absence proof, dual-build identity, and isolated data root. FINAL-VERIFY-SCRIPT.txt and FINAL-VERIFY-CMAKE.txt record two-round green suites (93 PASS / 0 FAIL script-path round 1 lines 90-151, 62 PASS / 0 FAIL CMake-path round 1 lines 173-243). ACCEPTANCE-CROSSCHECK.md maps all 11 assertions to concrete evidence with MET verdicts.

---

## Acceptance Cross-Check (All 11 Assertions)

1. ✓ Changeset manifest covers 270 files, sha256+classification, 0 genuine secrets
2. ✓ Matrix covers N1-N29; N20/N21/N23 dispositions present; N24-N28 deferrals with owner/rows
3. ✓ 36 mutating ops denied remotely without capability; negative matrix programmatic enumeration PASS
4. ✓ `/ingestx` → 404; malformed headers/lengths → controlled 400; routing exact-match parser verified
5. ✓ `--port=abc` / `--port=99999` → controlled error exit 2, no crash; 16 negative cases PASS
6. ✓ Remote `get_health`/`file_list`/`file_url` expose no db paths, drive letters, or `file:///` patterns
7. ✓ 11 schema corruption fixtures each FAIL doctor with missing-object name in output
8. ✓ Pack write failure leaves target sha256 unchanged; no temp leftovers; retry succeeds
9. ✓ Both build paths clean-dir → working binaries; 31 registered, two rounds green
10. ✓ Test counts in docs are generated (31/31); no 18/18 stale prose
11. ✓ Commit plan separates docs vs source (slice 1 source/build+tests, slice 2 docs/governance)

---

## Findings

**P0**: None. All blocking defects closed.

**P1**: None. All P1 audit recommendations were adopted and implemented.

**P2**: None. The implementation is production-ready against the approved plan.

---

## Rollback & Regression Verification

N30 introduces no breaking schema changes (v12 completeness check is read-only detection). N1 MCP write default-deny is preserved (test_n30.cpp:85-88 enforces stdio MCP without `--allow-write` still denies Write/Admin). N2.5 `sources_add` remains MCP-denied (local_only flag set). Provenance stamping (N1/N15 `source_kind`/`ingested_via`) intact in migrate.cpp v4/v8. The test suite's n20 previously-failing expected-deny check (test_n20.cpp:570) now passes via central enforcement, confirming the P0 authorization bypass is closed.

---

## Conclusion

N30 establishes the唯一可信基线 (sole trusted baseline) required by Phase 2. The 270-file changeset is fully classified and secrets-clean; N1-N29 governance contradictions are reconciled with explicit dispositions; every P0 security defect (authorization bypass, path disclosure, HTTP substring routing) and P1 correctness defect (CLI crashes, CMake gaps, incomplete schema checks, non-atomic pack writes) identified by three-party review is closed with test coverage. Both build paths are reproducibly green (31/31 tests, two rounds, zero failures). The implementation is a faithful execution of the approved plan with all adopted audit dispositions realized in code. N30 PASS gates all Phase 2 successor nodes (N31-N37).

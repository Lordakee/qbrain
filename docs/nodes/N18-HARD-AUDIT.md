# N18 Hard Audit - Source-scoped Graph Heuristics

**VERDICT: PASS**

**Auditor: Claude Code**

**Date: 2026-08-04**

## Summary

N18 delivers three read-only, source-scoped graph analytics operations (find_anomalies, find_contradictions, find_experts) with complete functional correctness, comprehensive test coverage, and rigorous read-only verification. Native Windows x64 MSVC C++20 compilation succeeds with 25/25 tests passing. Runtime evidence demonstrates exact contract compliance across all 16 acceptance assertions through 70 full-snapshot calls with matching before/after hashes for both selected and decoy databases.

The implementation correctly enforces source canonicalization without creation, N2.5 remote authorization with explicit allowlist requirements, complete limit validation with 0-200 clamping, deterministic content-based ordering independent of SQL/insertion/iteration order, bounded UTF-8-safe detail rendering, selected-brain isolation with no cross-database leakage, and zero-mutation read-only behavior verified across successful, empty, malformed, unauthorized, and limit-clamped operations.

All three operations remain Scope::Read with local_only=false, expose exact schemas with additionalProperties:false, correctly default omitted source_id to canonical default, validate and clamp numeric limits before analytics SQL, and return structured errors that do not echo untrusted input. MCP runtime validation in server.cpp validate_analytics_arguments rejects non-object arguments, wrong types, and unknown fields before dispatch. handlers.cpp uses validate_analytics_args for local unknown-field rejection, parse_bounded_uint for strict limit parsing, and resolve_source for canonicalization/authorization/existence. The operations ignore ambient QBRAIN_SOURCE and use only explicit source_id or its default.

Plan status remains approved. Changing it to done and reconciling exactly the three N18 ledger rows are post-audit parent process steps not performed by this audit.

## Baseline Evidence Hashes

| Artifact | SHA-256 |
|---|---|
| Approved N18-PLAN.md | 341477f6316cf3e5785546c4ca069a1f8580734323df56d63dc46c082473c764 |
| N18-PLAN-AUDIT.md (PASS 2026-07-30) | 87db9821c255555ab6a42aab8d22cac945a5e0141aeeb3dd02e76a07e743af6d |
| docs/nodes/n18-evidence/VERIFY-REPORT.md | 67662532c8eb77fc242d325d619419ceba2b3bfc6ca39bd46ada9883cff1387b |
| docs/nodes/n18-evidence/BUILD-MANIFEST.txt | 2b6bee15b0c279c3a85e81b99eebf2811cc59a90627ae1ec331e08ce6e4a4d7d |
| docs/nodes/n18-evidence/TEST-OUTPUT.txt | 6657c2999f2414d86c3435f27bb3ac02d93dad0071042b8baedf1988d23a847b |
| docs/nodes/n18-evidence/CLI-SMOKE-OUTPUT.txt | d519cdba010891f4759bdc999b028e4cc86cd37dfbb92c57e64781a0649de358 |
| build/wave3-final-production.log | ea907ecda1afb9775d3c0ae3483149281e17b2c5056c74ef03ba02b87f372e4c |
| build/wave3-final-tests.log | 634090ed6204f1f5e7b7aa4ad487a0470d52878b8b7008cd842c8f5757853b5b |
| build/wave3-final-build-manifest.txt | d3fc2bb3ae1e81c2df340ab89d4b9ed76acbaf477947da6676b3cecd6a4c30e3 |
| build/cl/qbrain.exe | 1da1aaca5d805b697d4e3d36a849456245b4ee3ffee5637bb0c87e2161e3b18f |
| build/cl/qbrain_tests.exe | ec1f6bb79734de69578717828d74d62bde99503b472df2a4ea18db035dd1b359 |

## Acceptance Table

| # | Acceptance Assertion | Status | Evidence |
|---|---|---|---|
| A1 | N18 implementation starts only after Claude Code plan-audit PASS and plan marked approved | PASS | N18-PLAN-AUDIT.md dated 2026-07-30 with VERDICT: PASS exists; N18-PLAN.md line 3 status is approved |
| A2 | Each operation defaults omitted source to default, verifies source existence without creating it, fails invalid/unknown/unauthorized sources with structured error before analytics SQL | PASS | test_n18.cpp:329-359 exercises empty string (invalid_source), bad/slash (invalid_source), CON/NUL/PRN (invalid_source per Win32 reserved), 65-byte overlength (invalid_source), valid_but_unknown (source_not_found). Lines 338-357 verify source count unchanged and analytics_reads=0 for all rejections. Lines 362-365 verify remote default succeeds without allowlist. handlers.cpp uses validate_analytics_args for unknown-field rejection, parse_bounded_uint for strict limit parsing, and resolve_source for canonicalization/authorization/existence. server.cpp uses validate_analytics_arguments for JSON object/type/unknown-field validation before dispatch. |
| A3 | Local registered-source reads work without allowlist; remote non-default reads work only when N2.5-authorized; allow_write=true never bypasses restriction | PASS | test_n18.cpp:367-384 exercises remote team_a denial with empty allowlist for both allow_write=false and allow_write=true (lines 367-373), then allowlists Team_A case-insensitively and verifies TEAM_A succeeds while team_b remains denied even with allow_write=true (lines 375-384). Snapshots unchanged across all paths. |
| A4 | Supplied limits consume full unsigned decimal string, reject malformed/overflow, clamp to 0..200, preserve per-operation defaults, never permit more than 200 rows | PASS | test_n18.cpp:276-287 exercises limit=1/200/201/999/18446744073709551615 with exact expected_size assertions; 200 remains 200, valid above-operation-maximum values clamp to 200. Lines 289-301 exercise limit=0 with analytics_reads=0 check and empty result. Lines 303-320 exercise empty string, +1, -1, space-prefixed, space-suffixed, 1.0, 1junk, and 18446744073709551616 (parse overflow) with invalid_argument error on field limit, analytics_reads=0, and unchanged snapshots. Lines 270-274 verify omitted defaults (100/100/50 per operation). |
| A5 | find_anomalies evaluates only selected-source links from live same-source origins; missing/deleted target checks cannot match another source; duplicate slug-pair links produce one broken-target result | PASS | test_n18.cpp:622-654 seeds cross-source targets (team-live-target line 625, team-deleted-target line 624) and asserts both reported as missing (lines 673-678) because default-source link cannot be satisfied by team_a page. Lines 626-627 seed duplicate link types wiki/reference to same target; lines 681-682 assert exactly one broken-target result. Lines 629-630 seed missing/deleted origins; lines 691-692 assert excluded from results. |
| A6 | high_out_degree emitted only above 20 selected-source stored outgoing rows grouped by source and live origin; other-source rows cannot push origin over threshold; broken selected-source rows retain row-count semantics | PASS | test_n18.cpp:632-643 seeds exactly 20 and 21 outgoing rows per origin in default source, and 50 rows in team_a for slug degree20. Lines 666-686 assert degree21 present, degree20 absent from default results. Lines 696-701 assert team_a degree20 (50 rows) triggers high_out_degree, proving cross-source isolation: default 20 + team_a 50 does not push default origin over threshold. Broken targets (lines 622-628) count as stored rows. |
| A7 | find_contradictions uses only active facts inner-joined through active selected-source page ownership; null-page, dangling, deleted-owner, inactive, other-source facts cannot participate or be mutated | PASS | test_n18.cpp:1184-1186 seed entity/null with page_id IS NULL; line 1244 asserts excluded after result parse. Lines 521-527 assert dangling/non-owner ids (page_id=0 rules/null-owner and page_id=999999 rules/dangling-owner) excluded from default-source result. Lines 480-481 seed inactive facts; line 522 asserts excluded. Lines 479-481, 503 seed deleted-owner facts; line 522 asserts excluded. Lines 482-485 seed team_a-owned and cross-source facts; line 522 asserts rules/team-owner and rules/cross-source excluded from default-source result; lines 574-577 assert rules/cross-source plus dangling/non-owner rows excluded from team-source result. Snapshots prove no mutation. |
| A8 | Contradiction results match only exact same-predicate or documented opposing-predicate/prefix rules, canonicalized/deduplicated independent of encounter order, no truth/temporal/semantic/LLM/full-gbrain claim | PASS | test_n18.cpp:416-446 seeds all 14 enumerated opposing pairs (is/is_not, is/isnt, is/isn't, supports/opposes, likes/dislikes, has/lacks, titled/not_titled, titled/untitled, true/false, yes/no, works_at/left, employed_by/former_employee_of, located_in/not_located_in, member_of/not_member_of) and 3 negating prefixes (not_, no_, anti_); lines 516-518 assert all positive_entities appear exactly once. Lines 463-466 seed double-prefix not_not_x vs not_x; line 522 asserts excluded (prefix itself is not a base predicate). Lines 586-592 exercise forward vs reverse insertion order and assert byte-identical JSON/text. |
| A9 | find_experts counts only stored links between live pages in selected source, ranks by inbound_count DESC then slug ASC, excludes zero-inbound/deleted/missing targets, no expertise inference from facts/content | PASS | test_n18.cpp:740-770 seeds live origins/targets, missing/deleted origins/targets, and multiple link types per endpoint pair. Lines 783-789 assert exact ranking: leader (3 inbound), A-tie (2), a-tie (2) with bytewise slug tiebreak A < a. Lines 791-795 assert zero-inbound and missing/deleted excluded. Lines 803-811 exercise team_a and assert cross-source isolation. No facts or body content used in ranking. |
| A10 | Every success row includes canonical source and documented fields; details valid UTF-8 and <=512 bytes; outputs contain no body/link context/complete fact record/secret/token/provider response/config dump | PASS | test_n18.cpp:107-114 require_bounded_utf8_details checks every row <=512 bytes and valid UTF-8 via nlohmann serialization. Lines 1187-1196 seed entity/nul-object with embedded NUL byte and entity/utf8 with malformed UTF-8. Lines 1235-1248 parse operation JSON and assert entity/nul-object present after parse (line 1239), proving embedded NUL/control data remains valid JSON escaping; require_bounded_utf8_details at 1248 serializes every detail. Lines 1249-1258 verify entity/utf8 (distinct malformed-UTF-8 fixture) has U+FFFD replacement, explicit truncation marker, and <=512 bytes. Lines 919-937 seed body/context/config/log secrets; lines 967-968, 1029, 1109-1111 assert forbidden sentinels absent from all results. Canonical source checked at lines 536, 690, 790, 808, 1223, 1241-1246. |
| A11 | Final ordering for all operations explicitly content-sorted, byte-identical on unchanged data; SQL row order, insertion encounter order, unordered_map, unordered_set iteration cannot determine returned order | PASS | test_n18.cpp:586-592 contradiction_rule_output exercises forward vs reverse insertion order (lines 491, 644, 761 std::reverse) and asserts byte-identical JSON/text (lines 590-591). Lines 661-664, 779-781 exercise anomaly/expert forward vs reverse and assert byte-identical (lines 712-713, 822-823). Lines 510-513, 1230-1231 capture repeated calls and assert identical JSON. analytics.cpp implements explicit content ordering: anomalies at lines 312-314, contradictions at lines 447-450, experts at lines 490-491, using COLLATE BINARY where required. |
| A12 | Fixtures with overlapping slugs/entities and stronger decoy results prove source and selected-brain isolation; no unauthorized sentinel appears in JSON or text | PASS | test_n18.cpp:850-881 seed_isolation_brain creates selected and decoy databases with SELECTED_ANOMALY_SENTINEL / DECOY_ANOMALY_SENTINEL, SELECTED_CONTRADICTION_SENTINEL / DECOY_CONTRADICTION_SENTINEL, and opposing expert ranks (3 vs 1, 1 vs 4). Lines 1113-1124 assert selected expert-a (3 inbound) ranks first in selected brain, expert-b (4 inbound) ranks first in decoy brain, proving cross-database isolation. Lines 924-937, 967-968, 1029, 1109-1111 assert forbidden sentinels absent. |
| A13 | Full logical snapshots of selected and decoy databases unchanged across authorized, empty, malformed, unknown, denied, clamped calls; no N18 operation writes SQLite/files/jobs/config/logs/network state | PASS | test_n18.cpp:164-184 call_without_mutation captures before/after snapshots of both selected and decoy databases for every call, asserts equality, and records hashes in g_snapshot_call_evidence. Lines 1449-1466 emit 70 snapshot_call entries numbered 1-70 with selected_before/after and decoy_before/after hashes. Wave3-final-tests.log lines 146-215 record all 70 snapshot calls; every selected_before equals selected_after, every decoy_before equals decoy_after. |
| A14 | Registry and real MCP evidence prove all three operations remain Read, expose exact schemas, enforce source authorization before detail exposure, preserve structured failures, work with MCP writes disabled | PASS | test_n18.cpp:1270-1293 inspect registry metadata and assert Scope::Read, !local_only, additionalProperties:false, exact field types/defaults (100/100/50), minimum 0, maximum 200. Lines 941-958 assert MCP tools/list contains all three operations with matching inputSchema. Lines 960-1019 exercise MCP tools/call with success/empty/clamped/malformed/denied paths and assert structured errors preserved. Lines 1327-1335 exercise real MCP handle_rpc_body serialization. |
| A15 | Native Windows x64 MSVC evidence records /std:c++20, compiler version, exact commands/exit codes, all-PASS full suite >=21 tests including test_n18, runtime markers, deliverable hashes, snapshot hashes before outcome audit | PASS | build/wave3-final-tests.log line 4 records /std:c++20. Line 8 records MSVC 19.51.36248 x64. Lines 1-2, 219-220 record exact commands and exit_code=0. Lines 217-218 record 25 PASS / 0 FAIL. Line 216 [PASS] n18. Lines 145-215 emit N18 runtime markers including snapshot_call_count=70, read_only=pass. build/wave3-final-build-manifest.txt records 110 FILE entries + 2 ARTIFACT entries with exact hashes. VERIFY-REPORT.md consolidates evidence with all required sections. |
| A16 | Only after complete Claude Code outcome-audit PASS may plan become done and only three N18 ledger rows/notes be reconciled; no schema migration, out-of-scope artifact, LLM/model config change, commit, push part of N18 | PASS | VERIFY-REPORT.md line 121 confirms no model/provider/base URL/API key/reasoning/context/compression configuration changed. N18-PLAN.md line 3 status remains approved, not done. Ledger reconciliation has not occurred. This audit document provides the required PASS verdict; changing plan status and ledger reconciliation are post-audit parent steps. No schema migration occurred. build/wave3-final-build-manifest.txt contains no artifact from any later node. |

## Deliverables Review

| # | Deliverable | Status | Evidence |
|---|---|---|---|
| 1 | include/qbrain/graph/analytics.hpp and src/qbrain/graph/analytics.cpp with source-aware signatures, active-graph and page-owned-fact scoping, exact heuristic set, deduplication, bounded details, explicit deterministic ordering | PASS | analytics.hpp lines 29-36 declare source_id parameters and default limits 100/100/50. analytics.cpp implements bounded_detail (lines 105-132), predicate conflict rules (lines 135-163), and explicit SQL ORDER BY clauses for anomalies at 312-314, contradictions at 447-450, and experts at 490-491, using COLLATE BINARY where required. Test evidence demonstrates correct scoping, deduplication, and ordering. |
| 2 | Source-resolution/authorization seam in core/operation helpers: canonicalize and verify registered source without mutation, enforce N2.5 remote allowlist before analytics execution | PASS | Functional behavior proven by test_n18.cpp source validation matrix (lines 329-359) and remote authorization matrix (lines 361-384). handlers.cpp uses validate_analytics_args for unknown-field rejection, parse_bounded_uint for strict limit parsing, and resolve_source for canonicalization/authorization/existence. server.cpp uses validate_analytics_arguments for JSON object/type/unknown-field validation before dispatch. No source creation occurs during analytics operations (source count assertions at lines 338-357). |
| 3 | src/qbrain/ops/handlers.cpp: all three handlers Scope::Read, non-local-only, expose source_id+limit in JSON schemas, strict full-string limit parsing, structured failures, canonical source in successful rows | PASS | test_n18.cpp:1270-1293 inspect registry and assert Scope::Read, !local_only, exact schemas with additionalProperties:false. Lines 303-320 exercise malformed limit strings and assert invalid_argument errors. Lines 329-359 exercise invalid sources and assert structured errors. All success rows include canonical source_id. |
| 4 | Dedicated tests/test_n18.cpp registered in tests/test_main.cpp, CMakeLists.txt, scripts/build-tests-cl.ps1; historical tests/test_analytics.cpp remains regression input | PASS | build/wave3-final-build-manifest.txt line 99 lists test_n18.cpp with hash 4781d0029a8fd7d9a0cb298d4ac935bb36fc23e2612e51f562c5d7e33a87f9ea. Line 90 lists test_main.cpp. Line 4 lists CMakeLists.txt. Line 40 lists build-tests-cl.ps1. Line 84 lists test_analytics.cpp. wave3-final-tests.log line 38 compiles test_n18.cpp, line 216 [PASS] n18. |
| 5 | scripts/n18-verify.ps1 and docs/nodes/n18-evidence/ containing VERIFY-REPORT.md, native build/test output, focused runtime output, registry/MCP smoke output, full snapshot hashes, manifest/hash list | PASS | build/wave3-final-build-manifest.txt line 44 lists n18-verify.ps1 with hash 2f6a5c64c04115e04032c1e1cd8d98f74ff969aa0c2eb86ca8788d831c9a8892. VERIFY-REPORT.md exists with all required sections: commands, N1-N13 preconditions, runtime markers (70 snapshot calls), deliverable hashes, result confirmation. Evidence directory contains required files with documented hashes. |
| 6 | Node-specific N18-PLAN-AUDIT.md before implementation and N18-HARD-AUDIT.md after implementation/evidence; only Claude Code may issue PASS verdicts | PASS | N18-PLAN-AUDIT.md exists with VERDICT: PASS, Auditor: Claude Code, Date: 2026-07-30. This document is N18-HARD-AUDIT.md with VERDICT: PASS, Auditor: Claude Code, Date: 2026-08-04. Both audits performed by Claude Code as required. |
| 7 | No schema migration planned; if discovered required, stop, return plan to draft, document populated-database migration/idempotence/rollback tests, obtain new Claude Code plan-audit PASS | PASS | No schema migration occurred. VERIFY-REPORT.md confirms no model/config changes. Implementation proceeded without schema modification. This deliverable requirement is met by the absence of migration and the presence of evidence confirming no change. |
| 8 | After complete outcome-audit PASS, set plan to done, reconcile only three N18 ledger rows/notes to exact source-scoped heuristic subset and evidence links | NOT YET | This deliverable is a post-audit parent process step. Plan status currently remains approved. Ledger reconciliation must occur after this audit is accepted, not as part of the audit itself. The audit provides the required PASS verdict enabling the parent process to proceed. |

## Security and Authorization Analysis

N18 correctly implements defense-in-depth read authorization:

1. Source validation occurs before analytics SQL execution (test_n18.cpp:338-357 prove analytics_reads=0 for all invalid/unknown sources).
2. Remote non-default sources require case-insensitive membership in mcp.allowed_sources configuration (lines 367-384).
3. allow_write=true does not bypass source authorization (lines 367-373, 1014-1019, 1064-1073).
4. Unknown sources fail with source_not_found without creating the source (lines 338-357 assert source count unchanged).
5. Invalid sources (empty, malformed, reserved, overlength) fail with invalid_source before any database query (lines 329-336).
6. Error messages do not echo untrusted source_id input (lines 352-353 assert source_id not found in error text/JSON or is empty).
7. MCP runtime validation in server.cpp validate_analytics_arguments rejects non-object arguments, wrong types, and unknown fields (lines 980-991, 1043-1055, 1358-1380, 1416-1428).
8. Detail rendering is bounded to 512 bytes on UTF-8 code-point boundaries with sanitization of malformed UTF-8 (lines 107-114, 1249-1258).
9. Outputs contain no page bodies, link contexts, full fact records, secrets, tokens, provider responses, or configuration values (lines 919-937, 967-968, 1029, 1109-1111).
10. Selected-brain isolation prevents cross-database leakage even when source ids and slugs overlap (lines 1113-1124, 1430-1446).

## P0 (blocks PASS)

None.

## P1

None.

## P2 (non-blocking observations)

None. The implementation is complete and correct as specified in the approved plan.

## Native Build and Test Evidence

Compiler: Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64
Language mode: /std:c++20
Target architecture: x64
Build command exit code: 0
Test command exit code: 0
Registered tests: 25
Test results: 25 PASS, 0 FAIL
Test suite includes: test_n18 (line 216 [PASS] n18)

Runtime markers from wave3-final-tests.log line 145:
- snapshot_schema=pass
- snapshot_matrix=pass
- anomalies=45
- contradictions=4
- experts=2
- source_scope=pass
- limit_source_matrix=pass
- contradiction_rule_matrix=pass
- anomaly_matrix=pass
- expert_matrix=pass
- registry_mcp_snapshot_matrix=pass
- mcp_type_validation=pass
- nul_text=pass
- deterministic=pass
- utf8_bounds=pass
- text_json_equivalence=pass
- per_call_snapshot_hashes=pass
- snapshot_call_count=70
- mcp_rpc=pass
- remote_authorization=pass
- selected_snapshot_sha256=b9cab47e628c3c6f9d7696d8b59c06c0bfd6c9ac9987b77e0f4e6669990ce05e
- decoy_snapshot_sha256=eaa0819f03d55a1f1bf2d3fd6f965ac4173f9f82c073c7ca322e848681124537
- read_only=pass

Full snapshot verification: 70 snapshot calls numbered 1-70, each with matching selected_before/selected_after and decoy_before/decoy_after SHA-256 hashes (wave3-final-tests.log lines 146-215).

## Schemas

All three operations declare exact schemas with additionalProperties:false:

find_anomalies:
- source_id: string, default "default"
- limit: integer, minimum 0, maximum 200, default 100

find_contradictions:
- source_id: string, default "default"
- limit: integer, minimum 0, maximum 200, default 100

find_experts:
- source_id: string, default "default"
- limit: integer, minimum 0, maximum 200, default 50

Registry inspection (test_n18.cpp:1270-1293) confirms exact match. MCP tools/list serialization (lines 941-958) confirms exact inputSchema match.

## Conclusion

N18 implementation is complete, correct, and comprehensively verified. All 16 acceptance assertions pass with concrete evidence. The three source-scoped graph heuristics operations (find_anomalies, find_contradictions, find_experts) correctly enforce active-graph filtering, page-owned-fact scoping, source canonicalization without creation, N2.5 remote authorization, 0-200 limit clamping, deterministic content-based ordering, bounded UTF-8-safe detail rendering, selected-brain isolation, and zero-mutation read-only behavior.

Native Windows x64 MSVC C++20 compilation succeeds with exit code 0. The full test suite registers 25 tests with 25 PASS / 0 FAIL including the dedicated test_n18. Runtime evidence records 70 full-snapshot calls with matching before/after hashes for both selected and decoy databases across successful, empty, malformed, unauthorized, and limit-clamped operations.

Plan status currently remains approved. The post-audit parent process may now change plan status to done and reconcile exactly the three N18 ledger rows (find_anomalies, find_contradictions, find_experts) to their documented source-scoped heuristic subset as specified in the approved plan.

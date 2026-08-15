# N19 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N19-PLAN.md (approved, SHA-256 `d03201c31a6a4051b2cc6d5b82e5ced7ae8c4b0982bc65223e251870d01c1d3a`)
**Plan audit**: docs/nodes/N19-PLAN-AUDIT.md (PASS, SHA-256 `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470`)
**Date**: 2026-08-04

## Executive Summary

N19 corrective implementation successfully addresses both P1 findings from the 2026-08-04 outcome audit FAIL. The pre-corrective schema gate provides contemporaneous temporal evidence that schema v12 was verified before corrective work began (gate completed 2026-08-04T08:50:13Z; corrective inputs modified 2026-08-04T09:05:29Z and 2026-08-04T09:20:20Z). The expanded identity test matrix provides exact SQL-derived counter and schema-version comparisons for all four required cells (selected/default, selected/team, decoy/default, decoy/team). All 15 approved-plan acceptance assertions are satisfied with deterministic evidence. Native Windows x64 MSVC C++20 builds succeed, the full suite reports 26 registered / 26 PASS / 0 FAIL including the dedicated N19 test, 204 contiguous snapshot rows prove read-only behavior for selected and decoy databases, and no N30 artifacts or protected configuration changes appear in scoped evidence.

## Acceptance

| # | Assertion from approved plan | Evidence | Status |
|---|------------------------------|----------|--------|
| 1 | Pre-corrective schema gate with exact hashes/temporal proof; v12 compatibility; no migration needed | `PRE-CORRECTIVE-SCHEMA-GATE.json` SHA-256 `2638f4c02a501cf295f8b2ab575e894d051159479b8f6a8beadca28d6544ea3a`; exit 0, `ok=true`, `schema_version=12`; gate completed 2026-08-04T08:50:13.0139314Z; 18 unchanged inputs, 2 corrective inputs modified after gate; `FULL-SUITE-OUTPUT.txt` lines 21-22 report v6→v12 migration idempotent; test matrix includes fresh/populated v12 reopen with unchanged snapshots | **PASS** |
| 2 | Source defaults to `default`, ignores ambient, verifies existence, rejects invalid/unknown/unauthorized before queries | `SNAPSHOT-EVIDENCE.txt` rows 1-2, 12-13, 23-24, 34-35 omitted/mixed-case success; rows 3-8, 14-19, 25-30, 36-41 reject invalid/empty/malformed/unknown/unauthorized before snapshot change; `FOCUSED-RUNTIME-OUTPUT.txt` line 37 `ambient_default=pass`; `MCP-SCHEMA-EVIDENCE.txt` line 7 `ambient_source_exclusion=pass` | **PASS** |
| 3 | Local reads need no allowlist; remote non-default requires N2.5 authorization; `allow_write` cannot bypass; all four operations work with MCP writes disabled | `SNAPSHOT-EVIDENCE.txt` rows 9-10, 20-21, 31-32, 42-43 remote default success; rows 10-11, 21-22, 32-33, 43-44 remote denied before query; `test_n19.cpp:422-429` `allow_write=true` unauthorized denied; `MCP-SCHEMA-EVIDENCE.txt` line 6 `mcp_writes_disabled=pass` | **PASS** |
| 4 | Four-cell exact counter/schema comparisons; local includes path; remote omits it | `test_n19.cpp:194-217` `exact_identity_expected` derives expected values via direct SQL for each source; `test_n19.cpp:219-236` `require_exact_local_identity` asserts `identity["pages"] == expected.pages`, `identity["chunks"] == expected.chunks`, `identity["embedded_chunks"] == expected.embedded_chunks`, `identity["links"] == expected.links`, `identity["schema_version"] == expected.schema_version`, `identity["db_path"] == brain.db_path()`; lines 448-478 call for selected/default, selected/team, decoy/default, decoy/team independently; `SNAPSHOT-EVIDENCE.txt` rows 49-52 identity matrix cells; `test_n19.cpp:480-496` remote omits `db_path`, redacts path sentinels | **PASS** |
| 5 | `volunteer_context` enforces query/alias and 4096-byte UTF-8 contract; modes use correct helpers | `test_n19.cpp:600-657` query/q alias validation including equal-alias acceptance and conflict rejection; 4096-byte accept, 4097-byte reject, malformed UTF-8 reject; `SNAPSHOT-EVIDENCE.txt` rows 57-67 query/alias/size cases; `src/qbrain/ops/handlers.cpp` volunteer_context delegates to `Brain::list_pages_for_source` for recent mode | **PASS** |
| 6 | Query/recent modes have stable membership/order with deterministic tie-breaks | `test_n19.cpp:579-598` reverse fixture insertion, repeat calls, byte-identical JSON comparison; `SNAPSHOT-EVIDENCE.txt` rows 54-56, 60 query repeat and reverse-fixture; `src/qbrain/search/hybrid.cpp` conservative FTS includes slug tie-break (deliverable hash `2a74917efc8e6ceffd8800ab2548fd4ce3c1065671d53dc174bc4bd190b74508` unchanged from PREBUILD) | **PASS** |
| 7 | `get_timeline` returns only active source `type=timeline` pages with predicates before limit; ordered correctly | `test_n19.cpp:756-805` seeds active/deleted timeline and non-timeline pages; proves source/type predicates before limit by making out-of-scope rows newer than in-scope; `SNAPSHOT-EVIDENCE.txt` rows 84-86 timeline default/repeat/team; effective-time/id ordering verified | **PASS** |
| 8 | `volunteer_chronicle` delegates to N15 `chronicle_since`; seven-day bounded default; no unbounded fallback | `test_n19.cpp:807-867` UTC boundary seam with fixed-now injection covers normal week, month-end, year-end, leap transitions; omitted `since` selects seven UTC dates; empty-window returns `[]` without 2000 fallback; `include/qbrain/util/time_util.hpp` SHA-256 `78390f81b3a583fd7e3b0ce3b62e7f4405ef3e0947b266a0e5fb4066b68b8cb3`; `src/qbrain/core/brain.cpp` unchanged `Brain::chronicle_since` three-argument signature reused | **PASS** |
| 9 | Limits consume complete unsigned decimal; defaults only when omitted; clamp correctly; reject invalid | `test_n19.cpp:499-570` exercises empty/sign/whitespace/decimal/suffix/overflow/zero/one/max/over-max for context (1..50) and timeline/chronicle (1..200); `SNAPSHOT-EVIDENCE.txt` rows 69-80, 87-94 volunteer_context limits; rows 95-103, 104-112 timeline/chronicle limits | **PASS** |
| 10 | Validation rejects non-object, wrong types, null, unknown fields, conflicting aliases, invalid dates | `test_n19.cpp:869-1028` MCP typed validation matrix for non-object arguments, wrong types, null, unknown fields; `test_n19.cpp:600-657` alias conflict rejection; `test_n19.cpp:807-867` Chronicle date validation; `MCP-SCHEMA-EVIDENCE.txt` lines 8-11 exact schema with `additionalProperties=false`; `SNAPSHOT-EVIDENCE.txt` rows 135-204 MCP validation cases | **PASS** |
| 11 | Every row contains canonical source, exact fields, valid UTF-8 bounded display; no bodies/paths except local identity | `test_n19.cpp:265-268` `require_rows_from_source` verifies canonical source; `test_n19.cpp:270-273` `require_bounded_json_string` verifies UTF-8 validity and size; `test_n19.cpp:184-184` `require_keys` enforces exact field sets; `test_n19.cpp:488-496` remote path redaction verified; no page body returns in any operation | **PASS** |
| 12 | Full logical snapshots identical before/after for selected and decoy across all calls | `SNAPSHOT-EVIDENCE.txt` 204 contiguous rows (header + 204 snapshot calls + trailing markers); every row reports `selected_before_sha256 == selected_after_sha256` and `decoy_before_sha256 == decoy_after_sha256`; final selected `192e0efd7f46b10c6ab2c65b3ac33481832f05097698318c3f4ee286730bc8f8`, decoy `0d74cc5de575761d7c6573cd8392ded34c77e242d3d977a3fb3fee0d66e40da1`; `test_n19.cpp:73-98` `SnapshotMatrix` enforces before/after identity | **PASS** |
| 13 | Two physical brains with overlapping source ids prove selected-brain and source isolation | `test_n19.cpp:1101-1179` seeds selected and decoy with same source ids but distinct counts/titles/rows/paths; `SNAPSHOT-EVIDENCE.txt` selected final SHA-256 ≠ decoy final SHA-256; `test_n19.cpp:488-496` no decoy path sentinel appears in selected remote response; identity matrix uses separate `exact_identity_expected(selected, ...)` vs `exact_identity_expected(decoy, ...)` | **PASS** |
| 14 | Native Windows x64 MSVC evidence records `/std:c++20`, full details, 26 tests including N19, no N30 references | `EVIDENCE-MANIFEST.json` platform reports `Microsoft Windows 11 专业工作站版 10.0.22624`, `Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36248 for x64`, `/std:c++20`, target `x64`; `FULL-SUITE-OUTPUT.txt` line 3 `expected_registered_tests=26`; lines 10-38 list 26 PASS including `[PASS] n19` line 37; `EVIDENCE-MANIFEST.json` `excluded_node_artifact_count=0`, `excluded_node_build_test_runtime_reference_count=0`; grep for N30 in evidence returns no results | **PASS** |
| 15 | Only after fresh Claude Code outcome-audit PASS may plan become `done`; no N17/N20+/migration/N30/config/commit/push | This audit is the required fresh Claude Code outcome audit; plan status remains `approved` until this PASS; `EVIDENCE-MANIFEST.json` `protected_model_configuration_changed=false`, `commit_or_push_command_executed_by_verifier=false`; `scoped_diff.protected_assignment_change_count=0`; no schema migration edited (SQL hashes unchanged from PREBUILD); N17/N20+ operations out of scope | **PASS** |

## Deliverables Check

| Deliverable | Status |
|-------------|--------|
| `include/qbrain/core/brain.hpp` | Present, SHA-256 `1aa7da46f32c3514320c9c878c0d57aae62721c59698fcfeb58c68ab98256229` ✓ |
| `include/qbrain/util/time_util.hpp` | Present, SHA-256 `78390f81b3a583fd7e3b0ce3b62e7f4405ef3e0947b266a0e5fb4066b68b8cb3` ✓ |
| `src/qbrain/core/brain.cpp` | Present, SHA-256 `24bcd78978dc3aefb8bf7057def006f1fa049a8d63276402403f3b9a3740235b` ✓ |
| `src/qbrain/util/time_util.cpp` | Present, SHA-256 `cb315dc6c50958c6a69270b130a9757881a9030cd27b8b730626d6e296ffe217` ✓ |
| `src/qbrain/search/hybrid.cpp` | Present, SHA-256 `2a74917efc8e6ceffd8800ab2548fd4ce3c1065671d53dc174bc4bd190b74508` ✓ |
| `src/qbrain/ops/handlers.cpp` | Present, SHA-256 `e02c76b55410f4ac02bb973d6e83691ffe1b97856f3a4234cb3677cca3c7ad7e` ✓ |
| `src/qbrain/mcp/server.cpp` | Present, SHA-256 `7515830e9d40c18580723567149569296b55d43a5e93615d7e5e35385564c4fd` ✓ |
| `tests/test_n19.cpp` | Present, SHA-256 `27023d52d3c637b081747d1e66c2a86c84a21c0628fb413d6301af8e00e6d8e9` (corrective), registered in test_main.cpp/CMakeLists.txt/build-tests-cl.ps1 ✓ |
| `scripts/n19-verify.ps1` | Present, SHA-256 `cd6f37cfcb5b4e40ed4af2bec46776814d868905b5d246dc1f511d5fd4ec2e8a` (corrective) ✓ |
| `docs/nodes/n19-evidence/VERIFY-REPORT.md` | Present, SHA-256 `6cf3ee923cf299531d44f2a9ba91b3eef9df13170686fb94ddad2bb52548e0f1` ✓ |
| `docs/nodes/n19-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json` | Present, SHA-256 `2638f4c02a501cf295f8b2ab575e894d051159479b8f6a8beadca28d6544ea3a` ✓ |
| `docs/nodes/n19-evidence/PREBUILD-MANIFEST.json` | Present, SHA-256 `e7b556cb957f0e2eeecd8bfeda8d385727ca6f94f32d4fcb9294e4658aa5aa27` ✓ |
| `docs/nodes/n19-evidence/EVIDENCE-MANIFEST.json` | Present, SHA-256 `fa17ce28a96e5ba62bd0a107d2f28c715678de71e2b645fbd2c1c2cc16c84b94` ✓ |
| Evidence logs (9 files) | All present with declared hashes: PRODUCTION-BUILD-OUTPUT.txt, TEST-BUILD-OUTPUT.txt, FULL-SUITE-OUTPUT.txt, FOCUSED-RUNTIME-OUTPUT.txt, SNAPSHOT-EVIDENCE.txt, MCP-SCHEMA-EVIDENCE.txt, SCHEMA-SMOKE-OUTPUT.txt, PLATFORM-OUTPUT.txt, manifest total 12 files ✓ |

## Findings

### P0 (blocks done)

None.

### P1 (blocks done)

None. Both P1 findings from the 2026-08-04 outcome audit have been resolved:

**P1-1 Resolution Verified**: Pre-corrective schema gate `PRE-CORRECTIVE-SCHEMA-GATE.json` provides contemporaneous temporal evidence. The gate completed at 2026-08-04T08:50:13.0139314+00:00 with exit 0, `ok=true`, `schema_version=12`. The gate artifact records approved-plan SHA-256 `d03201c31a6a4051b2cc6d5b82e5ced7ae8c4b0982bc65223e251870d01c1d3a`, plan-audit SHA-256 `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470`, binary SHA-256 `04460d5ae88d4e1c285cb4f7b4a05df548bfd830772a053b9555f2d45990e7b4`, and pre-corrective input manifest SHA-256 `dbe67c9bfbb31baaa4ae2c19dbac40bb11c9e82c662cd2c73ed930f8e5528cfa` covering 20 scoped inputs before corrective edits. Temporal ordering proof: 18 inputs remain byte-identical; exactly 2 corrective inputs (`tests/test_n19.cpp` modified 2026-08-04T09:05:29.5273994Z, `scripts/n19-verify.ps1` modified 2026-08-04T09:20:20.7511984Z) have timestamps strictly after gate completion. The gate predates all corrective changes.

**P1-2 Resolution Verified**: Identity test matrix provides exact SQL-derived counter and schema-version comparisons for all four cells. `test_n19.cpp:194-217` defines `exact_identity_expected` which queries `storage::check_schema_integrity` for live schema version and runs bound SQL for pages (active count), chunks (join to pages), embedded_chunks (non-NULL embedding count), and links (source-scoped count). `test_n19.cpp:219-236` defines `require_exact_local_identity` which asserts exact equality for `pages`, `chunks`, `embedded_chunks`, `links`, and `schema_version` against the SQL-derived expected values. Lines 448-478 independently call `exact_identity_expected(selected, "default")`, `exact_identity_expected(selected, "team_a")`, `exact_identity_expected(decoy, "default")`, `exact_identity_expected(decoy, "team_a")` and pass each to `require_exact_local_identity` with the corresponding operation result. This satisfies the plan requirement that "Selected/default, selected/team, decoy/default, and decoy/team each compare all four counters and schema version to direct bound SQL/integrity evidence, never broad inequality."

### P2 (advisory, non-blocking)

**P2-1: Snapshot evidence contains 204 data rows plus 4 metadata rows**

`SNAPSHOT-EVIDENCE.txt` contains 208 total lines: 1 header line, 204 snapshot data rows (numbered 1-204 contiguously), 2 summary lines, and 1 trailing marker line. The evidence manifest correctly reports `snapshot_call_count=204`. Each of the 204 rows reports identical before/after SHA-256 for both selected and decoy databases, proving read-only behavior. The verifier parsed all 204 rows and required contiguous indexing with exact before/after identity; a test failure or mutation would have been detected. The slight discrepancy between 204 data rows and 208 file lines is metadata structure, not a missing assertion.

**Recommendation**: None required. The 204-snapshot count matches the test matrix scope and all before/after pairs are verified identical.

**P2-2: Pre-corrective gate artifact uses audited-draft plan SHA-256 for temporal binding**

`PRE-CORRECTIVE-SCHEMA-GATE.json` line 15 records `audited_draft_plan_sha256: f1cffc57a0e3d1c7447eb8c0cacfa130a3afeb05f85cdceaba26406fc69f958e` (the plan SHA before approval) and line 14 records `approved_plan_sha256: d03201c31a6a4051b2cc6d5b82e5ced7ae8c4b0982bc65223e251870d01c1d3a` (the plan SHA after approval status change). The plan-audit SHA is the same in both cases: `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470`. The draft-plan SHA serves as a temporal marker showing the gate was prepared during the approval transition. The approved-plan SHA is the binding hash for outcome audit matching. Both fields are present and correct.

**Recommendation**: None required. The dual SHA-256 recording provides additional temporal proof without ambiguity.

**P2-3: Full-suite output includes expected provider timeout and concurrent SQLite-busy markers from unrelated tests**

`FULL-SUITE-OUTPUT.txt` line 17 reports a rerank provider timeout (`transport_timeout`, `fallback_taken=true`) and line 19 reports a minions concurrent-claim SQLite busy condition (`winner=tok-race-A`, `loser=sqlite_busy`, `error=database is locked`). These are expected markers from existing N7 rerank and N17 minions tests, not N19 failures. Line 37 shows `[PASS] n19` with all expected markers present. The suite result is 26 registered / 26 PASS / 0 FAIL.

**Recommendation**: None required. Unrelated test markers do not affect N19 acceptance.

## Conclusion

All 15 approved-plan acceptance assertions are satisfied with deterministic evidence. The pre-corrective schema gate provides contemporaneous temporal proof that schema v12 was verified before corrective implementation began, resolving P1-1. The expanded identity test matrix provides exact SQL-derived counter and schema-version comparisons for all four required cells (selected/default, selected/team, decoy/default, decoy/team), resolving P1-2. Native Windows x64 MSVC C++20 builds succeed with exit code 0. The full test suite reports 26 registered / 26 PASS / 0 FAIL, including the dedicated N19 test. Exactly 204 contiguous snapshot rows prove read-only behavior for selected and decoy databases across all four N19 operations. No N30 artifacts, protected configuration changes, or premature ledger reconciliation appear in scoped evidence. Three advisory P2 findings are recorded but do not block acceptance.

**The parent may now mark N19-PLAN.md `Status: done` and reconcile exactly the four N19 ledger rows: `get_brain_identity`, `volunteer_context`, `get_timeline`, and `volunteer_chronicle`.**

# N17 HARD AUDIT - Job Replay and Job Message Inbox

**VERDICT: PASS**

**Auditor**: Claude Code
**Date**: 2026-08-04
**Plan**: docs/nodes/N17-PLAN.md
**Plan SHA-256**: `3954237b5707348963821bd72a5eccec5f805e437478b9fc02f135761582bf77`
**Plan Audit**: docs/nodes/N17-PLAN-AUDIT.md
**Plan Audit SHA-256**: `46cded0d8537f432617e0fed4d59b1d9e842ce255ee6851ce224b4cea177433b`

---

## Executive Summary

N17 is a retrospective correctness closure that re-verifies and corrects existing job replay and job-message surfaces against the current schema v12, selected-brain, strict-input, MCP default-deny, and native Windows evidence contracts. The implementation successfully delivers:

1. **Corrective breaking change**: Replay restricted to terminal `failed`/`completed` states only, rejecting `waiting`/`active`/other nonterminal states to prevent duplicate work and token-fence bypass
2. **Strict job-id parsing**: Full-string validation rejecting whitespace, signs, suffixes, overflow, and embedded control bytes
3. **Structured errors**: Bounded JSON error responses with explicit codes (`invalid_argument`, `not_found`, `invalid_state`, `database_busy`, `database_error`)
4. **Registered list helper**: `list_job_messages` now registered as Scope::Read with real MCP/CLI paths
5. **Schema v8 verification**: Existing v8 DDL unchanged; targeted integrity checks added for damaged-v12 detection
6. **Native Windows x64 evidence**: MSVC C++20 production/test builds, 26 PASS/0 FAIL, isolated temporary databases, zero N30 artifacts

All 16 acceptance assertions from the approved plan are satisfied by concrete implementation and runtime evidence.

---

## Acceptance Assertions

| # | Assertion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Retrospective closure identifies historical pre-fix behavior and proves corrective surfaces | **PASS** | VERIFY-REPORT.md line 33-38: "The approved plan identifies the historical pre-fix behavior as any-status replay, permissive prefix/whitespace id parsing, generic failures, and an unregistered list helper. The dedicated current test marker records the strict-id matrix, terminal-only replay matrix, structured MCP validation, registered tools/list path, default-deny/allow-write paths..." Corrective work is explicitly proven, not assumed. |
| 2 | All N17 job ids consume entire positive ASCII decimal 1..INT64_MAX; malformed/conflicting/wrong-typed/overflow/non-positive/unknown/unexpected-field inputs return structured errors before mutation | **PASS** | RUNTIME-MARKERS.txt line 4: `strict_id_cases=84`; test_n17.cpp:467-496 exercises empty, zero, signs, whitespace, decimals, suffixes, control bytes, INT64_MAX+1, overflow; handlers.cpp uses `parse_strict_job_id` (not `std::stoll`); all rejections preserve snapshots. |
| 3 | Replay's intentional breaking correction enforced: only `failed`/`completed` succeed; `waiting`/`active`/`paused`/`cancelled`/`dead`/arbitrary rejected, no silent compatibility | **PASS** | RUNTIME-MARKERS.txt line 4: `replay_terminal_state_matrix=pass replay_state_cases=9`; test_n17.cpp:499-541 seeds all states; minions.cpp:218-223 checks `status != "failed" && status != "completed"` returns `invalid_state`. Historical any-status behavior is explicitly corrected. |
| 4 | One successful replay creates exactly one distinct `waiting` row with only queue/type/payload/priority copied, reset attempts/result/error/fence, fresh timestamps; complete original row byte-equivalent | **PASS** | test_n17.cpp:357-421 `require_replay_delta`: atomic clone verified by snapshot diff; original row unchanged including unusual result/error/fence values; new row has distinct id, copied fields match byte-for-byte, waiting status, attempts=0, null result/lock. Evidence: `replay_race_successes=1`. |
| 5 | Replay preserves original payload byte-for-byte, copies no message, changes no other application table; handles both-success and busy-loser concurrency schedules with declared row/sequence deltas | **PASS** | test_n17.cpp:406-407 compares raw `payload_json` database text directly without parse/dump; line 419-420 confirms zero messages on new id; line 641-674 concurrency matrix accepts both-success (2 rows) and busy-loser (1 row, retry adds 1) schedules. Evidence: `replay_race_busy=1`, `successful_replay_delta=jobs:+1,jobs_sequence:+1,other_application_state:+0,decoy:+0`. |
| 6 | One successful message send requires existing job, stores declared sender and canonical valid JSON within exact UTF-8/byte bounds, inserts exactly one row, changes no job/unrelated table | **PASS** | test_n17.cpp:424-465 `require_message_delta`: requires job existence (line 438); validates sender/payload stored canonically (lines 460-462); snapshot diff confirms exactly one `job_messages` row added, no change to jobs or other tables. Evidence: `sender_payload_cases=26`, `successful_message_delta=job_messages:+1,job_messages_sequence:+1,other_application_state:+0,decoy:+0`. |
| 7 | Invalid sender/payload, malformed UTF-8/JSON, boundary-plus-one input, nonexistent job perform neither insert nor prune; application rows plus sequence state unchanged | **PASS** | test_n17.cpp:549-607 exercises sender/payload boundaries: 1/128/129 bytes, malformed UTF-8, controls/NUL, valid JSON 65536 bytes, 65537 (boundary+1), malformed JSON; lines 574-575 use `without_mutation` helper verifying full snapshot equality including `sqlite_sequence`. Evidence: `sender_payload_utf8_json_boundaries=pass sender_payload_cases=26`. |
| 8 | Message listing registered, distinguishes missing job from empty inbox, returns ≤effective 1..200 limit in deterministic `id DESC` order with exact five-field shape; repeated unchanged reads byte-identical | **PASS** | OPERATION-SCHEMAS.json line 83-120: `list_job_messages` registered; test_n17.cpp:609-639 verifies missing-job vs empty-inbox distinction (lines 613-617), newest-first `id DESC` order (line 627), exact five fields `{id, job_id, sender, payload_json, created_at}` (line 625), byte-identical repeated output (line 633-636), limit bounds 0→1, 1, 50, 200, 201→200. Evidence: `list_limit_cases=15`, `missing_vs_empty_list=pass list_limit_matrix=pass`. |
| 9 | Every read, malformed call, unknown-id, invalid-state write, denied remote write preserves defined full logical snapshots of both selected and decoy brains; valid writes have only exact declared selected-brain row/sequence deltas | **PASS** | test_n17.cpp:42-44 uses `qbrain::test_support::logical_snapshot` from wave3_test_support.hpp; all rejection/read/deny paths use `without_mutation` helper verifying selected+decoy snapshots unchanged (e.g., line 474-477); valid write assertions verify exact +1 row and corresponding sequence advance only (lines 447-454). Evidence: snapshots captured for both success/failure schedules. |
| 10 | Existing v8 DDL and version-marker source unchanged; fresh/populated-v7/current-v12 fixtures prove exact table/index/no-FK shape, idempotent traversal, injected rollback, targeted damaged-v12 integrity detection without silent repair | **PASS** | SCHEMA-EVIDENCE.txt line 3-5: `head_v8_source_sha256` == `current_v8_source_sha256`, `v8_source_unchanged=true`; line 6-7: `current_schema_version=12`, `schema_version_above_12_count=0`; test_n17.cpp:790-836 verifies exact v8 table/index shape including no-FK (line 835); lines 838-874 create v7 fixture, migrate through v8 to v12, verify idempotence; lines 728-781 inject rollback at v8 marker, verify clean recovery. Evidence: `migration_v7_v8_v12=pass migration_idempotence=pass migration_rollback=pass damaged_integrity=pass`. |
| 11 | Explicit no-FK orphan policy: N17 has no job deletion, atomic send requires existing job, missing-parent list rejected, no cascade claim in docs or ledger | **PASS** | test_n17.cpp:834-835: `PRAGMA foreign_key_list(job_messages)` returns empty; minions.cpp:346-349 sends only if job exists (atomic query with INSERT); test_n17.cpp:613-617 list rejects missing job with `not_found` before empty-inbox case; plan lines 132-135 explicitly state no-FK orphan policy; no CASCADE keyword in implementation or evidence. |
| 12 | Registry and real MCP evidence show replay/send as Write+`local_only=true`, list as Read, exact `additionalProperties:false` typed schemas, pre-handler remote denial without allow-write, structured bounded errors, valid explicitly allowed writes | **PASS** | N17-SCOPE-MANIFEST.txt lines 23-28: `replay_job_scope=Write local_only=true`, `send_job_message_scope=Write local_only=true`, `list_job_messages_scope=Read local_only=false`; OPERATION-SCHEMAS.json: all three operations have `"additionalProperties":false`, exact integer bounds 1..INT64_MAX for job_id, string bounds for sender/payload, limit 0..200; test_n17.cpp:683-726 `mcp_rejection_cases=26` verifies pre-dispatch rejection of non-object, unknown fields, wrong types, then remote denial before handler, then valid allowed writes succeed. Evidence: `registry_tools_list=pass real_mcp=pass default_deny=pass allow_write=pass mcp_rejection_cases=26`. |
| 13 | All three operations stay inside `ctx.brain`; no cross-brain rows, source creation, ambient-source interpretation, filesystem/network/provider work, job side effects beyond replay, model/configuration data | **PASS** | minions.cpp:201-377: all operations receive `Brain&` parameter, use only that database handle; no `QBRAIN_SOURCE` access, no source creation calls, no filesystem/network/provider APIs; handlers.cpp registration uses `ctx.brain` for all three operations; N17-SCOPE-MANIFEST.txt line 20: `live_network_call_count=0`; lines 13-19: zero protected model configuration changes, isolated config absent or canonical-defaults-only. |
| 14 | Native Windows x64 MSVC `/std:c++20` production/test builds exit zero, complete suite records ≥26 PASS/0 FAIL including dedicated N17, evidence manifest records exact commands/hashes/snapshots/deltas/concurrency/zero-N30/zero-N17-created-later-node paths | **PASS** | VERIFY-REPORT.md lines 6-18: Windows 11 x64, MSVC 19.51.36248, `/std:c++20`, cl.exe hash `dc8426b8...`, qbrain.exe hash `dcb8d0c2...`, qbrain_tests.exe hash `a9bd5a42...`; lines 20-31: all commands exit 0, exactly 26 PASS/0 FAIL including `[PASS] n17`; BUILD-MANIFEST.txt line 14: `observed_fail_count=0`; N17-SCOPE-MANIFEST.txt lines 11-13: `n30_artifact_count=0`, `n30_repository_path_count=0`, `n17_created_or_modified_n19_or_later_path_count=0`. Evidence hashes: selected snapshot `bc270828...`, decoy `fd440154...`, migration `b37d7576...`, rollback `b0dd9c48...`. |
| 15 | Only `replay_job` and `send_job_message` receive fresh N17 ledger notes after outcome audit passes; `list_job_messages` remains uncounted Qbrain helper, operation total not incremented for it | **PASS** | Plan lines 42-47 explicitly state: "`list_job_messages` is a Qbrain read helper required to make the inbox usable and testable. It is not an upstream ledger row and must not be added to, counted in, or represented as an implemented operation in the upstream parity ledger." Only two upstream operations claimed (lines 40-46 table). This audit confirms implementation matches that contract; ledger reconciliation is post-audit human responsibility per plan line 166. |
| 16 | No schema v13, downgrade tool, message retention/deletion, N19+implementation, N30 artifact/dependency, third-party dependency, model configuration change, commit, push is part of N17 | **PASS** | SCHEMA-EVIDENCE.txt line 7: `schema_version_above_12_count=0`; N17-SCOPE-MANIFEST.txt: `n30_artifact_count=0`, `n30_repository_path_count=0`, `n17_created_or_modified_n19_or_later_path_count=0`, `protected_model_configuration_scoped_path_count=0`, `third_party_dependency_change_count=0`, `git_mutating_command_count=0`; lines 7-8: git HEAD identical before/after; no message deletion/retention code in deliverables; N19/N20+ tests exist as pre-existing regression baseline, not N17 work. |

---

## Deliverables Check

All 14 scoped deliverables present with expected hashes per EVIDENCE-MANIFEST.txt and N17-SCOPE-MANIFEST.txt:

| Path | SHA-256 | Purpose |
|------|---------|---------|
| `include/qbrain/jobs/minions.hpp` | `00da6811...` | N17 API declarations |
| `src/qbrain/jobs/minions.cpp` | `245b7bae...` | Replay/message implementation with strict validation |
| `src/qbrain/ops/handlers.cpp` | `e02c76b5...` | Operation registrations, strict id parser |
| `src/qbrain/mcp/server.cpp` | `7515830e...` | Typed MCP argument map |
| `src/qbrain/ops/registry.cpp` | `e3fb8bad...` | Structured pre-handler authorization errors |
| `src/qbrain/storage/migrate.cpp` | `d775529f...` | v8 verification, targeted v12 integrity checks |
| `tests/test_n17.cpp` | `e4fe3eaa...` | Dedicated N17 test: 84 id cases, 9 replay states, 26 sender/payload cases, 15 list limits, 26 MCP rejections, concurrency |
| `tests/wave3_test_support.hpp` | `9a4f9409...` | Shared snapshot utilities |
| `tests/test_main.cpp` | `e47ab532...` | Test registration |
| `CMakeLists.txt` | `24a46685...` | Build configuration |
| `scripts/build-tests-cl.ps1` | `a682bb81...` | Native Windows test build |
| `scripts/n17-verify.ps1` | `1c8991fd...` | Evidence capture script |
| `docs/nodes/N17-PLAN.md` | `3954237b...` | Approved plan |
| `docs/nodes/N17-PLAN-AUDIT.md` | `46cded0d...` | Plan audit PASS |

Evidence artifacts generated per EVIDENCE-MANIFEST.txt: PRODUCTION-BUILD-OUTPUT.txt, TEST-BUILD-OUTPUT.txt, BUILD-MANIFEST.txt, TEST-OUTPUT.txt, CLI-SMOKE-OUTPUT.txt, RUNTIME-MARKERS.txt, SCHEMA-EVIDENCE.txt, OPERATION-SCHEMAS.json, N17-SCOPE-MANIFEST.txt, VERIFY-REPORT.md (self-documenting).

---

## P0 Issues (blocking)

None.

---

## P1 Issues (should fix)

None.

---

## P2 Observations (non-blocking)

**P2-1: Concurrency schedule observation**

Test evidence shows exactly one success and one busy result for both replay and message concurrency tests (RUNTIME-MARKERS.txt: `replay_race_successes=1 replay_race_busy=1 message_race_successes=1 message_race_busy=1`). The test correctly accepts both legitimate schedules per plan lines 196-198. This is sound evidence; hardware/WAL/timing variations are expected. No action required.

**P2-2: Historical audit artifact disposition**

The existing `docs/nodes/N17-HARD-AUDIT.md` dated 2026-07-26 is correctly replaced by this fresh 2026-08-04 audit. Plan lines 7-9 state: "The replay/message code, historical ledger claims, tests, and 2026-07-26 `N17-HARD-AUDIT.md` may already exist or have been used in production, but they were produced under the old combined-wave process. They are background evidence only and do not satisfy the current node-specific plan-audit or outcome-audit gates." This audit independently verifies current implementation against current contracts; historical PASS text was not used as a gate.

**P2-3: Test marker interpretation**

RUNTIME-MARKERS.txt line 4 is a single-line structured marker emitted by test_n17.cpp containing all matrix outcomes. The evidence report (VERIFY-REPORT.md lines 33-38) correctly interprets this marker as proof of corrective work: strict-id matrix, terminal-only replay matrix, structured MCP validation, registered tools/list path, default-deny/allow-write paths, exact snapshots, and concurrency schedules. This is faithful to plan requirement that evidence "identifies the historical pre-fix behavior and proves each named corrective surface" (acceptance assertion 1).

---

## Conclusion

N17 successfully closes governance and correctness gaps in existing job replay and job-message surfaces. All 16 acceptance assertions are satisfied with concrete implementation and runtime evidence:

- **Corrective breaking changes** explicitly proven: terminal-state replay guard, strict full-string id parsing, structured errors, registered list helper
- **Schema v8 verification** complete: existing DDL unchanged, targeted v12 integrity checks added, no-FK orphan policy documented and tested
- **MCP security** enforced: Write operations `local_only=true`, `additionalProperties:false` schemas, pre-handler remote denial, structured bounded errors
- **Exact-delta guarantees** verified: replay adds exactly one `jobs` row, send adds exactly one `job_messages` row, all other tables unchanged, decoy brain unchanged, both success and busy-loser concurrency schedules handled correctly
- **Native Windows evidence** complete: MSVC C++20 x64, 26 PASS/0 FAIL, isolated temporary databases, zero N30 artifacts, zero model configuration changes, zero commits/pushes

The implementation is correct, complete, secure, and ready for production. Only `replay_job` and `send_job_message` should receive fresh ledger reconciliation (per plan lines 42-47, `list_job_messages` is an uncounted helper). N17 may be marked `done` after human approval of this audit.

**VERDICT: PASS**

---

**Audit completed**: 2026-08-04
**Auditor**: Claude Code
**Next step**: Human approval to mark N17 done and reconcile exactly two ledger rows (`replay_job`, `send_job_message`)

# N12 Outcome Evidence

**Node**: N12 - Rerank, Minions, and Multi-phase Dream  
**Plan**: `docs/nodes/N12-PLAN.md`  
**Plan status at capture**: approved; outcome audit pending  
**Evidence capture**: 2026-07-29 UTC  
**Audit status**: This file is runtime and source evidence only. It is not a Claude Code outcome hard-audit verdict.

## Gate Preconditions

The approved N12 plan requires node-specific plan and outcome PASS artifacts for N1-N11 before implementation. The complete precondition table, including SHA-256 hashes of each audit artifact, is recorded in [`n12-evidence/VERIFY-REPORT.md`](n12-evidence/VERIFY-REPORT.md). Every listed dependency is marked `done` with a plan audit PASS and an outcome audit PASS.

The N12 plan audit is [`N12-PLAN-AUDIT.md`](N12-PLAN-AUDIT.md), with `VERDICT: PASS`, `Auditor: Claude Code`, dated 2026-07-29. The historical [`N12-HARD-AUDIT.md`](N12-HARD-AUDIT.md) is dated 2026-07-26 and is not used as evidence for this retrospective implementation.

## Build And Runtime

Environment and command facts:

- Windows 11 native: build 22624.
- MSVC compiler: 19.51.36248, x64 target.
- Language mode: `/std:c++20`.
- Build command: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1`.
- Test command: `build\cl\qbrain_tests.exe`.
- Build output contains `TESTS_BUILD_OK`.
- Exact test result: 20 registered tests, 20 `[PASS]`, 0 `[FAIL]`, process exit code 0.
- The existing `live_sync` diagnostic is a non-failing warning about a deliberately invalid fixture path.

Raw logs:

- [`BUILD-OUTPUT.txt`](n12-evidence/BUILD-OUTPUT.txt)
- [`TEST-OUTPUT.txt`](n12-evidence/TEST-OUTPUT.txt)
- [`VERIFY-REPORT.md`](n12-evidence/VERIFY-REPORT.md)

## Acceptance Evidence

| Approved-plan area | Observed evidence |
|---|---|
| Rerank fail-open and deterministic fallback | `test_rerank` passes throw, empty, partial, duplicate, foreign, malformed, transport, and timeout cases. Fallback preserves the local baseline membership and exact serialized triples; all fallback scores are finite and in `[0,1]`. |
| Rerank timeout and audit safety | `[INFO] silent_provider_elapsed_ms=3961`; the real loopback listener accepts and stays silent. The redacted sample is `{"doc_count":3,"failure_reason":"transport_timeout","fallback_taken":true,"query_hash":"abadadc5b647126edb5c309a536952857ea008c0ec22babd6e0c74dd9791408e","timestamp":"2026-07-29T20:52:13Z"}`. The sample has exactly the five approved fields and no prompt, response, exception body, or key. Rotation preserves a `.1` generation at the 1 MiB boundary. |
| Minion token fence and lifecycle | `test_minions` passes empty/wrong/stale token rejection, duplicate claim rejection, bounded failure text, waiting/active/terminal cancellation, reclaim lease clearing, and exact attempt increments. |
| Concurrent claim loser | `[INFO] minions_concurrent_claim winner=tok-race-A loser=sqlite_busy error=step: database is locked`. Exactly one independent SQLite connection claims the row. |
| Populated-v5 migration | `[INFO] migration_v6 populated_v5=preserved idempotent=noop rollback_after_first_ddl=clean rollback_after_marker=clean migrated_job_fence=pass fresh_shape=pass`. The populated fixture snapshot hash is `692927dc1b80165584578f7256eac3ff956e4dd468552e96fb35038d9849bda4`. |
| Dream dry-run isolation | `[INFO] dream_dry_run_snapshot_sha256=447b07f81e145d5f74dffac396df1faace0399121b7f05bbdd811c052c509cf0 phases=orphans,extract_facts,consolidate,embed,purge unchanged=pass`. The hash covers every non-SQLite user table, not only page/chunk/link counts. |
| Dream phase mutations and purge boundary | `test_n12_dream` exercises all five dry-run phases, selected apply phases, mock embed with non-zero mutations, no-phase apply with purge skipped, explicit purge eligibility/cascade/reference cleanup, and all retention boundary inputs. The first explicit purge reports `mutations=7`; before/after snapshot hashes are `3a45db1ae4fa98811c9f843db9e993c7af58c952f723b725b9ca63c8d88c4f23` and `f7fe43f2134531f8b1f083f008b46c051d9ca3195034964131cd4b3bbc625a36`. |
| Remote MCP write denial | `[INFO] mcp_write_deny_snapshot_sha256=e650e3892a8736511efda93fe2b33d8a8b69752416f89d7d54a6803ca8c5a574 ops=submit_job,cancel_job,run_dream unchanged=pass`. The test checks the full database snapshot and verifies the waiting cancellation target remains waiting. The implementation marks `submit_job`, `cancel_job`, and `run_dream` local-only, so `--allow-write` remains the explicit enable path. |

## Changed Surface

The implementation and focused tests are hashed in the deliverable table in [`n12-evidence/VERIFY-REPORT.md`](n12-evidence/VERIFY-REPORT.md). The bounded surface is:

- `rerank.cpp` / `chat.cpp` and their headers, including the rerank-only 3000 ms deadline and safe rotating audit sink.
- `minions.cpp` / migration v6 and their headers, including token fencing, reclaim accounting, bounded errors, nullable v6 columns, and transactional rollback.
- `dream.cpp` / `dream.hpp`, CLI and MCP argument plumbing, including mutation counters, retention parsing, purge target freezing, and explicit cascade/reference cleanup.
- Focused N12 tests, test registration, the MSVC test build list, and this evidence harness.

No LLM provider, model, base URL, API key, reasoning, context, or compression configuration was changed by N12 implementation or tests. No commit or push was performed.

## Remaining Gate

The required next step is a fresh Claude Code outcome hard audit against the approved N12 plan and this evidence. N12 must remain not-done until that audit writes a real `VERDICT: PASS` with no P0/P1 findings. Only after that gate may the plan status change to `done` and the N12 rows in `docs/OPS-PARITY-LEDGER.md` be reconciled.

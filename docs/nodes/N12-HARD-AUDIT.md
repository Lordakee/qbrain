# N12 Outcome Hard Audit

**VERDICT: PASS**
**Auditor: Claude Code**
**Approved plan**: `docs/nodes/N12-PLAN.md`
**Plan audit**: `docs/nodes/N12-PLAN-AUDIT.md` (`VERDICT: PASS`)
**Audit date**: 2026-07-29
**Scope**: Read-only review of the current implementation, focused tests, native Windows/MSVC logs, and outcome evidence.
**Historical audit**: The 2026-07-26 N12 audit was not used as this verdict and has been replaced by this fresh audit.

## Audit Provenance

Claude Code was invoked with model `claude-opus-5`, high effort, `Read` tools only, plan permission mode, no session persistence, and no file-edit authority. The first response reached `VERDICT: PASS` but was truncated during acceptance row 12, so it was not accepted as the gate artifact. A fresh concise Claude Code audit re-read the current files and returned every mandatory section.

Raw responses:

- `docs/nodes/n12-evidence/CLAUDE-N12-HARD-AUDIT-RESPONSE.txt` (truncated; provenance only)
- `docs/nodes/n12-evidence/CLAUDE-N12-HARD-AUDIT-RESPONSE-2.txt` (complete; source of this audit)

## Acceptance Table

| # | Assertion | Result | Claude Code evidence |
|---|---|---|---|
| 1 | Rerank failure never throws, exits non-zero, or empties non-truncating results | **PASS** | `tests/test_rerank.cpp` throw injection and `docs/nodes/n12-evidence/TEST-OUTPUT.txt` `[PASS] rerank`. |
| 2 | Fallback membership/count/scores match the no-LLM local baseline | **PASS** | `serialize_triples` equality in `tests/test_rerank.cpp`; finite `[0,1]` scoring in `src/qbrain/search/rerank.cpp`. |
| 3 | Repeat triples are byte-identical; silent provider falls back below 10 seconds | **PASS** | Repeated baseline equality and the real silent-loopback assertion; final evidence records `silent_provider_elapsed_ms=3961`. |
| 4 | Audit has only safe fields, a closed failure enum, no secret/payload, and 1 MiB rotation | **PASS** | Exact five-field JSON sample in `TEST-OUTPUT.txt`; closed-enum/redaction/rotation assertions in `tests/test_rerank.cpp`. |
| 5 | No active double claim; wrong/stale token is rejected, including migrated-v5 jobs | **PASS** | Duplicate/wrong/stale token matrix in `tests/test_minions.cpp` and migrated job checks in `tests/test_migration_v6.cpp`. |
| 6 | Reclaim increments once and clears its fence; terminal states hold; concurrent claim has one winner | **PASS** | Reclaim assertions plus final runtime marker `winner=tok-race-A loser=sqlite_busy error=step: database is locked`. |
| 7 | Remote submit/cancel/dream writes are denied without allow-write and allowed explicitly | **PASS** | All three operations are local-only; `tests/test_n12_dream.cpp` proves identical full snapshots after each denied request; `tests/test_mcp.cpp` proves the allow-write path. |
| 8 | Default dry-run reports all five phases with zero mutations and an unchanged full snapshot | **PASS** | Five isolated dry runs plus all-phase dry run; final full-table SHA-256 marker is recorded in `VERIFY-REPORT.md`. |
| 9 | Selected non-purge phases reconcile mutation counts; mock embed mutates non-zero rows | **PASS** | Per-table deltas for orphans/extract/consolidate; `QBRAIN_EMBED_MOCK=1` completes two jobs with non-empty deterministic vectors. |
| 10 | Apply without phase skips purge; explicit purge obeys eligibility, cascade, and retention bounds | **PASS** | `skipped_purge` implementation and purge fixture covering recent deleted, old active, cross-source same slug, references, and every numeric boundary. |
| 11 | Every phase has isolated dry/apply coverage; invalid retention fails without mutation | **PASS** | Five-phase matrix and nonnumeric/decimal/positive-overflow/negative-overflow snapshot checks in `tests/test_n12_dream.cpp`. |
| 12 | Populated v5 upgrades without content loss; v6 is nullable, transactional, idempotent, and fenced | **PASS** | Stable populated-v5 hash, second-run no-op, first-DDL and marker rollback injection, migrated-job lifecycle, and fresh shape checks. |
| 13 | N1-N11 gates, MSVC metadata, `/std:c++20`, exact commands, and exact test count are recorded | **PASS** | `VERIFY-REPORT.md` records every dependency audit hash, MSVC 19.51 x64, native Windows, `/std:c++20`, commands, and 20 PASS / 0 FAIL. |

## Deliverables

**PASS.** Claude Code confirmed the hashed deliverables in `docs/nodes/n12-evidence/VERIFY-REPORT.md`:

- Rerank/chat implementation with the 3000 ms bounded call, fail-open baseline, safe JSONL audit, and rotation.
- Minion lifecycle with token fencing, bounded errors, reclaim accounting, and concurrent-claim behavior.
- Five-phase dream implementation with mutation counters, strict retention parsing, explicit purge target set, and reference/cascade cleanup.
- Transactional schema-v6 migration with populated-v5, idempotence, rollback-injection, and migrated-job tests.
- Focused tests, canonical MSVC build registration, PowerShell verification harness, raw logs, and outcome evidence.

No protected LLM provider, model, base URL, API key, reasoning, context, or compression configuration was changed.

## P0

None.

## P1

None.

## P2

1. The exact concurrent loser in a race can be either the documented `no_job` result or a documented SQLite busy result. This run recorded `sqlite_busy`; a CI stress loop could improve repeatability evidence, but the one-winner product contract is already enforced.
2. Schema v6 is additive and intentionally has no supported downgrade path. The documented mitigation is to take a pre-migration backup and restore it instead of attempting schema rollback.

## Comparison To Approved Plan

Claude Code found all 13 falsifiable acceptance assertions satisfied by current source and runtime evidence. The implementation remains within the approved rerank/minion/dream/migration/CLI-MCP/test scope, retains Windows-native C++20/MSVC operation, preserves MCP write default-deny, and introduces no provider/model configuration change.

Remaining risk is limited to the two documented P2 observations. Neither weakens an acceptance contract or blocks completion.

## Conclusion

N12 matches the approved plan with 13/13 acceptance assertions PASS, all required deliverables present, native MSVC build evidence, 20/20 registered tests PASS, no P0 findings, and no P1 findings.

**VERDICT: PASS**

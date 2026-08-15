# N1 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: `docs/nodes/N1-PLAN.md`
**Plan audit**: `docs/nodes/N1-PLAN-AUDIT.md`
**Date**: 2026-07-28
**Audit type**: Outcome re-audit under approved N29 governance reconciliation

## Acceptance

| # | Assertion from plan | Evidence | Status |
|---|---------------------|----------|--------|
| 1 | Fresh put followed by embed drain embeds chunks | Prior put -> `embed --drain` evidence embedded 6/7; current implementation retains the queue/drain path | PASS |
| 2 | Put without an embedding key does not fail | `enqueue_embed_page` no-ops when no key is configured | PASS |
| 3 | Doctor schema is at least v4 | Migration v4 and doctor schema evidence | PASS |
| 4 | Unit tests are green | Fresh MSVC-linked `build\cl\qbrain_tests.exe`: 18/18 PASS | PASS |
| 5 | MCP capture remains denied without `--allow-write` | Registry `local_only` guard remains in place | PASS |
| 6 | Remote put skips link extraction | `handlers.cpp` remote branch confirms the mitigation | PASS |
| 7 | Provenance columns are persisted | Migration v4 plus `put_page` INSERT for `source_kind`, `ingested_via`, and `ingested_at` | PASS |

## Deliverables check

| Deliverable | Status |
|-------------|--------|
| Post-write embed enqueue and drain | PASS |
| Default-deny MCP write gate with allow-write path | PASS |
| Page provenance migration and persistence | PASS |
| Remote-put link-extraction mitigation | PASS |

## Findings

### P0 (blocks done)

None.

### P1

None.

### P2

1. The historical drain run is referenced by evidence rather than reproduced as a new transcript.
2. The 18-test suite is newer than the original six-test evidence, but the growth is additive and the fresh run is green.
3. The drain count does not identify which page was skipped; the independent keyless-no-op behavior covers that acceptance.

## Conclusion

All seven acceptance assertions and deliverables pass with no P0 or P1 findings. N1 outcome is PASS and the node may remain done.

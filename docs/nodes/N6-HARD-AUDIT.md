# N6 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N6-PLAN.md (approved)
**Plan audit**: docs/nodes/N6-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude One-Liner
VERDICT=PASS - reported evidence covers every approved N6 item (mock embed provider, --drain job/chunk mutation plus failure marking, existing finite worker --once, dream dry/apply) with matching test_wave4 cases and 16/16 qbrain_tests green.

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | embed drain stores embeddings | QBRAIN_EMBED_MOCK + drain_embed_jobs + chunk embedding | PASS |
| 2 | empty drain success | second drain returns 0 | PASS |
| 3 | bad config no rollback | failed job; page retained | PASS |
| 4 | no double-complete | second drain no completed repeat | PASS |
| 5 | dream dry-run no facts | facts count unchanged | PASS |
| 6 | dream apply facts | consolidate apply increases facts | PASS |
| 7 | MCP apply default-deny | remote run_dream without allow_write denied | PASS |
| 8 | worker once finite | finite drain_jobs and existing worker --once path | PASS |
| 9 | stretch not silently claimed | ledger note retained; plan scopes scheduler/cron deferred | PASS |

## Unit Suite
`qbrain_tests.exe`: **16/16 PASS**

## Conclusion
**N6 done.**

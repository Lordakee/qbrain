# N4 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N4-PLAN.md (approved)
**Plan audit**: docs/nodes/N4-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude one-liner
VERDICT=PASS — dual base_url handled, gated save is correctly a no-op without allow-write, deferred multi-round is scoped out, and all 15 checks pass.

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | dual chat/embed base_url | config + gateway paths | PASS |
| 2 | no chat key → no page write | think degraded/error; no mutate | PASS |
| 3 | save without allow-write no mutate | mcp think+save pages unchanged | PASS |
| 4 | separate embed path | embed client distinct URL/model | PASS |
| 5 | multi-round not claimed | single-round think only | PASS |

## Unit suite
**15/15 PASS**

## Conclusion
**N4 done.**

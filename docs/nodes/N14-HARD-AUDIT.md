# N14 HARD AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N14-PLAN.md
**Date**: 2026-07-26

## Acceptance
| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | pause waiting/active to paused | minions.cpp + test_minions PASS | PASS |
| 2 | resume paused to waiting | test_minions PASS | PASS |
| 3 | get_job_progress fields | handlers + unit | PASS |
| 4 | get_status_snapshot counts | status_snapshot + doctor | PASS |
| 5 | doctor --remediate | runtime JSON default_source true schema 7 | PASS |

## Findings
### P0
None.

### P1
None blocking.

### P2
Broader gbrain parity still deferred outside this node.

## Conclusion
N14 job control and doctor remediate meet plan goals. Unit suite green.

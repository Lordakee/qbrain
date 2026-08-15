# N21 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: `docs/nodes/N21-PLAN.md`
**Plan audit**: `docs/nodes/N21-PLAN-AUDIT.md`
**Date**: 2026-07-29
**Audit type**: Retrospective N29 reconciliation; implementation already exists

## Acceptance

| # | Assertion from plan | Evidence | Status |
|---|---------------------|----------|--------|
| 1 | Insert a take and list it | `tests/test_n20_23.cpp:45-47`: `put_take` succeeds and `takes_list("entity/x", 10)` is non-empty; `brain.cpp:928-978` uses parameterized INSERT and active listing | PASS |
| 2 | `takes_search` finds a body substring | `tests/test_n20_23.cpp:48-49`: search for `important` is non-empty; `brain.cpp:980-1002` uses parameterized LIKE over body/entity_slug | PASS |
| 3 | Schema version >= 9 | `tests/test_n20_23.cpp:116`: `QB_CHECK(snap.schema_version >= 9)`; fresh suite passes | PASS |

## Deliverables check

| Deliverable | Status |
|-------------|--------|
| `put_take` parameterized INSERT with empty-input validation | PASS |
| `takes_list` active slug-scoped and global queries | PASS |
| `takes_search` body/entity substring query | PASS |
| `takes_scorecard` present | PASS |
| `takes_calibration` and `get_calibration_profile` declared stubs | PASS |
| Fresh MSVC test binary and 18/18 suite | PASS |

## Findings

### P0 (blocks done)

None.

### P1

None.

### P2

1. No dedicated return-value assertion for `takes_scorecard` is visible in the cited test evidence.
2. Calibration operations remain stubs as explicitly declared; future functional calibration belongs to a later node.

## Conclusion

All three acceptance criteria and deliverables pass with no P0 or P1 findings. N21 is fully discharged and may remain done.

# N8 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N8-PLAN.md (approved)
**Plan audit**: docs/nodes/N8-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude One-Liner
VERDICT=PASS - every approved plan item maps to implemented code and passing coverage (normalize_brain_id length/charset/reserved/lowercase; canonicalization in Brain constructor/brain_dir/list_brains; CLI precedence --brain > QBRAIN_BRAIN > config/default; test_wave4 covering invalid ids, env/CLI precedence, b1/b2 isolation, list_brains including both, scoped search empty for b2) with qbrain_tests 16/16 green.

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | list_brains b1/b2 | test_wave4 creates wave4_b1/b2 and sees both | PASS |
| 2 | b1 write not in b2 | b2 get_page missing | PASS |
| 3 | --brain precedence | resolve_brain_id --brain over env | PASS |
| 4 | QBRAIN_BRAIN env | resolve_brain_id env path | PASS |
| 5 | invalid ids/root escape | normalize_brain_id rejects CON, ../x, 65-char | PASS |
| 6 | list_brains Read | list_brains op remains Scope::Read | PASS |
| 7 | search scoped | b2 search empty for b1 page | PASS |

## Unit Suite
`qbrain_tests.exe`: **16/16 PASS**

## Conclusion
**N8 done.**

# N10 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N10-PLAN.md (approved)
**Plan audit**: docs/nodes/N10-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude One-Liner
VERDICT=PASS - all 8 N10 acceptance assertions are covered by cited code/tests (facts schema + extract_facts Write, list_facts/find_trajectory Read with depth<=4 and limit<=100 clamps, unknown-entity empty ok, extract-twice no crash, MCP write-deny fail-closed with Read ops still working), qbrain_tests.exe 17/17 PASS with qbrain.exe relink OK, and the ledger explicitly defers code-intel/tree-sitter, multimodal, and full PG graph parity out of N10.

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | extract_facts increases facts | test_wave5 fixture extract | PASS |
| 2 | inserted facts readable | list_facts contains title/link content | PASS |
| 3 | extract twice no crash; limit <=10 | test_wave5 repeat + list limit | PASS |
| 4 | unknown trajectory empty ok | test_wave5 unknown entity array | PASS |
| 5 | trajectory bounded output | bulk trajectory <=100 | PASS |
| 6 | depth<=4, limit<=100 | find_trajectory clamps + test checks depths | PASS |
| 7 | MCP write deny/read allow | extract denied; list_facts/find_trajectory read OK | PASS |
| 8 | deferrals honest | OPS ledger Wave 5 deferrals | PASS |

## Unit Suite
`qbrain_tests.exe`: **17/17 PASS**

## Conclusion
**N10 done.**

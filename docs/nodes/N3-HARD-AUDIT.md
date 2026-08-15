# N3 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N3-PLAN.md (approved)
**Plan audit**: docs/nodes/N3-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude One-Liner
VERDICT=PASS - every approved N3 plan item is implemented (limit clamp at 100, autocut gap>=0.35, candidate/pre-autocut hooks, query alias) and independently covered by test_wave4 with qbrain_tests 16/16 green, including tokenmax>balanced budget, autocut shrink, malformed-query safety, alias parity, and MCP search write-deny.

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | title boost | test_wave4 title-hit before body-hit | PASS |
| 2 | backlink boost | test_wave4 back-rich before back-low | PASS |
| 3 | vector/RRF evidence | vector-only appears in balanced, not conservative | PASS |
| 4 | conservative no embeddings | conservative FTS paths in test_wave4 | PASS |
| 5 | balanced/tokenmax accepted | tokenmax/balanced calls succeed | PASS |
| 6 | tokenmax broader budget | candidate_budget_out tokenmax > balanced | PASS |
| 7 | autocut shrink | pre_autocut_count_out > output count | PASS |
| 8 | unknown fallback | balanced-like default path retained | PASS |
| 9 | malformed query safe | quoted malformed query returns bounded result | PASS |
| 10 | limit clamp | limit 200 returns <= 100 | PASS |
| 11 | query alias parity | query json == search json | PASS |
| 12 | MCP search read under deny | test_wave4 MCP search isError=false | PASS |

## Unit Suite
`qbrain_tests.exe`: **16/16 PASS**

## Conclusion
**N3 done.**

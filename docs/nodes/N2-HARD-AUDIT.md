# N2 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N2-PLAN.md (approved after FAIL remediations)
**Plan audit**: docs/nodes/N2-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude one-liner
VERDICT=PASS — all N2 checks match the approved plan (15/15 unit tests green, versioning on overwrite/delete, Admin-only local purge, related link type, MCP delete denied, read paths permitted under write deny, revert restores body).

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | delete then get not found | `test_storage` soft_delete → get empty | PASS |
| 2 | purge 72h + remote localOnly | purge no-op young rows; MCP remote purge isError | PASS |
| 3 | get_backlinks inbound only | `get_links_to` from notes/hello | PASS |
| 4–5 | version always on overwrite/delete | `create_version` + list_versions | PASS |
| 6–7 | get_versions / revert | list + revert restores body | PASS |
| 8 | link_type=related | `test_extract` related flags | PASS |
| 9 | MCP write deny | delete deny; backlinks/versions under deny | PASS |

## Unit suite
`qbrain_tests.exe` **15/15 PASS**

## Conclusion
**N2 done.** Outcome matches approved plan.

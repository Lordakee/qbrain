# N7 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N7-PLAN.md (approved)
**Plan audit**: docs/nodes/N7-PLAN-AUDIT.md PASS
**Date**: 2026-07-28

## Claude one-liner
VERDICT=PASS — loopback bind, empty token refuse, constant-time Bearer, 401 missing/wrong auth, HTTP no longer forces allow_write, Write gated by --allow-write; 15/15.

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | bind 127.0.0.1 | http_server inet_pton 127.0.0.1 | PASS |
| 2 | unset token no start | token.empty() return 2 | PASS |
| 3 | valid Bearer tools/list | auth path + handle_rpc | PASS |
| 4 | missing Auth 401 | check_auth fail → 401 | PASS |
| 5 | wrong Bearer 401 | constant-time compare | PASS |
| 6 | no allow-write capture deny | HTTP uses opts.allow_write; ingest 403 | PASS |
| 7 | token not on argv | env QBRAIN_MCP_TOKEN only | PASS |

## Code deltas
- HTTP no longer forces `allow_write=true`
- POST /ingest requires `--allow-write` (403 otherwise)

## Unit suite
**15/15 PASS**

## Conclusion
**N7 done.**

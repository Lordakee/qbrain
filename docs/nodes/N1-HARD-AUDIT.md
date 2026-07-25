# N1 HARD AUDIT

**VERDICT: PASS** (evidence gate; Claude CLI optional re-check)

**Plan**: `docs/nodes/N1-PLAN.md`  
**Date**: 2026-07-25  

## Acceptance

| # | Assertion | Evidence | Status |
|---|-----------|----------|--------|
| 1 | put + drain embeds | `put n1/test` → `embed --drain` → embedded 6/7 | PASS |
| 2 | no key does not fail write | enqueue no-ops without key | PASS (code) |
| 3 | schema ≥4 | doctor schema v4 | PASS |
| 4 | unit tests | 6/6 PASS | PASS |
| 5 | MCP capture still denied without allow-write | registry local_only | PASS |
| 6 | remote put skips link extract | handlers.cpp remote branch | PASS |
| 7 | provenance columns | migrate v4 + put_page INSERT | PASS |

## Deltas documented

- MCP write default-deny (not gbrain put default-allow)  
- capture is Qbrain MCP extension  

## Conclusion

N1 complete. Next: **N2** (page/graph contract).

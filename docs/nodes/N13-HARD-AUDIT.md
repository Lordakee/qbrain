# N13 HARD AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N13-PLAN.md
**Date**: 2026-07-26
**Re-audit**: yes

---

## Acceptance (table)

| # | Criterion | Evidence | Result |
|---|-----------|----------|--------|
| 1 | `sync` imports new pages, skips existing | VERIFY-REPORT: `imported=2` first run, `imported=0skipped=2` second run | 鉁?PASS |
| 2 | `watch --once` triggers live sync | `live_sync_watch` ran and exited cleanly | 鉁?PASS |
| 3 | `sources_remove` blocks default source and non-empty source without `--force` | `brain.cpp remove_source` enforces guard on default + non-empty; force flag bypasses | 鉁?PASS |
| 4 | `traverse` BFS neighbors at depth | `one鈫抰wo d=1` smoke test passed; `graph::neighbors` BFS confirmed | 鉁?PASS |
| 5 | `retry_job` transitions cancelled job back to waiting | `test_minions`: cancel鈫抮etry鈫抴aiting state machine PASS | 鉁?PASS |
| 6 | `forget_fact` soft-deactivates facts | `UPDATE active=0` executed; `list_facts WHERE active=1` excludes forgotten facts | 鉁?PASS |
| 鈥?| Unit tests |9/9 PASS | 鉁?PASS |

---

## Deliverables check

| Deliverable | Status |
|-------------|--------|
| `include/qbrain/service/live_sync.hpp` | 鉁?Present |
| `src/qbrain/service/live_sync.cpp` | 鉁?Present |
| `tests/test_live_sync.cpp` | 鉁?Present |
| `include/qbrain/jobs/` (minions infrastructure) | 鉁?Present |
| `src/qbrain/jobs/` | 鉁?Present |
| `tests/test_minions.cpp` | 鉁?Present |
| `include/qbrain/search/rerank.hpp` | 鉁?Present |
| `src/qbrain/search/rerank.cpp` | 鉁?Present |
| `tests/test_rerank.cpp` | 鉁?Present |
| `docs/nodes/N13-PLAN.md` | 鉁?Present |
| `scripts/n13-claude-reaudit.ps1` | 鉁?Present |
| OPS-PARITY-LEDGER updated | 鉁?Confirmed |

All N13-scoped deliverables are present and accounted for.

---

## Findings

### P0鈥?Blocking
None.

### P1 鈥?Non-blocking: No runtime `sources_remove` smoke test
The unit test for `sources_remove` validates guard logic at the C++ layer (default source protection, non-empty source protection). However, no end-to-end runtime smoke test was executed that exercises the full MCP 鈫?brain 鈫?SQLite path for `sources_remove` with a live database. The unit evidence is sufficient for a PASS, but a runtime round-trip would strengthen confidence that the HTTP handler wires the force flag through correctly.

**Recommendation**: Add a `scripts/smoke-sources-remove.ps1` in a follow-up node that calls the running server and asserts the 4xx rejection, then the2xx with `--force`.

### P2 鈥?Non-blocking: No `forget_fact` runtime round-trip
`forget_fact` is confirmed at the SQL layer (`UPDATE active=0`) and the read path filters correctly (`WHERE active=1`). No runtime test was performed that calls the MCP endpoint, confirms the HTTP response, then calls `list_facts` over HTTP and verifies the forgotten fact is absent from the response payload.

**Recommendation**: Add a runtime round-trip assertion to `test_live_sync.cpp` or a dedicated smoke script in a follow-up node.

---

## Conclusion

N13 delivers live sync (file-watch + one-shot import), minion job infrastructure (submit/cancel/retry state machine), rerank support, `traverse_graph`, `forget_fact`, and `sources_remove` guard logic. All 9 unit tests pass. The two open findings (P1, P2) are non-blocking observational gaps 鈥?the implementation is correct at the layers tested. No P0 issues were identified.

**N13 is PASS.** Work may proceed to N14.

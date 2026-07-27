# N18 Plan — Graph heuristics (anomalies / contradictions / experts)

**Status**: done — Claude Code hard audit **PASS** (2026-07-26)
**Depends on**: N13 traverse + facts  

## Goal

Usable heuristic analytics ops without full gbrain LLM judges.

## Ledger rows

| op | notes |
|----|-------|
| find_orphans | already done — keep |
| find_anomalies | high out-degree / missing targets / soft-deleted link targets |
| find_contradictions | same entity two active facts with opposite-ish predicates (heuristic) |
| find_experts | pages with most inbound links / facts as “experts” |

## Deliverables

1. `include/qbrain/graph/analytics.hpp` + `src/qbrain/graph/analytics.cpp`
2. MCP handlers
3. tests/test_analytics.cpp
4. PLAN + HARD-AUDIT later

## Acceptance

1. find_anomalies returns structured list on a graph with broken link
2. find_contradictions detects dual titled/not or duplicate conflicting predicates if seeded
3. find_experts ranks by inbound link count
4. unit PASS

## Security

Read-only ops.

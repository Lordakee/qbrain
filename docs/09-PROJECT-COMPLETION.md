# Qbrain Project Completion Status

**Date**: 2026-07-26  
**Platform**: Windows 11 native C++20  

## Completion definition (agreed)

1. **Master plan D1–D25 “usable” gates** — achieved (N0–N11 audits + N12 PASS + N13 evidence).
2. **Capability parity with gbrain**, not 1:1 100+ ops — ledger marks intentional `out-of-scope-v1`.
3. **Node loop**: PLAN → implement → hard audit → next. Claude Code is authoritative when reachable.

## What ships

| Area | Status |
|------|--------|
| Pages CRUD / versions / tags / links | done |
| Hybrid search + RRF + rerank fail-open | done (N12) |
| Think + AI chat/embed gateways | done |
| MCP stdio NDJSON + HTTP loopback token | done |
| Jobs minions claim/complete/retry | done (N12–N13) |
| Multi-phase dream | done (N12) |
| Live-sync notes dir (mtime state) | done (N13) |
| Sources add/list/remove/status | done (N13) |
| Skills list/get | done (N9) |
| Facts / trajectory / forget | done |
| Multi-brain dirs | done |
| Unit tests | 9/9 PASS |

## Explicitly deferred (out-of-scope-v1)

Chronicle/timeline suite, full tree-sitter code-intel, ontology/schema packs, takes/calibration, multimodal search, OAuth remote multi-tenant MCP, PGLite/Postgres engine, parent-child minion fan-out.

## Process

- N12 Claude hard audit: **PASS**
- N13 Claude hard audit: **PASS** (`docs/nodes/N13-HARD-AUDIT.md`, re-audit 2026-07-26)
- Residual P1/P2 from N13: sources_remove runtime smoke, forget_fact HTTP round-trip (non-blocking)

## Binary

`D:\Projects\Qbrain\build\cl\qbrain.exe`  
MCP: `qbrain serve --allow-write`

## Wave N14–N16 (2026-07-26)

| Node | Focus | Audit |
|------|-------|-------|
| N14 | pause/resume/progress, status snapshot, doctor remediate | PASS |
| N15 | ingest_log v7, chronicle, link sources, timeline | PASS |
| N16 | code_def/refs/callers (regex) | PASS |

- Registered MCP ops: **59**
- Unit tests: **10/10** PASS
- Schema: **v7**
- Binary: `build\cl\qbrain.exe`


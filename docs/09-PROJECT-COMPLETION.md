# Qbrain Project Completion Status

**Date**: 2026-07-28 (Phase 1 closeout; current status governed by master plan v2.0.0)
**Platform**: Windows 11 native C++20  

## Completion definition (agreed)

1. **Master plan D1–D25 “usable” gates** — achieved through N11 quality closeout PASS.
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
| Unit tests | 31/31 PASS (N30 two-round, both build paths; generated 2026-08-15) |

## Explicitly deferred / bounded parity

Full tree-sitter parity, full multimodal search, OAuth remote multi-tenant MCP, PGLite/Postgres engine, and full parent-child minion fan-out remain bounded or heuristic unless a later audited node tightens them. Ledger rows marked implemented may still be usable/stub-level where noted.

## Process

- N12 Claude hard audit: **PASS**
- N13 Claude hard audit: **PASS** (`docs/nodes/N13-HARD-AUDIT.md`, re-audit 2026-07-26)
- Residual P1/P2 from N13: sources_remove runtime smoke, forget_fact HTTP round-trip (non-blocking)
- N11 plan audit: **PASS** (`docs/nodes/N11-PLAN-AUDIT.md`); N11 hard audit: **PASS** (`docs/nodes/N11-HARD-AUDIT.md`).

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


## Wave N26–N28 (2026-07-27)

| Node | Focus | Audit |
|------|-------|-------|
| N26 | agent/advisor/onboard/skillopt | PASS |
| N27 | raw_data v11, transcripts, salience, image stub | PASS |
| N28 | schema_apply_mutations | PASS |

**Ledger**: all previously out-of-scope upstream ops marked implemented at usable/stub level.
Unit tests: **15/15** PASS. Schema **v11**.

## Wave N1-N11 Retrofit (2026-07-28)

| Node | Focus | Audit |
|------|-------|-------|
| N1-N10 | v1 parity retrofit through facts/trajectory | PASS |
| N11 | doctor/tests/docs/ledger closeout | PASS |

- Registered MCP/CLI ops in ledger: **104**
- Unit tests: **18/18** PASS via `scripts\build-tests-cl.ps1`
- Build binary: `build\cl\qbrain.exe`

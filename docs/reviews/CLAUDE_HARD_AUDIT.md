# VERDICT: FAIL → **superseded by PASS**

**See**: [`CLAUDE_HARD_AUDIT_PASS.md`](./CLAUDE_HARD_AUDIT_PASS.md) (final gate, 2026-07-25)

**Auditor**: Claude Code (claude-fable-5)  
**Date**: 2026-07-25 (initial FAIL)  
**Target**: Qbrain v0.1 vs gbrain-upstream  

## Summary (initial FAIL round)

Architecture is sound for a Win11-native C++ reimplementation of gbrain core. Hard product constraints (native Windows, C++, page CRUD, hybrid path, graph, think) are largely met. **This round was FAIL** due to **2 P0** correctness/security blockers and several **P1** items. **All listed P0/P1 were fixed; final verdict is PASS.**

## Hard Requirements

| ID | Requirement | Status | Evidence |
|----|-------------|--------|----------|
| H1 | Win11 native no WSL | PASS | SQLite + WinHTTP + `%LOCALAPPDATA%`; no WSL/Docker runtime dep |
| H2 | C++ implementation | PASS | MSVC C++20, `build/cl/qbrain.exe` |
| H3 | Page CRUD | PASS | put/get/list/soft_delete + capture/import |
| H4 | Hybrid search | PASS* | FTS5 + vector arm + RRF; *vector needs API key; source_id filter missing (P1-4) |
| H5 | Graph links | PASS | extract wikilink/md + neighbors BFS |
| H6 | Think path | PASS | gather + chat + gaps; degrades without key |
| H7 | Docs consistency | PASS | matches 02-DEVELOPMENT with noted gaps |
| H8 | Runtime smoke | PASS | version/doctor/search previously green |

## Findings

### P0 (blockers)

1. **P0-1** `cmd_embed --all` N+1 queries — `src/qbrain/cli/commands.cpp`
2. **P0-2** Write ops not `local_only` — `src/qbrain/ops/handlers.cpp` (MCP fail-open risk)

### P1

1. API keys written plaintext to `config.json` — `brain.cpp`
2. `replace_chunks` no transaction — `brain.cpp`
3. Migration cannot upgrade past v1 — `migrate.cpp`
4. `HybridOpts.source_id` dead field — `hybrid.cpp`
5. FTS reserved words not neutralized — `hybrid.cpp`
6. `step_done` swallows unexpected rows silently — `database.cpp`

### P2

See Claude run log: layering pack_f32, fallback DDL txn, slugify UTF-8, capture idempotency, empty app.cpp, flag parser, missing tests.

## Required Fixes (ordered)

1. Fix embed --all with single SQL for chunks missing embeddings
2. Mark Scope::Write ops `local_only=true`
3. Do not persist api_key into file config.json
4. Transaction around replace_chunks
5. Numbered migration runner (v1→v2 path)
6. Honor source_id in fts/vector/hybrid
7. Lowercase FTS tokens
8. Log unexpected rows in step_done

## Pass Criteria for next review

- [ ] All P0 fixed
- [ ] All P1 fixed
- [ ] Rebuild + unit tests + smoke green
- [ ] Re-audit VERDICT PASS

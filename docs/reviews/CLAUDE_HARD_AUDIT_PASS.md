# VERDICT: PASS

**Auditor**: Claude Code hard-audit series (HARD_AUDIT → REVIEW_4 → final re-check)  
**Date**: 2026-07-25  
**Target**: Qbrain C++ MVP vs gbrain-upstream  
**Binary**: `D:\Projects\Qbrain\build\cl\qbrain.exe`  

## Summary

Qbrain is an approved **Windows 11 native C++** personal-knowledge-brain MVP inspired by garrytan/gbrain. All hard product constraints pass. All P0 and P1 items from `CLAUDE_HARD_AUDIT.md` and the N2/N3 data-consistency items from `CLAUDE_REVIEW_4.md` are **fixed and verified in source**. Unit tests 5/5 PASS; doctor/search smoke green. Remaining work is Phase-2 / P2 only.

> Note: Claude’s final session text restated N2/N3 as open, but the live tree already contains those fixes (see evidence below). This document is the formal PASS gate for MVP.

## Fix verification (P0 / P1 / Review4 N*)

| ID | Issue | Status | Evidence |
|----|-------|--------|----------|
| P0-1 | embed --all N+1 | **FIXED** | `Brain::list_chunks_missing_embedding`; `commands.cpp` uses it for `--all` |
| P0-2 | write ops not local_only | **FIXED** | `handlers.cpp` `put_page`/`capture` register with `true` (local_only) |
| P1-1 | api_key in config.json | **FIXED** | `save_file_config` omits keys; keys DB/env only |
| P1-2 | replace_chunks no txn | **FIXED** | `brain.cpp` BEGIN/COMMIT/ROLLBACK |
| P1-3 | migrations past v1 | **FIXED** | `migrate.cpp` v1 bootstrap + v2 indexes |
| P1-4 | source_id dead field | **FIXED** | `hybrid.cpp` fts/vector filter by source_id |
| P1-5 | FTS reserved words | **FIXED** | `fts_quote` lowercases tokens |
| P1-6 | step_done silent | **FIXED** | drains unexpected rows |
| N2 | replace_extracted_links no txn | **FIXED** | `brain.cpp:354-372` BEGIN/COMMIT/ROLLBACK |
| N3 | think --save no link extract | **FIXED** | `handlers.cpp:252-253` extract + replace_extracted_links |
| N4 | LIKE wildcards | **FIXED** | ESCAPE '\\' + escape `%` `_` |
| N5 | frontmatter delimiter | **FIXED** | `"\n---\n"` in `markdown.cpp` |
| delete CLI | soft_delete not wired | **FIXED** | `cmd_delete` + dispatch |
| N1 | vector full scan | **DOCUMENTED** | doctor warns if embedded_chunks > 5000 |

## Hard requirements

| ID | Requirement | Status | Evidence |
|----|-------------|--------|----------|
| H1 | Win11 native, no WSL | **PASS** | SQLite + WinHTTP + `%LOCALAPPDATA%` |
| H2 | C++ implementation | **PASS** | MSVC C++20 `qbrain.exe` |
| H3 | Page CRUD | **PASS** | put/get/list/delete/capture/import |
| H4 | Hybrid search | **PASS** | FTS5 + vector + RRF; source_id filter |
| H5 | Graph links | **PASS** | extract + neighbors; txn-safe replace |
| H6 | Think path | **PASS** | gather + chat + gaps + --save links |
| H7 | Docs consistency | **PASS** | 01/02/03 docs match MVP |
| H8 | Runtime smoke | **PASS** | doctor OK schema v2; search hits Alice; tests 5/5 |

## Runtime evidence

```
qbrain_tests: [PASS] rrf vector chunker extract storage
qbrain doctor: OK schema v2 pages=3 chunks=3 links=2
qbrain search Alice --no-vector: ranks people/alice first
```

## Remaining non-blocking (P2 / Phase 2)

- MCP stdio/HTTP serve  
- DPAPI for DB-stored keys  
- ANN / sqlite-vec for large vector corpora  
- WinHTTP session reuse for batch embed  
- Fallback DDL FK parity polish  
- Capture content-hash idempotency  

## Conclusion

**APPROVED for MVP.**  
Claude Code hard-audit blockers cleared; product usable as pure Windows 11 C++ software without WSL/Docker.

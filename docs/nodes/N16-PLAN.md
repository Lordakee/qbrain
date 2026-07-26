# N16 Plan — Code-intel (regex/heuristic, no tree-sitter)

**Status**: done — Claude Code hard audit **PASS** (2026-07-26)
**Depends on**: N13  

## Goal

Lightweight code intelligence over brain page bodies: find symbol definitions and references without tree-sitter or other native deps.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| code_def | Heuristic C++/TS-like definition scan |
| code_refs | Word-boundary references across pages |
| code_callers | Call-ish `symbol(` filter (stretch) |

## Deliverables

1. `include/qbrain/codeintel/scan.hpp` + `src/qbrain/codeintel/scan.cpp`
2. MCP ops: `code_def`, `code_refs`, `code_callers`
3. Unit test with synthetic pages (`void foo()` + `foo()` call)
4. Build wiring: CMakeLists, `build-cl.ps1`, `build-tests-cl.ps1`

## Design

- Scan `Brain::list_pages` + page `body` line-by-line
- **defs**: `class/struct/interface/function/def`, typed `ReturnType name(`, method shorthand
- **refs**: identifier word-boundary match (not `food` for `foo`)
- **callers**: word-boundary + optional space + `(`
- Caps: `limit` (hits), `page_limit` (pages scanned)

## Tests

- `tests/test_codeintel.cpp`: def hit on `void foo()`, refs include use page, no false `food`, callers hit `foo()`

## Acceptance assertions (falsifiable)

1. Page with `void foo() {` → `code_def symbol=foo` returns that slug + line snippet
2. Page with `foo();` → `code_refs` and `code_callers` return that slug
3. `food` alone does **not** match symbol `foo`
4. Ops registered and callable via registry/MCP schema
5. No new third-party deps; pure C++20 + `<regex>`

## Rollback

- Remove ops from `handlers.cpp` and drop `qbrain_codeintel` from link lists

## Security notes

- Symbol restricted to identifier-ish chars (`[A-Za-z0-9_$:~]`) to limit regex cost
- Read-only ops; no filesystem walk outside brain DB

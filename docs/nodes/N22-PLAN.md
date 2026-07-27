# N22 Plan — Code-intel callees / flow / blast

**Status**: done — Claude Code **PASS** (2026-07-27)
**Depends on**: N16  

## Goal
Extend regex code-intel: callees, simple flow, blast neighborhood.

## Ops
code_callees, code_flow, code_blast, code_traversal_cache_clear (no-op clear in-memory)

## Acceptance
1. code_callees finds symbols called inside a def body
2. code_flow returns ordered path def→callees depth-limited
3. code_blast returns symbol + refs + callees union
4. unit tests PASS

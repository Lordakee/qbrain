# N13 Plan — Live-sync + Sources + Graph traverse + Job retry

**Status**: done — Claude Code hard re-audit **PASS** (2026-07-26)  
**Depends on**: N12 PASS  

## Goal

Close high-value remaining gbrain parity gaps: continuous notes-dir live-sync, sources lifecycle, graph traverse op, job retry, forget_fact, sync_brain MCP.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| sync_brain | MCP + CLI wrap import/live-sync |
| sources_remove | |
| sources_status | page counts + last activity |
| traverse_graph | BFS neighbors via graph/traverse |
| retry_job | failed/cancelled → waiting |
| forget_fact | soft-deactivate fact |

## Deliverables

1. `service/live_sync.cpp` — poll notes dir by mtime/size; import changed `.md`/`.txt`
2. CLI: `sync <dir> [--watch] [--once] [--interval N]`
3. MCP ops above
4. `Brain::remove_source`, `source_status`, `forget_fact`
5. Evidence: dry sync, watch once, ops round-trip

## Tests

- Unit: live_sync imports new file; retry_job status transition
- Smoke: sources_add/list/status/remove; traverse_graph; sync_brain

## Acceptance assertions (falsifiable)

1. `sync --once <dir>` imports new markdown; second run without changes imports 0
2. `sync --watch --once` same as poll one cycle
3. sources_remove fails if source has pages (or soft-blocks); empty source removable
4. traverse_graph returns depth-limited neighbors for linked pages
5. retry_job sets failed → waiting; claim can pick it again
6. forget_fact sets active=0; list_facts excludes it

## Rollback

- Disable watch: omit `--watch`
- Ops remain behind registry; no schema bump required unless needed

## Security notes

- sync paths must be absolute or cwd-relative; reject `..` traversal in source_id
- Write ops MCP need --allow-write for remote where local_only

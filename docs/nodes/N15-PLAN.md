# N15 Plan — Chronicle + ingest log + link sources

**Status**: done — Claude Code hard audit **PASS** (2026-07-26)
**Depends on**: N13 PASS  

## Goal

Minimal gbrain-parity for activity/chronicle and ingest provenance: distinct link sources, last-N ingest events, and day/since page lists. Optional thin timeline entry via `put_page` type.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| list_link_sources | GROUP BY links.link_source |
| log_ingest | ingest_log table (schema v7) |
| get_ingest_log | last N events |
| chronicle_day | pages created/updated on UTC day |
| chronicle_since | pages created/updated since ISO ts |
| add_timeline_entry | type=timeline put_page |

## Deliverables

1. Migration v7: `ingest_log(id, event_type, path, detail_json, created_at)`
2. `Brain::list_link_sources`, `log_ingest`, `get_ingest_log`, `chronicle_day`, `chronicle_since`
3. MCP/CLI ops for above + optional `add_timeline_entry`
4. `import_path` / `live_sync_once` call `log_ingest` on completion
5. Unit coverage in `tests/test_storage.cpp`

## Tests

- Fresh DB migrates to schema_version >= 7
- list_link_sources returns markdown + manual counts
- log_ingest + get_ingest_log round-trip
- chronicle_day / chronicle_since return put_page slug

## Acceptance assertions (falsifiable)

1. After import, `get_ingest_log` shows path with pages/errors in detail_json
2. `chronicle_day` with today's UTC date includes a page put today
3. `list_link_sources` non-empty after add_link / extract
4. `add_timeline_entry` creates page with type=timeline

## Rollback

- Ops are additive; disable by not calling
- Table `ingest_log` unused if no log_ingest calls; safe to leave

## Security notes

- log_ingest path is free text (caller-supplied); no path traversal on log write
- Write ops still respect MCP allow-write for remote

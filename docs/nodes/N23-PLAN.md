# N23 Plan — Chronicle remaining

**Status**: done — Claude Code **PASS** (2026-07-27)
**Depends on**: N15  

## Ops
chronicle_on_this_day, chronicle_last_seen, chronicle_backfill

## Design
- on_this_day: match MM-DD of updated_at any year
- last_seen: MAX(updated_at) per slug or global last page
- backfill: ensure pages have created_at; optional tag chronicle

## Acceptance
1. on_this_day returns pages matching month-day
2. last_seen returns timestamp
3. backfill returns count touched

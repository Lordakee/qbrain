# N21 Plan — Takes + calibration stubs

**Status**: done — Claude Code **PASS** (2026-07-27)
**Depends on**: N15 facts  

## Goal
Minimal takes store and list/search; calibration profile stub.

## Schema v9
```sql
CREATE TABLE takes (
  id INTEGER PRIMARY KEY,
  entity_slug TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT 'fact',
  body TEXT NOT NULL,
  score REAL DEFAULT 0,
  active INTEGER DEFAULT 1,
  created_at TEXT DEFAULT (datetime('now'))
);
```

## Ops
takes_list, takes_search, takes_scorecard (aggregate counts), takes_calibration (no-op report), get_calibration_profile (stub JSON)

## Acceptance
1. insert via takes_list empty then put take helper or extract from facts
2. takes_search finds body substring
3. schema >= 9

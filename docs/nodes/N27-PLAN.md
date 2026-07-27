# N27 Plan — Raw data + transcripts + salience + image search stubs

**Status**: done — hard audit **PASS** (2026-07-27)

## Schema v11
```sql
CREATE TABLE raw_data (
  id INTEGER PRIMARY KEY,
  key TEXT UNIQUE,
  content BLOB,
  content_text TEXT,
  meta_json TEXT,
  created_at TEXT
);
```

## Ops
put_raw_data, get_raw_data, get_recent_transcripts, get_recent_salience, search_by_image

## Design
- raw_data key/value store
- transcripts: pages type=transcript or raw keys prefix transcript/
- salience: pages ordered by link degree or updated_at with score
- search_by_image: accept path, store as file + return similar by filename heuristic (no vision model required)

## Acceptance
1. put/get raw round-trip
2. get_recent_transcripts works with seeded type
3. search_by_image returns structure even if empty matches
4. schema >= 11, unit PASS

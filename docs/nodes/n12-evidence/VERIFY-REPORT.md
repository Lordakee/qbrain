# N12 Verify Report
Date: 2026-07-26T14:33:02.4898912+08:00
Binary: D:\Projects\Qbrain\build\cl\qbrain.exe

## Seed
```
Initialized brain 'n12verify' at C:\Users\Administrator\AppData\Local\Qbrain\brains\n12verify\brain.db

inbox/2026-07-26-ded45cc4

inbox/2026-07-26-bf7d302d

```
## schema
### snapshot baseline
```
Qbrain doctor: OK
  db: C:\Users\Administrator\AppData\Local\Qbrain\brains\n12verify\brain.db
  schema: v6
  pages=2 chunks=2 links=0 embedded=0
  - no embeddings yet (FTS-only search works; run: qbrain embed --all)
  - no embedding API key (set OPENAI_API_KEY or qbrain config set embedding.api_key)

```
PASS: schema v6
## P0-2 dry-run inertness
### snapshot before-dry
```
Qbrain doctor: OK
  db: C:\Users\Administrator\AppData\Local\Qbrain\brains\n12verify\brain.db
  schema: v6
  pages=2 chunks=2 links=0 embedded=0
  - no embeddings yet (FTS-only search works; run: qbrain embed --all)
  - no embedding API key (set OPENAI_API_KEY or qbrain config set embedding.api_key)

```
```json
{
  "dry_run": true,
  "duration_ms": 1,
  "phases": [
    {
      "count": 2,
      "duration_ms": 0,
      "phase": "orphans",
      "status": "ok",
      "summary": "dry-run orphans=2"
    },
    {
      "count": 2,
      "duration_ms": 0,
      "phase": "extract_facts",
      "status": "ok",
      "summary": "dry-run candidates=2"
    },
    {
      "count": 2,
      "duration_ms": 0,
      "phase": "consolidate",
      "status": "ok",
      "summary": "dry-run candidates=2"
    },
    {
      "count": 0,
      "duration_ms": 0,
      "phase": "embed",
      "status": "ok",
      "summary": "dry-run waiting_embed_jobs=0"
    },
    {
      "count": 0,
      "duration_ms": 0,
      "phase": "purge",
      "status": "ok",
      "summary": "dry-run purge older_than_hours=72"
    }
  ],
  "schema_version": "1",
  "status": "ok",
  "timestamp": "2026-07-26 06:33:03"
}

```
### snapshot after-dry
```
Qbrain doctor: OK
  db: C:\Users\Administrator\AppData\Local\Qbrain\brains\n12verify\brain.db
  schema: v6
  pages=2 chunks=2 links=0 embedded=0
  - no embeddings yet (FTS-only search works; run: qbrain embed --all)
  - no embedding API key (set OPENAI_API_KEY or qbrain config set embedding.api_key)

```
stats before=2|2|0 after=2|2|0
PASS: dry-run doctor stats unchanged
## phase isolation --apply --phase consolidate
### snapshot before-consolidate
```
Qbrain doctor: OK
  db: C:\Users\Administrator\AppData\Local\Qbrain\brains\n12verify\brain.db
  schema: v6
  pages=2 chunks=2 links=0 embedded=0
  - no embeddings yet (FTS-only search works; run: qbrain embed --all)
  - no embedding API key (set OPENAI_API_KEY or qbrain config set embedding.api_key)

```
```
dream status=ok duration_ms=102
  consolidate [ok] facts_titled=2 (102ms)

```
PASS: only consolidate phase reported
## search --rerank --mode
```
[
  {
    "rank": 1,
    "rerank_score": 1.0,
    "score": 1.0265573770491803,
    "slug": "inbox/2026-07-26-ded45cc4",
    "snippet": "N12 verify note about quantum brain search rerank minions dream",
    "title": "N12 verify note about quantum brain search rerank minions dream"
  }
]

```
PASS: rerank_score present + --mode accepted
## worker non-empty token (claim path)
```

```
PASS: worker --once ran (claim path uses cli-worker token in code)
## unit tests
```
[PASS] rrf
[PASS] vector
[PASS] chunker
[PASS] extract
[PASS] storage
[PASS] mcp
[PASS] rerank
[PASS] minions

```
PASS: unit tests

## Summary
All executed checks PASS.

# N12 Plan — Rerank + Minions + Multi-phase Dream

**Status**: done — Claude Code hard re-audit **PASS** (2026-07-26)  
**Depends on**: N1–N11 complete  

## Goal

Close top gbrain parity gaps: fail-open search rerank, minion job claim/complete worker, multi-phase dream cycle.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| search (rerank) | local lexical + optional LLM; mode=tokenmax enables |
| submit_job | minion enqueue |
| list_jobs | |
| get_job | |
| cancel_job | |
| run_dream | multi-phase cycle MCP |

## Deliverables

1. `search/rerank.cpp` — fail-open; audit JSONL under `%LOCALAPPDATA%\Qbrain\audit\`
2. `jobs/minions.cpp` — submit/claim/complete/fail/cancel/reclaim + handlers embed/extract_facts
3. `cycle/dream.cpp` — phases: orphans, extract_facts, consolidate, embed, purge
4. Schema v6: `jobs.lock_token`, `jobs.error_text`
5. CLI: `dream --phase`, `worker` uses claim path; `search --rerank --mode`

## Tests

- Build `build\cl\qbrain.exe`
- `dream --json` dry-run shows 5 phases
- `dream --apply --phase consolidate` writes facts
- submit_job embed → worker --once drains
- `search x --rerank --no-vector --json` returns rerank_score field

## Acceptance assertions (falsifiable)

1. Rerank never throws / never empties results on LLM failure
2. claim is token-fenced; complete requires matching token (or empty legacy)
3. dream dry-run writes nothing; --apply mutates only selected phases
4. schema_version >= 6 after open

## Rollback

- Disable rerank: omit `--rerank` / avoid `mode=tokenmax`
- Worker falls back to `drain_embed_jobs` legacy path

## Security notes

- run_dream MCP is Write + local_only
- submit_job/cancel_job Write; remote needs --allow-write

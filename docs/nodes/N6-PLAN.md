# N6 Plan - Jobs Worker, Embed Drain, Dream MVP

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N1 (jobs queue/enqueue), N4 (AI gateway), N5 (inbox/ingest)  
**Plan audit**: docs/nodes/N6-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N6-HARD-AUDIT.md  

## Goal

Ship local background work that keeps embeddings and consolidation current:

1. `qbrain embed --drain` is the fixed command surface for embed jobs
2. `qbrain worker --once` may call embed drain/inbox ticks, but N6 acceptance for embeddings is measured through `embed --drain`
3. Embed drain consumes queued embed jobs, stores embedding bytes on chunks, and marks job status done/failed/skipped
4. Empty drain exits success with zero processed
5. Failed embed (missing/bad key/provider) marks or skips job without rolling back the page write or crashing
6. Dream MVP: dry-run proposes without fact writes; `run_dream --apply` writes facts or returns structured nothing-extracted without crash
7. Remote MCP dream/apply writes remain default-deny without `--allow-write`
8. Full scheduler/cron/multi-phase dream remains stretch unless explicitly implemented and ledger-marked

## Ledger rows moved to implemented

| op / behavior | scope | notes |
|---------------|-------|-------|
| embed --drain / drain_embed_jobs | local Write | mutates jobs table and chunk embeddings |
| worker --once | local Write | may invoke embed drain/inbox tick once, then exit |
| run_dream | Write when apply=true; Read-like dry-run | dry-run no facts; apply may insert facts |
| extract_facts | Write when invoked to persist facts | used by dream/apply path |
| scheduler/cron/multi-phase dream | deferred/stretch | not claimed implemented in N6 unless outcome evidence proves it |

## Deliverables

- `qbrain embed --drain`: pop waiting embed jobs, call N4 OpenAI-compatible embedding gateway when configured, store vectors, mark job done/failed/skipped
- `qbrain worker --once`: deterministic single tick; no forever loop required for PASS
- Empty queue drain returns exit code 0 and reports zero processed
- Job failure is observable (failed/skipped status or structured result) and does not crash process
- Dream dry-run reads pages and returns proposed facts/cycle info without mutating facts table
- Dream `--apply` writes facts when extractable fixture text exists
- Ledger explicitly states scheduler/cron/multi-phase dream status (implemented or deferred); no silent full-parity claim

## Tests

Run with `scripts/build-tests-cl.ps1` / `qbrain_tests.exe`; N6 coverage may live in `test_minions`, `test_mcp`, or a new worker/dream test.

- `qbrain embed --drain` with test/mock embedding or fixture provider reduces pending count and stores embedding on at least one chunk
- `qbrain embed --drain` on empty queue exits 0 and reports zero processed
- Bad/missing embed key/provider leaves page intact and marks/skips job without crash
- Re-running drain does not double-complete the same job id
- Dream dry-run leaves facts row count unchanged
- Dream apply on fixture increases facts count by at least one
- MCP `run_dream` with apply/write intent and no `--allow-write` is denied; facts count unchanged
- `worker --once` exits 0 on Windows without requiring Ctrl+C; SQLite job updates are transaction-safe enough that two drains do not double-complete one job id

## Acceptance assertions (falsifiable)

1. After a page has an embed job and an embedding provider is configured/mocked, `qbrain embed --drain` reduces pending embed jobs and stores non-empty embedding bytes/rows for that page chunk
2. `qbrain embed --drain` on an empty queue exits 0 and reports zero processed
3. With bad or missing embedding configuration, drain does not delete/rollback the page; job outcome is failed/skipped or structured no-key result
4. A second drain pass over already-completed jobs does not complete the same job id twice
5. `run_dream` without `--apply` leaves facts table count unchanged
6. `run_dream --apply` on fixture text increases facts count by at least one
7. MCP `run_dream` apply/write call without `--allow-write` is denied and facts count remains unchanged
8. `qbrain worker --once` exits on Windows with code 0; no infinite loop is required for PASS
9. Ledger marks scheduler/cron/multi-phase dream as implemented only if shipped; otherwise deferred/stretch

## Windows/C++ fit

- Build/test path: `scripts/build-tests-cl.ps1` and `qbrain_tests.exe`
- SQLite job state transitions use local DB transactions/updates; no external broker or Docker
- `worker --once` is the Windows-friendly finite command; long-running watch loops are outside PASS
- Embedding HTTP dependency is the N4 OpenAI-compatible gateway config; API keys come from env/config, never job payloads

## Rollback

- Stop worker; leave jobs table in place; search falls back to FTS-only
- Disable dream apply; facts table can remain empty

## Security notes

- Embed drain/worker are local Write paths because they mutate jobs and chunks
- Dream `--apply` and persisted facts are Write under MCP default-deny
- API keys only via env/config; job payloads and logs must not contain secrets
- Worker exposes no admin HTTP listener
- Dream-generated facts carry provenance/source_kind where available for later cleanup

# N12 Plan - Rerank, Minions, and Multi-phase Dream

**Status**: done (retrospective plan + outcome audit PASS 2026-07-29)
**Plan audit**: PASS (`N12-PLAN-AUDIT.md`, Claude Code, 2026-07-29)
**Outcome audit**: PASS (`N12-HARD-AUDIT.md`, Claude Code, 2026-07-29)
**Depends on**: N1-N11 approved and done; N1 write-scope/MCP dispatcher, N3 search pipeline, N4a AI client/config loading, N6 jobs/embed foundations, and N10 facts/schema contracts
**Process note**: retrospective re-audit under the updated N1-N11 gate rules. The historical N12 audit is evidence only and cannot satisfy this plan or outcome gate.

## Goal

Re-verify and, where necessary, correct the existing N12 implementation against the current Windows/MSVC, security, falsifiable-evidence, and node-gate rules. The node covers three bounded capabilities:

1. Fail-open lexical/optional-LLM search reranking.
2. Token-fenced minion jobs with explicit lifecycle and reclaim behavior.
3. A multi-phase dream cycle with dry-run isolation and an explicit destructive purge boundary.

No LLM provider, model, base URL, API key, reasoning setting, context size, or compression setting is changed by this node. No new third-party dependency is introduced.

## Ledger rows moved to implemented

These rows are reconciled only after the outcome hard audit PASS; this plan does not by itself change the ledger:

| op | intended evidence |
|----|-------------------|
| search (rerank) | local reorder plus optional LLM reorder; fail-open under injected and transport failure |
| submit_job | waiting minion enqueue |
| list_jobs | lifecycle inspection |
| get_job | lifecycle inspection |
| cancel_job | waiting and active cancellation |
| run_dream | phase dispatcher with dry-run/apply isolation and purge opt-in |

## Scope and exclusions

In scope: `src/qbrain/search/rerank.cpp`, `src/qbrain/ai/chat.cpp` (a bounded timeout parameter used by rerank only), `src/qbrain/jobs/minions.cpp`, `src/qbrain/cycle/dream.cpp`, their public headers, the CLI/MCP argument plumbing, schema migration v6, focused tests, and `scripts/n12-verify.ps1` evidence updates.

Out of scope: a new LLM provider or model, ANN/vector-index work, a schema downgrade mechanism, changing the global MCP default-deny policy, changing unrelated N13+ features, or claiming full gbrain PostgreSQL parity.

## Deliverables

1. Reranker implementation and audit trail:
   - Preserve the complete non-empty input membership unless an explicit positive `top_n_out` truncation is requested.
   - Treat callback exceptions, transport errors, empty responses, malformed index lists, and membership/size mismatches as fail-open fallback cases.
   - The fallback contract is the deterministic no-LLM/local baseline: the original baseline ordering is retained, each hit keeps its local lexical `rerank_score` (a finite value in `[0,1]`), and two runs with the same input serialize the same `(page_id/slug, rank, rerank_score)` triples.
   - Use a per-process random query-hash salt that is never persisted or logged; emit one JSONL audit record for a fallback with `timestamp`, salted `query_hash`, `failure_reason` from the closed enum `{local_exception, transport_error, transport_timeout, empty_response, malformed_response, membership_mismatch, empty_guard, size_guard}`, `fallback_taken=true`, and bounded `doc_count`. Never write prompts, response bodies, bearer/API keys, or unbounded exception text. Cap the audit file at 1 MiB and rotate/truncate before appending more data.
   - The rerank-only HTTP call has a bounded 3000 ms timeout. A listening-but-silent loopback endpoint must therefore fall back within 10 seconds, including process overhead.
2. Minion implementation:
   - `submit_job`, `claim_job`, `complete_job`, `fail_job`, `cancel_job`, and `reclaim_stalled` remain token-fenced and transactional at the SQL update boundary.
   - Reclaim clears the stale lock and increments `attempts` exactly once per reclaimed job; a second claimant cannot take an active job before the lock expires.
   - Failed jobs persist bounded `error_text` and enter the documented terminal `failed` state; cancellation works for `waiting` and `active` jobs.
3. Dream implementation:
   - Keep phases `orphans`, `extract_facts`, `consolidate`, `embed`, and `purge` independently dispatchable.
   - Add an explicit per-phase mutation count to the report. Dry-run reports zero mutations and never writes.
   - `--apply --phase <non-purge>` runs only the selected phase. `--apply` without `--phase` runs the non-destructive phases only and reports purge as skipped; purge requires explicit `--phase purge` (or the equivalent explicit MCP argument).
   - Purge eligibility is exactly `pages.deleted_at IS NOT NULL AND pages.deleted_at < datetime('now', '-' || retention_hours || ' hours')`. Purge deletes eligible pages only; `content_chunks`, `tags`, and `page_versions` are removed by their page foreign keys, while `facts` for those page ids and `links` whose source/from or source/to references an eligible page are explicitly removed. Active/non-deleted pages, jobs, and unrelated facts/links survive.
   - Purge retention defaults to 72 hours and is clamped to 1..8760 hours. The input contract is: absent/empty -> 72; `0` or any negative integer -> 1; `1` -> 1; `72` -> 72; `8760` -> 8760; values above 8760 -> 8760; non-numeric or overflow input -> fail closed with no mutation. Rows inside the retention window are never deleted.
4. Schema v6 migration:
   - Add/verify nullable `TEXT` columns `jobs.lock_token` and `jobs.error_text` (both default `NULL`) and the supporting index for both fresh and populated v5 databases. Existing NULL values are valid; claim writes a non-empty token before any completion/failure operation.
   - Keep migration additive, transactional, and idempotent. An injected mid-v6 failure must roll back both column/index changes and the schema-version marker. Document that rollback/downgrade is unsupported and that pre-v6 binaries have no supported write/compatibility contract for a v6 database.
5. Tests and evidence:
   - Extend focused unit tests and `scripts/n12-verify.ps1`; retain a reproducible Windows PowerShell/MSVC command path.
   - Produce the node-specific `N12-PLAN-AUDIT.md` and, after implementation/build/evidence, `N12-HARD-AUDIT.md`.

## Tests

All commands below run on native Windows PowerShell with no WSL or Docker dependency.

1. Build and regression suite:
   - `powershell -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1`
   - `build\\cl\\qbrain_tests.exe`
   - Expected: build succeeds and every registered test passes (record the exact count; it must be at least the current 18 tests).
2. Rerank failure matrix:
   - Unit injection through the existing test callback: throw, return empty, return partial/duplicate/out-of-range membership.
   - Process-level temporary-brain run with a dummy key and an asserted temporary `chat.base_url` pointing at a dynamically selected loopback port. First verify the port is free; a PowerShell `TcpListener` accepts the connection and deliberately sends no response, then closes it. Run the same query with rerank disabled to establish the local baseline.
   - Expected for each non-truncating query: exit code 0, non-empty result set, identical result count and ordering to the no-LLM/local baseline, finite scores in `[0,1]`, byte-identical serialized triples on two consecutive runs, and a new audit JSONL row containing the required safe fields. Assert the audit file contains none of the dummy key, prompt, or response text, and assert wall-clock fallback latency is below 10 seconds.
3. Minion negative and lifecycle matrix:
   - Empty claim token is rejected.
   - First claim succeeds; a second claim cannot claim the same active row.
   - Wrong/stale token `complete_job` and `fail_job` are rejected and leave the row active.
   - Matching-token complete reaches `completed`; matching-token fail reaches `failed` and stores bounded `error_text`.
   - Force `lock_until` older than the reclaim threshold; reclaim returns one row, clears the lock, changes status to `waiting`, and increments attempts exactly once. A subsequent claim increments it once more.
   - Cancel both waiting and active jobs; terminal rows cannot be cancelled again.
   - MCP/remote `submit_job`, `cancel_job`, and `run_dream` with write permission disabled return an error and leave a before/after job snapshot identical. The explicitly allowed path is tested separately.
   - Run the same wrong-token, matching-token, and reclaim checks against a pre-existing v5 job row after migration; assert the nullable-column contract does not weaken the fence. Two separate database connections concurrently attempt a claim; exactly one succeeds (the other returns no job or a documented SQLite busy result).
4. Dream phase/isolation matrix:
   - `dream --json` (default dry-run) reports all five phases and zero mutations.
   - For each of `orphans`, `extract_facts`, `consolidate`, `embed`, and `purge`, capture a before/after snapshot of every user table (`pages`, `content_chunks`, `links`, `tags`, `facts`, `page_versions`, `jobs`, `job_messages`, and any migration-created tables) plus a stable content hash. Dry-run snapshots must be identical.
   - Apply each non-purge phase on a fixture that has candidates; only that phase may change its declared tables and its `mutations` counter must equal the observed delta. The embed fixture sets `QBRAIN_EMBED_MOCK=1`, contains at least one waiting embed job, and must complete at least one job (`mutations > 0`); the variable is cleared after the test.
   - `--apply` without a phase must not run purge. Explicit `--apply --phase purge` must use the boundary table above and must preserve a recent soft-deleted row, preserve an old non-deleted row, delete one old eligible soft-deleted row, and remove only its declared cascade/reference rows. Invalid retention input must return non-zero and leave the snapshot unchanged.
   - Remote `run_dream` without explicit allow-write is denied and leaves the same snapshot.
5. Schema migration matrix:
   - Build a populated v5 fixture with pages, chunks, links, a job row, and the v1-v5 schema-version markers; run the v6 migration.
   - Assert schema version 6 or newer, both new nullable columns/index exist, all pre-existing row counts and content hashes are unchanged, the pre-existing v5 job passes wrong-token/matching-token lifecycle checks, and the second migration invocation is a successful no-op. Inject a failure after the first v6 DDL statement and assert the database and version marker are unchanged.
   - Fresh open still reaches the current schema version. Record the no-downgrade/old-binary compatibility posture in the evidence.

## Acceptance assertions (falsifiable)

1. A rerank failure injection never throws, exits non-zero, or turns a non-empty non-truncating result into an empty result.
2. Under rerank failure, result count and membership equal the no-rerank baseline and every returned hit has a defined fallback `rerank_score`.
3. Under rerank failure, serialized ordering and finite `[0,1]` scores match the no-LLM/local baseline byte-for-byte on two consecutive runs; silent-provider fallback completes below 10 seconds.
4. Every rerank fallback audit row has the required safe fields, a closed-enum reason, and contains no prompt, response body, or secret; the capped/rotated file never exceeds 1 MiB.
5. A job cannot be claimed twice while active, and completion/failure with a wrong or stale token is rejected without changing the row, including a migrated v5 job.
6. Reclaim changes an expired active job to waiting, clears its lock, and increments attempts exactly once; fail and cancel produce their documented terminal states; concurrent claim has one winner.
7. Remote submit/cancel/run_dream writes are denied without explicit allow-write and the database is unchanged by the denied request; the explicitly allowed path succeeds where its scope permits.
8. Default dream dry-run reports all five phases with `mutations=0` and leaves the full-table snapshot and content hash unchanged.
9. Applying a selected non-purge phase changes only its declared tables; the report mutation count equals the observed delta, and embed has a non-zero deterministic mock mutation.
10. Applying dream with no phase never invokes purge; explicit purge follows the eligibility/cascade predicate and the complete retention boundary table, and does not delete data newer than the retention window or old non-deleted rows.
11. Every phase (`orphans`, `extract_facts`, `consolidate`, `embed`, `purge`) has an isolated dry-run/apply test, including the destructive boundary and invalid-input no-mutation case.
12. A populated v5 database upgrades to v6 without losing rows/content, migrated jobs retain the token fence, the migration is transactional and idempotent on its second run, and no rollback is claimed.
13. Before implementation, a precondition records PASS node-specific plan/outcome audits for N1-N11. Before the outcome audit, the evidence manifest records `cl.exe` version/architecture, `/std:c++20`, exact commands, exact test count, redacted audit sample, and snapshot hashes.

## Rollback

- Disable reranking by omitting `--rerank`/`rerank_llm`; the lexical search path remains available.
- Stop workers or use the legacy embed drain path while preserving the token-fenced queue data.
- Do not attempt schema rollback. Take a database backup before migration; v6 is additive and downgrade is unsupported.
- Keep purge disabled unless the explicit purge phase and retention argument are supplied.
- Each implementation slice is independently revertible; the evidence manifest records the slice boundary and any database backup/restore rehearsal.

## Security notes

- MCP write operations remain default-deny; remote submit/cancel/run_dream need explicit allow-write, and no denied request may mutate the database.
- Rerank audit logs are hashed/redacted and bounded; no secrets or model payloads are persisted.
- The process-local query hash salt is never persisted; failure reasons are limited to a closed enum. The temporary rerank config uses only the dummy key and loopback sentinel, and the test asserts those effective values before the request. The silent loopback listener is stopped and its port is verified free after the test.
- Tests use temporary databases and a loopback failure-injection endpoint only; they never touch production `%LOCALAPPDATA%\\Qbrain` data.
- No model/provider/baseURL/key/reasoning/context configuration is changed by implementation or tests.

## Parallelism notes (optional)

- Only after the plan audit PASS may independent slices be assigned: rerank/audit, minion state machine, dream/migration tests.
- The parent agent owns merge review, Windows build/full suite, ledger update, and both Claude Code gates.

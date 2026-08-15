# N14 Plan - Job Control, Progress, Status Snapshot, and Doctor Remediation

**Status**: done (plan + outcome audit PASS 2026-08-04)
**Plan audit**: PASS (`N14-PLAN-AUDIT.md`, Claude Code, 2026-07-30)
**Outcome audit**: PASS (`N14-HARD-AUDIT.md`, Claude Code, 2026-08-04)
**Depends on**: N1-N13 approved and done; especially N1 MCP write default-deny, N2.5 canonical source ids, N4 provider-secret handling, N6 job/embed foundations, N8 selected-brain isolation, N11 read-only doctor separation, N12 token-fenced minion lifecycle, and N13 `local_only` write-gate/full-snapshot evidence
**Process note**: This is a retrospective re-plan under the updated N1-N13 contracts. The 2026-07-26 N14 plan, shared `n14-16-evidence`, and historical `N14-HARD-AUDIT.md` are context only and cannot satisfy the current plan or outcome gate. No N14 implementation may proceed until a new node-specific Claude Code plan audit returns PASS and this plan is changed to `approved`.

## Goal

Re-verify and, where necessary, correct the existing N14 operational-control surface without weakening the completed N1-N13 contracts:

1. Pause and resume jobs while preserving the N12 claim-token fence and allowing only documented state transitions.
2. Return bounded, non-mutating job progress and a compact snapshot for the selected brain.
3. Keep `doctor` read-only by default while making explicit remediation idempotent, accurately reported, and safe under failure/concurrency.
4. Prove the CLI/MCP behavior with node-specific native Windows/MSVC evidence rather than relying on the historical combined N14-N16 report.

N14 is a bounded Qbrain capability subset. It does not claim gbrain delayed-job scheduling, percentage/step progress, full sync/cycle status parity, or destructive schema repair.

## Ledger rows moved to implemented

The ledger rows remain provisional until the new N14 outcome audit passes. Their final notes must state the exact Qbrain subset below.

| op | scope and locality | N14 contract |
|----|--------------------|--------------|
| `pause_job` | Write, `local_only=true` | Strict positive job id; `waiting` or `active` to `paused`; clear `lock_token`/`lock_until`; preserve payload, result, queue, priority, attempts, and error fields; stale worker token cannot complete/fail afterward |
| `resume_job` | Write, `local_only=true` | Strict positive job id; `paused` to `waiting`; clear fence fields; preserve non-state job data; next claim obtains a new token through the N12 claim path |
| `get_job_progress` | Read | Return only `id`, `type`, `status`, `attempts`, `lock_until`, and bounded/redacted `error_text`; unknown or malformed id is a structured error; no mutation |
| `get_status_snapshot` | Read | Selected-brain schema version, live page count, stored chunk/link counts, embedded chunk count, and all-queue `waiting`/`active`/`failed`/`paused` job counts; no host-global or cross-brain state |
| `doctor_remediate` | Write, `local_only=true`; CLI `doctor --remediate` | Ensure canonical default source, reclaim only expired active jobs in the default queue, and enqueue one pending embed job per eligible live page only when embedding availability is reported; return observed mutation counts |

## Deliverables

1. `include/qbrain/jobs/minions.hpp` and `src/qbrain/jobs/minions.cpp`: audited pause/resume/progress/count contracts that preserve N12 token fencing and reject undocumented states without mutation.
2. `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`: exact selected-brain status snapshot plus idempotent remediation. Pending embed identity must be determined from parsed/canonical JSON `page_id`, not substring matching (for example, page 1 must not collide with page 10).
3. `src/qbrain/ops/handlers.cpp`: strict full-string positive `int64` id parsing; correct Read/Write scope and `local_only` metadata; safe JSON serialization; structured failures instead of exceptions escaping the operation boundary.
4. `src/qbrain/cli/commands.cpp`: `doctor` remains the N11 read-only path; only explicit `doctor --remediate` invokes the N14 write path, then reports the post-remediation doctor result with stable exit behavior.
5. A dedicated `tests/test_n14.cpp`, registered in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`, so N14 acceptance is independently attributable. Existing N11-N13 tests remain regression gates.
6. `scripts/n14-verify.ps1` and `docs/nodes/n14-evidence/VERIFY-REPORT.md` plus captured build, test, and CLI-smoke output. Evidence must include stable database snapshot hashes and observed row deltas for each mutating case.
7. No schema migration is planned. If implementation proves a schema change unavoidable, stop and return the plan to `draft` for a revised dependency, migration, rollback, and populated-database test matrix before editing schema code.
8. After implementation and evidence only: a new node-specific Claude Code outcome audit, followed by N14-only ledger/status updates if and only if that audit returns PASS with no blocking P0/P1.

## Tests

All commands and runtime checks run on native Windows 11 PowerShell/MSVC with C++20. WSL and Docker are not part of the build or test path.

1. Native build and complete regression suite:
   - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1`
   - `build\cl\qbrain_tests.exe`
   - Record the MSVC compiler version and architecture, `/std:c++20`, exact commands, exit codes, and exact registered test count. Every test must pass and the count must be at least the N13 baseline of 21.
2. Pause/resume state matrix:
   - Exercise waiting, active, paused, completed, failed, cancelled, unknown, zero/negative, overflow, and trailing-junk ids.
   - Waiting and active may pause; only paused may resume. Repeated or invalid transitions return false/structured error and leave a full row/database snapshot unchanged.
   - Pausing active clears its fence immediately; completion/failure with the old token fails. Resuming preserves attempts and payload; a later claim increments attempts only through the normal N12 claim path and receives a new token.
   - A paused job is not claimable. After resume, two concurrent claim attempts have exactly one winner (or one documented SQLite-busy loser) and no duplicate execution.
3. Progress read matrix:
   - Cover waiting, active, paused, failed, and terminal rows; assert exact JSON field names/types and exact values from the selected job.
   - Assert omission of payload, result JSON, priority, queue, `lock_token`, and any provider/model configuration.
   - Seed a bounded dummy error containing credential-like text and prove the remote serialization redacts it; missing/malformed ids return structured errors and do not mutate the database.
4. Status snapshot matrix:
   - Seed known live and soft-deleted pages, chunks, links, embeddings, and every job status. Assert `pages` counts live rows, chunks/links follow the existing stored-row `BrainStats` contract, and the four exposed job counters equal direct SQL counts across all queues.
   - Assert `schema_version` equals the database integrity/schema query, not a hard-coded historical value.
   - Create two brain databases with different fixtures and prove no count crosses the N8 selected-brain boundary. Repeat the read and prove byte-identical full snapshots.
   - A damaged/missing required table produces a structured operation error without process termination or attempted repair; N11 `run_doctor` remains the diagnostic path.
5. Remediation matrix:
   - Remove the default source from an otherwise valid fixture; remediation restores exactly canonical `default` and a second call is a no-op for that action.
   - Seed expired and future active leases in the default queue plus an expired lease in another queue. Only the expired default-queue row is reclaimed, its fence is cleared, attempts increments exactly once, and its old token remains invalid. A second remediation does not increment it again.
   - With embedding unavailable, report `api_key_present=false`, enqueue zero embed jobs, perform no outbound request, and never expose a key or configuration value.
   - Through a deterministic in-memory dependency seam (not by changing a real API key, base URL, provider, model, or agent configuration), report embedding available. Multiple missing chunks on one live page enqueue exactly one waiting embed job; soft-deleted or already embedded pages enqueue none.
   - Existing waiting/active/paused embed jobs for the same parsed integer page id suppress duplicates; terminal jobs do not permanently suppress a needed retry; malformed payloads and page-id prefix collisions do not suppress the correct page. A second remediation is idempotent.
   - Two database connections racing remediation produce at most one pending embed job per page, with a documented SQLite-busy loser permitted. Inject a SQLite failure during the mutation sequence and assert the default-source, reclaimed-job, and embed-job changes roll back to the pre-call full snapshot.
   - Report fields (`default_source`, `reclaimed`, `embed_jobs_enqueued`, `api_key_present`, `notes`) must equal observed database deltas and decisions.
6. Registry/MCP security matrix:
   - Inspect registered metadata: `pause_job`, `resume_job`, and `doctor_remediate` are Write plus `local_only=true`; `get_job_progress` and `get_status_snapshot` are Read.
   - Call each N14 write op through a remote context with `allow_write=false`; every call is denied before its handler and the complete database snapshot/hash is unchanged.
   - Exercise valid explicitly allowed calls separately and assert only the documented rows/fields change. Read ops succeed with write disabled and leave the snapshot unchanged.
7. CLI smoke matrix:
   - Use a unique temporary `LOCALAPPDATA` sandbox and brain id; never touch a production brain.
   - Run `qbrain doctor --json`, then `qbrain doctor --remediate --json`, then `qbrain doctor --json`; assert finite execution, stable JSON, expected exit codes, canonical default source, and post-remediation health.
   - The smoke must not change model/provider configuration, persist a dummy secret, or make a live network call.

## Acceptance assertions (falsifiable)

1. N14 implementation begins only after a new Claude Code plan audit returns PASS and the plan is marked `approved`; the old combined evidence and historical hard audit are not used as gate substitutes.
2. `pause_job` changes only waiting/active rows to paused, clears their fence fields, preserves all non-state job data, and makes any old active-worker token unusable.
3. `resume_job` changes only a paused row to waiting; the row remains unclaimed until the N12 claim path assigns one new token, and concurrent claim has one winner.
4. Completed, failed, cancelled, already-paused/already-waiting wrong-state, unknown, malformed, non-positive, and overflow inputs fail without changing a full database snapshot.
5. The operation parser consumes the entire decimal id string; an input such as `1junk` can never operate on job 1.
6. `get_job_progress` returns exactly the documented safe fields and correct state/attempt/fence information, redacts credential-like dummy material from `error_text`, and never returns payload/result/lock token or mutates SQLite.
7. `get_status_snapshot` matches direct SQL/schema evidence for the selected brain, follows the documented live-page/stored-row count semantics, exposes only the four declared job counters, and cannot read another brain's rows.
8. A malformed snapshot fixture fails in a structured manner without repair or process termination; default `run_doctor` remains read-only and unchanged from N11.
9. Remediation restores only canonical `default`, reclaims only expired active default-queue jobs, clears their stale fences, increments attempts once, and is idempotent on the next run.
10. With embedding unavailable, remediation enqueues no embed jobs and makes no provider request; with deterministic injected availability, it enqueues exactly one pending job per eligible live page and reports the exact observed count.
11. Embed deduplication uses parsed integer identity: page 1 and page 10 do not collide, multiple chunks do not duplicate work, pending states suppress duplicates, terminal states permit a needed retry, and malformed payloads fail safely.
12. Concurrent remediation leaves at most one pending embed job per eligible page; an injected database failure rolls back the complete N14 remediation mutation set.
13. Remote calls to all three N14 Write operations with write disabled are rejected before handler mutation and preserve a byte-identical full database snapshot; explicitly allowed valid calls mutate only their declared surface.
14. Progress and status Read operations work with write disabled, emit valid bounded JSON, expose no secret/configuration values, and leave the database unchanged.
15. Native Windows MSVC evidence records Windows/x64, compiler version, `/std:c++20`, exact commands and exit codes, exact all-PASS test count `>=21`, CLI runtime markers, row deltas, and snapshot hashes. No LLM/agent/application model, provider, base URL, API key, reasoning, context, or compression configuration is changed.

## Explicit exclusions and no-N30 rule

- N30 is not a dependency, deliverable, coordinator, evidence container, audit substitute, or follow-up for N14. No `N30-*` file is created or used. N14 closes through its own PLAN -> PLAN-AUDIT PASS -> implementation/evidence -> HARD-AUDIT PASS loop.
- N15, N16, N18, and later-node capabilities are outside this plan even if their historical code is present in the worktree. Their plans and audits remain separate.
- No delayed-job scheduler/state, percentage progress engine, full upstream sync/cycle status envelope, cross-brain aggregate status, remote provider call, or schema repair is claimed by N14.
- No commit or push is part of the node unless the human user separately requests it.

## Rollback

- Keep MCP writes disabled and unregister/hide the three N14 write operations if their transition or authorization contract cannot be maintained; reads and N11 `run_doctor` remain available.
- Resume intentionally paused jobs through the audited transition. Do not repair job state with ad hoc SQL in the normal operator path.
- Omit `--remediate` to retain read-only doctor behavior. Remediation is idempotent and transactionally bounded; restore the pre-test database backup if a rehearsal fails.
- No schema downgrade is needed because no schema change is planned. Any later schema proposal requires a revised audited plan before implementation.

## Security notes

- MCP mutation remains default-deny. The registry must reject remote N14 writes before handler execution when `allow_write=false`, and denial evidence must cover the whole database rather than a single row count.
- Job ids are untrusted input and require strict positive-integer parsing with overflow/trailing-data rejection.
- Pausing an active job is a security boundary: clearing the N12 fence must make stale completion/failure tokens ineffective before the operation returns.
- Progress output is bounded and redacted. It never includes payload/result JSON, lock tokens, API keys, provider URLs, model names, or configuration dumps.
- Status is scoped to the selected N8 brain. Remediation touches only the selected database, canonical default source, default queue, and eligible live pages.
- Tests and CLI smoke use temporary databases and a temporary `LOCALAPPDATA`; they do not touch production `%LOCALAPPDATA%\Qbrain` data or perform live network requests.
- No model/provider/baseURL/key/reasoning/context/compression setting is modified by planning, implementation, testing, or evidence collection.

## Dependencies and parallelism notes

- Before implementation, verify that every N1-N13 node has a node-specific plan-audit PASS and outcome-audit PASS; record those references in N14 evidence. A stale metadata typo cannot replace the audit document itself.
- After plan approval only, disjoint slices may cover job transitions, snapshot/remediation, and focused tests/evidence. `src/qbrain/ops/handlers.cpp`, `src/qbrain/core/brain.cpp`, and test registration are shared hot files and require parent-owned merge review.
- The parent agent owns the native build/full suite, CLI smoke, evidence manifest, ledger update, and both N14 Claude Code gates. No subagent may mark N14 done or write a PASS audit.

# N17 Plan - Audited Job Replay and Job Message Inbox

**Status**: done
**Direct dependencies**: N8 selected-brain routing/isolation, N12 token-fenced job lifecycle, N13 in-place retry semantics, N14 strict positive-id and local-only write contracts, and N15 schema v12 baseline. N1 operation scopes/MCP default-deny and N7 loopback authentication are transitive contracts through those nodes. N17 is this plan's retrospective closure target, not a dependency of itself; N18, N19, and every later node are not dependencies.
**Plan audit**: PASS (`N17-PLAN-AUDIT.md`, Claude Code, 2026-08-04)
**Outcome audit**: PASS (`N17-HARD-AUDIT.md`, Claude Code, 2026-08-04)
**Process note**: This is a retrospective re-verification and correctness closure of existing N17 surfaces. The replay/message code, historical ledger claims, tests, and 2026-07-26 `N17-HARD-AUDIT.md` may already exist or have been used in production, but they were produced under the old combined-wave process. They are background evidence only and do not satisfy the current node-specific plan-audit or outcome-audit gates.

The first current-process plan audit on 2026-08-04 returned FAIL; the revised plan then received a fresh Claude Code PASS. The parent marked this plan approved only after that PASS, before implementation/evidence work began. N17 is not a greenfield dependency and does not claim that the historical implementation already conforms.

## Goal

Re-verify and, where necessary, correct the existing N17 job replay and job-message capability against the current schema v12, selected-brain, strict-input, MCP default-deny, and native Windows evidence contracts. The node closes governance and correctness gaps in existing surfaces; it does not pretend that historical PASS evidence was produced under the current rules.

N17 has three bounded surfaces:

1. Replay a terminal `failed` or `completed` job by creating exactly one fresh `waiting` job while preserving the complete original row.
2. Append a validated, bounded JSON message to an existing job and list that job's messages with deterministic ordering and limits.
3. Prove that the historical schema v8 `job_messages` step remains correct inside fresh, upgraded, and already-current schema v12 databases.

N17 does not reset a source job in place, delete jobs, copy messages to replayed jobs, interpret a job payload's optional `source_id`, or claim a cross-brain/global queue. Jobs and messages belong to the physically selected N8 brain database.

No LLM or agent model, provider, base URL, API key, reasoning effort, context size, or compression threshold is read or changed by this node. No third-party dependency is introduced.

## Historical divergence and new corrective work

The current implementation is known not to satisfy the refreshed contract in several places. These are explicit corrective changes, not silent compatibility assumptions:

- Historical `replay_job` accepts any existing status, and the historical audit even exercised a `waiting` source row. N17 intentionally changes the final contract to `failed`/`completed` only. Replaying `waiting`, `active`, or other nonterminal rows can duplicate work, bypass an active worker's token fence, or replay an operator-paused/cancelled decision. This is an intentional breaking correctness/security fix to an existing production/ledger surface.
- There is no silent compatibility claim for old waiting/any-status callers. They must use the documented N12-N14 lifecycle or N13 `retry_job` path instead. The outcome evidence must name this divergence and prove the rejected states.
- Strict full-string job-id parsing is new corrective work; historical `std::stoll` prefix/whitespace behavior is not retained.
- Terminal-state guarding plus one atomic clone is new corrective work; the historical pre-read/unconditional clone is not retained.
- Structured error codes and bounded error serialization are new corrective work; historical generic text/zero return values are not accepted as the final contract.
- Typed MCP argument validation, `additionalProperties:false` schemas, and pre-dispatch rejection for these operations are new corrective work.
- `list_job_messages` exists as a C++ helper but is not currently registered. Its Read registration and real MCP/CLI serialization path are new corrective work.
- The v8 `job_messages` DDL already exists in the migration chain and has already been applied by current schema v12 databases. N17 verifies that existing DDL and version markers match the declared shape; it does not re-implement v8 or introduce schema v13. If current-v12 integrity checks are missing, adding only those targeted checks is corrective work. Any DDL/version mismatch discovered during implementation requires returning this plan to draft for a separately audited migration change.

## Ledger rows to reconcile after outcome PASS

The current ledger entries are historical claims until this fresh node loop completes. N17 owns exactly these two upstream operation rows:

| op | intended bounded evidence |
|----|---------------------------|
| `replay_job` | Clone an existing `failed` or `completed` job into one fresh `waiting` row; preserve the original row and do not copy its messages |
| `send_job_message` | Append one validated, bounded JSON message to an existing job in the selected brain |

`list_job_messages` is a Qbrain read helper required to make the inbox usable and testable. It is not an upstream ledger row and must not be added to, counted in, or represented as an implemented operation in the upstream parity ledger. No N19 or later operation may be attributed to N17.

## Dependency and frozen-baseline evidence

The dependency claim is intentionally narrow. Each direct or transitive contract below has its own current artifact with a literal Claude Code `VERDICT: PASS`; none is an N17 artifact or a substitute for N17's own gates.

| Contract | Plan audit | Outcome audit | Use in N17 |
|----------|------------|---------------|------------|
| N1 operation scopes/default-deny | `docs/nodes/N1-PLAN-AUDIT.md` PASS | `docs/nodes/N1-HARD-AUDIT.md` PASS | transitive authorization invariant |
| N7 authenticated loopback MCP | `docs/nodes/N7-PLAN-AUDIT.md` PASS | `docs/nodes/N7-HARD-AUDIT.md` PASS | transitive transport invariant |
| N8 selected-brain isolation | `docs/nodes/N8-PLAN-AUDIT.md` PASS | `docs/nodes/N8-HARD-AUDIT.md` PASS | direct physical tenancy boundary |
| N12 token-fenced jobs | `docs/nodes/N12-PLAN-AUDIT.md` PASS | `docs/nodes/N12-HARD-AUDIT.md` PASS | direct job-state/fence contract |
| N13 in-place retry separation | `docs/nodes/N13-PLAN-AUDIT.md` PASS | `docs/nodes/N13-HARD-AUDIT.md` PASS | direct alternative to replay |
| N14 strict ids/local-only job writes | `docs/nodes/N14-PLAN-AUDIT.md` PASS | `docs/nodes/N14-HARD-AUDIT.md` PASS | direct parser/security baseline |
| N15 current schema v12 | `docs/nodes/N15-PLAN-AUDIT.md` PASS | `docs/nodes/N15-HARD-AUDIT.md` PASS | direct database-version baseline |

N16 and N18 are completed Wave 3 regression baselines, not N17 dependencies. N18's current artifacts are `docs/nodes/N18-PLAN-AUDIT.md` PASS and `docs/nodes/N18-HARD-AUDIT.md` PASS. N19 remains outside this plan and supplies no dependency or gate.

The exact frozen Wave 3 runtime baseline is:

| Artifact | SHA-256 / fact |
|----------|---------------|
| `build/wave3-final-production.log` | `ea907ecda1afb9775d3c0ae3483149281e17b2c5056c74ef03ba02b87f372e4c` |
| `build/wave3-final-tests.log` | `634090ed6204f1f5e7b7aa4ad487a0470d52878b8b7008cd842c8f5757853b5b`; `expected_registered_tests=25`, `observed_pass_count=25`, `observed_fail_count=0` |
| `build/wave3-final-build-manifest.txt` | `d3fc2bb3ae1e81c2df340ab89d4b9ed76acbaf477947da6676b3cecd6a4c30e3`; 110 `FILE` entries and 2 `ARTIFACT` entries; zero `N30` matches |
| N14 outcome audit | `docs/nodes/N14-HARD-AUDIT.md`; `15c9b5a0886e6b9406b6bbe6ed2cd12e12ffa1b732768193a4e281355b1f3c2b` |
| N15 outcome audit | `docs/nodes/N15-HARD-AUDIT.md`; `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59` |
| N16 outcome audit | `docs/nodes/N16-HARD-AUDIT.md`; `591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba` |
| N18 outcome audit | `docs/nodes/N18-HARD-AUDIT.md`; `f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053` |

These artifacts establish a regression starting point only. They do not prove N17 correctness and do not close either N17 audit gate.

## Scope and exclusions

In scope: corrective N17 declarations and implementations in `include/qbrain/jobs/minions.hpp` and `src/qbrain/jobs/minions.cpp`; read-only verification of the already-existing v8 DDL plus targeted additions to current integrity checks in `src/qbrain/storage/migrate.cpp` if those checks are absent; and the smallest N17-specific operation and typed MCP validation changes in `src/qbrain/ops/handlers.cpp` and `src/qbrain/mcp/server.cpp`.

Only if required for structured authorization errors without weakening other operations may N17 touch `src/qbrain/ops/registry.cpp`. A dedicated `tests/test_n17.cpp`, test registration, native Windows verification artifacts, and post-PASS reconciliation of only the two owned ledger rows are also in scope.

Out of scope: changing N12 claim/complete/fail behavior, changing N13 in-place `retry_job`, changing N14 pause/resume/progress, deleting or pruning jobs/messages, adding job lineage columns, copying job messages during replay, delayed scheduling, worker orchestration, cross-brain queues, treating payload `source_id` as an authorization scope, a schema version above 12, schema downgrade support, N19 or later capabilities, and any commit or push.

## Authoritative behavior

### 1. Strict job identifier contract

- `job_id` is the canonical argument for all three N17 operations. The historical `id` spelling remains a compatibility alias.
- If both are present they must parse to the same value; otherwise return a structured `invalid_argument` error naming `job_id` before any database access.
- Local/CLI string arguments must consist of the complete ASCII decimal representation of an integer in `1..INT64_MAX`. Reject empty input, zero, signs, whitespace, decimal points, suffixes such as `1junk`, embedded NUL/control bytes, and overflow. Prefix parsing is forbidden.
- MCP JSON accepts a positive integer, not a numeric string, floating point value, negative integer, boolean, object, array, or null. Runtime validation must enforce this rather than relying only on the advertised JSON Schema.
- Unknown fields are rejected for MCP calls before dispatch and for direct registry calls before handler mutation. Invalid, missing, conflicting, and unknown ids are distinguishable from an existing job in an invalid replay state.

### 2. Replay contract

- A source job is replayable only when its current status is exactly `failed` or `completed`. `waiting`, `active`, `paused`, `cancelled`, `dead`, arbitrary statuses, and a missing id are rejected without mutation. `retry_job` remains the separate N13 in-place path for its documented statuses.
- One successful call inserts exactly one new row using one atomic SQL mutation whose source-id and source-status predicates are evaluated at insert time. A pre-read followed by an unconditional insert is not sufficient.
- The new row has a distinct positive id; copies `queue`, `type`, `payload_json`, and `priority` byte-for-byte; has `status='waiting'`, `attempts=0`, NULL/empty `result_json` and `error_text`, NULL/empty `lock_token` and `lock_until`, and fresh non-empty database timestamps.
- Replay treats the stored job payload as opaque text. It neither repairs nor canonicalizes it; exact payload preservation is the contract.
- This differs intentionally from new message input: replay clones an already-stored audit payload byte-for-byte, while `send_job_message` validates and canonicalizes new untrusted JSON before storage. Replay tests compare the raw database text directly and must not parse/dump it before the equality assertion.
- The complete original row remains byte-equivalent across the call, including status, queue, type, payload, result, error, priority, attempts, lock token, lock deadline, and timestamps. This is required even for deliberately seeded terminal rows containing unusual result/error/fence values.
- Existing messages remain attached only to the original id. The fresh id begins with zero messages. No lineage relationship is claimed beyond the structured response fields `original_id`, `new_id`, and `status`.
- Each independently successful concurrent replay request represents a distinct replay and creates exactly one unique clone. If SQLite returns busy/locked for a loser, the operation returns a bounded structured `database_busy` error and makes no partial mutation; a retry can then create its own one clone.

### 3. Message write and read contract

- `send_job_message` accepts messages for any existing job status. It rejects a missing job with structured `not_found` and performs no insertion or sequence change.
- Omitted `sender` defaults to `system`. A supplied sender must be valid UTF-8, 1..128 bytes, contain no embedded NUL, C0 control, or DEL byte, and is stored byte-for-byte. A supplied empty or boundary-plus-one sender is invalid rather than silently defaulted.
- Omitted `payload_json` defaults to `{}`. A supplied value must be non-empty valid UTF-8 and syntactically valid JSON. Incoming and canonical stored forms must each be at most 65,536 bytes. Parse and canonicalize with the existing JSON library before mutation; malformed UTF-8, malformed JSON, raw embedded NUL, boundary-plus-one input, or canonical expansion beyond the limit returns structured `invalid_argument` with no insert.
- Existence testing and insertion are one atomic database operation/transaction. One successful call inserts exactly one message and changes no job field or other application table.
- `list_job_messages` first distinguishes a missing job (`not_found`) from an existing job with no messages (successful `[]`). It never creates a job or source and never writes SQLite.
- List `limit` defaults to 50 and has effective bounds 1..200. An omitted value uses 50; a syntactically valid unsigned zero clamps to 1; an unsigned value above 200 clamps to 200. String-path parsing consumes the entire decimal input and rejects signs, whitespace, decimals, suffixes, empty supplied values, and overflow. MCP type validation accepts only an unsigned JSON integer.
- Messages are returned newest first by `id DESC`; ids are the deterministic tie-breaker for equal timestamps. Every item contains exactly `id`, `job_id`, `sender`, `payload_json`, and `created_at`; `payload_json` must parse as the stored canonical JSON document. Repeated reads on an unchanged database are byte-identical.
- Two connections concurrently sending to one job must produce one unique row per successful call. A documented structured SQLite-busy loser is permitted only with no partial row; retry must succeed after the winner commits. Listing committed rows returns each once in the declared order and never exposes another physical brain's messages.

### 4. Existing schema v8 inside the schema v12 baseline

The v8 step already exists at `src/qbrain/storage/migrate.cpp` and current databases have already advanced to schema v12. Its DDL and version marker are verification inputs, not fresh N17 implementation. This plan expects the existing v8 DDL below to remain byte/semantically unchanged and does not authorize schema v13 or any rewrite of an already-current database:

```sql
CREATE TABLE job_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  job_id INTEGER NOT NULL,
  sender TEXT NOT NULL DEFAULT 'system',
  payload_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX idx_job_messages_job ON job_messages(job_id, id);
```

- The current shape intentionally has no foreign key. The explicit orphan policy is: Qbrain retains job rows as audit records; N17 introduces no job-delete path; message send atomically requires an existing parent; list refuses a nonexistent parent; and future job deletion/pruning requires its own audited plan and either cascade or explicit message cleanup. N17 must not claim database-level cascade protection.
- The known corrective storage gap is narrower: current schema-v12 integrity checks may omit `job_messages`, `idx_job_messages_job`, or their required columns. If inspection confirms that omission, N17 adds detection only. It does not add/alter the table, index DDL, schema marker, defaults, or foreign-key policy.
- A fresh database must traverse the migration chain to current schema version 12 and contain the exact v8 table/index shape. A populated v7 fixture must migrate through v8 to v12 without changing its existing jobs or other data.
- An already-populated, correctly shaped v12 database must remain a no-op when migrations run again: schema SQL, application rows, schema markers, and index shape stay identical.
- Inject failure at the v8 schema-version marker after v8 DDL has begun. The transaction must roll back the table, index, and version marker while preserving the complete populated v7 fixture. Removing the injection and retrying must succeed through v12.
- Current schema integrity checks must report missing `job_messages`, missing `idx_job_messages_job`, or wrong/missing required columns as a structured integrity failure on a damaged v12 database. They do not silently repair or destructively rebuild a current database.
- Downgrade and pre-v8 writer compatibility are unsupported. Deployment rollback uses a pre-migration database backup, not reverse DDL.

### 5. Operation scopes, MCP schemas, and errors

- Register `replay_job` and `send_job_message` as `Scope::Write`, `local_only=true` under the current registry semantics. A remote request is rejected before its handler unless `--allow-write`/`allow_write=true` is explicit. An allowed remote request may perform only the documented mutation.
- Register `list_job_messages` as `Scope::Read`, `local_only=false`. It works when writes are disabled and remains read-only.
- Advertised schemas use `type: object`, `additionalProperties: false`, exact property types/bounds, and required/alias rules consistent with runtime behavior. Add all three operations to the MCP typed-argument map so non-object arguments, unknown keys, wrong JSON types, and numeric strings are rejected before dispatch.
- Every handler-originated failure returns `ok=false`, nonzero exit code, bounded text, and JSON shaped as `{"error":{"code":"...","field":"...","message":"..."}}`. Required codes include `invalid_argument`, `not_found`, `invalid_state`, `database_busy`, and `database_error` where applicable. Messages do not echo untrusted payloads, sender text, SQL, database paths, tokens, or configuration.
- Remote write denial returns MCP `isError=true` and a structured bounded `write_denied` error while preserving the shared N1/N13 authorization order: denial occurs before the handler and before database access. Any shared registry adjustment must add structure without widening authorization or changing unrelated operation scopes.
- All three operations use only `ctx.brain`, so the N8-selected physical database is the complete tenancy boundary. They do not read ambient `QBRAIN_SOURCE`, create sources, inspect another brain, touch the filesystem, contact the network, enqueue provider work, or read/change model configuration.

### 6. Full logical snapshots and allowed deltas

- A "full logical snapshot" is a deterministic, binary-safe serialization of normalized application schema objects from `sqlite_master` (tables, indexes, triggers, and virtual-table declarations), every row of every non-shadow application-owned table discovered from that schema in stable primary-key/column order, and `sqlite_sequence` when present.
- SQLite internal tables and FTS shadow tables are excluded from row enumeration only when their owning virtual-table declaration and all application source rows/triggers are included; tests record the exact exclusion list. Text length/bytes, NULL, integer, real, and blob values are type-tagged so embedded NUL or malformed text cannot compare equal accidentally.
- The snapshot includes the complete selected brain and a separately opened decoy physical brain. A read, validation failure, unknown id, invalid replay state, remote denial, database-busy loser, or injected migration failure requires identical before/after hashes and zero row/sequence delta in both brains.
- A single successful replay permits only one appended `jobs` row and the corresponding expected `sqlite_sequence` advance. A single successful send permits only one appended `job_messages` row and its corresponding expected sequence advance. Every pre-existing row, every other table, every schema object, and the complete decoy brain remain identical.
- Concurrency evidence must handle both legitimate schedules. If both calls succeed, replay produces exactly two distinct new jobs or send produces exactly two distinct messages. If one call returns structured `database_busy`, exactly one row is added and the loser has no partial delta; retry after the winner commits adds its own distinct row.

## Deliverables

1. Audited N17 job/message API in `include/qbrain/jobs/minions.hpp` and `src/qbrain/jobs/minions.cpp`, implementing the exact replay, validation, ordering, transaction, and concurrency behavior above.
2. N17 operation registrations and strict parsers in `src/qbrain/ops/handlers.cpp`, with the smallest necessary typed MCP map changes in `src/qbrain/mcp/server.cpp` and structured pre-handler denial support if required.
3. Verification evidence for the already-existing v8 migration plus targeted current-v12 integrity checks in `src/qbrain/storage/migrate.cpp` for the known missing table/index/column coverage. The v8 DDL, version marker, current schema version 12, and no-FK policy remain unchanged.
4. Dedicated `tests/test_n17.cpp`, registered in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`. Existing N12-N16/N18 tests remain unchanged regression gates unless a directly affected assertion requires an explicit, reviewed update.
5. `scripts/n17-verify.ps1` and `docs/nodes/n17-evidence/VERIFY-REPORT.md` with captured production build, full test build/runtime, schema/migration, concurrency, MCP, snapshot, and artifact-hash evidence. Evidence reports facts and cannot issue an audit verdict.
6. After implementation and evidence only, a new node-specific Claude Code outcome audit against this approved plan. Only after PASS may the parent set the plan to `done` and reconcile exactly `replay_job` and `send_job_message` in the upstream ledger.

## Tests

All tests and scripts run on native Windows 11 PowerShell with MSVC x64 C++20. WSL and Docker are not part of the build or runtime path.

1. Native build and regression suite:
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1` and record exit code.
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1` and the resulting `build\cl\qbrain_tests.exe` according to the script contract.
   - Record Windows/x64, full `cl.exe` version, `/std:c++20`, exact commands/exit codes, exact registered count, and every result. The suite must include `[PASS] n17`, have at least 26 registered tests (the completed Wave 3 baseline is 25), and have zero failures.
2. Strict id and replay state matrix:
   - Exercise canonical `job_id`, legacy `id`, equal aliases, conflicting aliases, omitted, empty, zero, negative/sign, whitespace, decimal, trailing junk, embedded control, `INT64_MAX`, overflow, and unknown id.
   - Seed `failed`, `completed`, `waiting`, `active`, `paused`, `cancelled`, `dead`, and arbitrary statuses. Exactly `failed` and `completed` replay successfully; every rejection preserves the full logical database snapshot.
   - For both successful statuses, seed distinctive queue/type/payload/result/error/priority/attempt/fence/timestamps and at least one source message. Assert the original row is byte-equivalent; exactly one new waiting row has only the declared copied fields; the new row has no messages; and every other application table is unchanged.
3. Message validation and boundary matrix:
   - Exercise omitted sender/payload defaults, exact 1/128-byte sender boundaries, 129 bytes, multibyte UTF-8 boundaries, malformed UTF-8, controls/NUL, exact valid JSON input/stored boundaries through 65,536 bytes, boundary-plus-one, malformed JSON, and canonicalization.
   - Test messages on every documented job status. Missing parent, invalid sender, invalid payload, and parse/size failure leave full snapshots and autoincrement state unchanged.
4. Message list matrix:
   - Distinguish unknown job from an empty inbox. Seed equal timestamps and assert newest `id DESC` order, exact five-field JSON shape, JSON-parseable payloads, byte-identical repeated output, and no duplicates.
   - Exercise omitted/0/1/50/200/above-max limits and reject sign, whitespace, suffix, decimal, empty supplied value, overflow, and wrong MCP JSON types. Every list path preserves full selected and decoy database snapshots.
5. Migration and integrity matrix:
   - Fresh open reaches v12 with the exact v8 table/index/column/default/no-FK shape.
   - A populated v7 database migrates through v8 to v12 with every existing job and unrelated row/content hash unchanged; a second invocation is a no-op.
   - Trigger a deterministic failure on insertion of schema marker 8 and prove complete rollback, then remove the injection and prove successful retry.
   - On copies of a v12 fixture, remove the table, index, or required column shape and prove integrity check fails with the corresponding bounded reason without repair. `PRAGMA foreign_key_list(job_messages)` is empty and the evidence explicitly cites the orphan policy rather than claiming cascade.
   - Hash the existing v8 DDL/version-marker source before and after N17. It must remain unchanged; only the missing current-v12 integrity-check logic may change in the migration module.
6. Registry/MCP authorization and typed-schema matrix:
   - Inspect all three registrations and real `tools/list` schemas. Verify exact scopes/locality, `additionalProperties:false`, property types, alias requirements, defaults, and limits.
   - Through real MCP serialization, reject non-object arguments, unknown fields, numeric strings, floats, negative values, conflicting aliases, malformed sender/payload, and missing ids with structured errors and no mutation.
   - Deny both writes remotely with `allow_write=false` before handler/database access; then test each valid remote write with explicit allow-write. The read succeeds with write disabled. No denied or read call changes either brain.
7. Concurrency and exact-delta matrix:
   - Race two connections replaying one terminal job and two connections sending to one job. The test must accept and verify the both-success schedule: two successes mean exactly two distinct rows. It must also verify the one-success/one-structured-busy schedule when observed: exactly one row before retry, no loser delta, then one distinct row after retry. It may not assume contention always produces a loser.
   - For every valid write, compare full logical before/after snapshots of schema and every application table. Replay permits exactly one `jobs` row plus the expected internal autoincrement delta; send permits exactly one `job_messages` row plus its expected internal autoincrement delta. No existing row or other table changes.
   - For successful/empty/malformed/unknown/denied reads and writes, also snapshot a decoy physical brain. Its schema, rows, and internal sequence state remain identical.
8. Evidence manifest and CLI smoke:
   - Run in temporary databases and an isolated temporary `LOCALAPPDATA`; do not touch production `%LOCALAPPDATA%\Qbrain` data.
   - Record build/test logs, operation schemas, runtime markers, migration/schema hashes, selected/decoy snapshots, exact deltas, concurrency outcomes, executable hashes, and hashes of every N17 deliverable plus the approved plan/audit.
   - The N17 scoped deliverable manifest must record `n30_artifact_count=0`, contain no `N30-*` path or dependency, and record zero N17-created/modified N19-or-later paths. A repository check for `docs/nodes/N30-*` must also return zero. Pre-existing historical later-node files are not N17 deliverables and are neither edited nor falsely reported as absent.
   - Assert no live network call, no secret in evidence, no model/provider/baseURL/key/reasoning/context/compression change, and no commit or push.

## Acceptance assertions (falsifiable)

1. The delivered node is a retrospective correctness closure: the evidence identifies the historical any-status replay, permissive id parsing, generic failures, and unregistered list helper as pre-fix behavior, and proves each named corrective surface without treating historical PASS text as current conformance.
2. All N17 job ids consume an entire positive ASCII decimal value in `1..INT64_MAX`; malformed, conflicting, wrong-typed, overflow, non-positive, unknown, and unexpected-field inputs return structured errors before mutation.
3. Replay's intentional breaking correction is enforced: only `failed` and `completed` sources succeed; `waiting`, `active`, `paused`, `cancelled`, `dead`, and arbitrary statuses are rejected to prevent duplicate live work, with no silent compatibility claim for the historical any-status/waiting behavior.
4. One successful replay creates exactly one distinct `waiting` row with only queue/type/payload/priority copied, reset attempts/result/error/fence, and fresh timestamps, while the complete original row remains byte-equivalent.
5. Replay preserves the original payload byte-for-byte, copies no message, changes no other application table, and handles both-success and busy-loser concurrency schedules with only the declared row/sequence deltas.
6. One successful message send requires an existing job, stores the declared sender and canonical valid JSON within exact UTF-8/byte bounds, inserts exactly one row, and changes no job or unrelated table.
7. Invalid sender/payload, malformed UTF-8/JSON, boundary-plus-one input, and a nonexistent job perform neither insert nor prune and leave application rows plus sequence state unchanged.
8. Message listing is registered and distinguishes missing job from empty inbox, returns at most the effective 1..200 limit in deterministic `id DESC` order with the exact five-field shape, and repeated unchanged reads are byte-identical.
9. Every read, malformed call, unknown-id call, invalid-state write, and denied remote write preserves the defined full logical snapshots of both selected and decoy brains; valid writes have only the exact declared selected-brain row and sequence deltas.
10. Existing v8 DDL and version-marker source remain unchanged while fresh/populated-v7/current-v12 fixtures prove the exact table/index/no-FK shape, idempotent traversal, injected rollback, and targeted damaged-v12 integrity detection without silent repair.
11. The explicit no-FK orphan policy is true in code and evidence: N17 has no job deletion, atomic send requires an existing job, missing-parent list is rejected, and no cascade claim appears in docs or ledger.
12. Registry and real MCP evidence show replay/send as Write plus `local_only=true`, list as Read, exact `additionalProperties:false` typed schemas, pre-handler remote denial without allow-write, structured bounded errors, and valid explicitly allowed writes.
13. All three operations stay inside `ctx.brain`; no cross-brain rows, source creation, ambient-source interpretation, filesystem/network/provider work, job side effects beyond replay, or model/configuration data appears.
14. Native Windows x64 MSVC `/std:c++20` production/test builds exit zero, the complete suite records at least 26 PASS and zero FAIL including dedicated N17, and the evidence manifest records exact commands, hashes, snapshots, deltas, concurrency outcomes, zero N30 paths, and zero N17-created/modified later-node paths.
15. Only `replay_job` and `send_job_message` receive fresh N17 ledger notes after the separate outcome audit passes; `list_job_messages` remains an uncounted Qbrain helper and the operation total is not incremented for it.
16. No schema version above 12, downgrade tool, message retention/deletion, N19 or later implementation, N30 artifact/dependency, third-party dependency, model configuration change, commit, or push is part of N17.

## Rollback

- Keep the two N17 writes unavailable if replay state, exact-delta, validation, or authorization guarantees cannot be maintained. Existing N12/N13 job lifecycle operations remain available.
- Keep `list_job_messages` unavailable if bounded deterministic read and selected-brain isolation cannot be proven; do not widen it or fall back to another brain.
- Do not downgrade schema. Back up a pre-v8 database before rehearsal and restore that backup if deployment migration fails. A current damaged v12 database is reported, not silently rebuilt.
- Revert the API, handler/MCP map, test registration, and v8 integrity-check slices together. Do not delete existing jobs or messages as rollback.
- Keep remote writes disabled by omitting `--allow-write`; never weaken `local_only` or bypass validation as a recovery measure.

## Security notes

- Job payloads, results, errors, locks, and messages can contain sensitive content. Replay responses never expose those fields, and errors never echo sender/payload/database/configuration text.
- MCP write operations remain default-deny. The registry denies remote replay/send before handler execution unless explicit allow-write is set; authentication and loopback policy from N7 remain unchanged.
- Strict ids, typed JSON arguments, sender/payload byte bounds, UTF-8 validation, JSON parsing, list caps, bound SQL parameters, and deterministic errors limit injection, output, storage, and parser abuse.
- `payload_json` is data only. It is never executed, used as SQL, treated as a filesystem path, interpreted as an authorization-bearing source id, or sent to a provider.
- Selected-brain isolation is the tenancy boundary because the jobs schema has no source column. Tests use two physical temporary brains and never rely on payload content for isolation.
- Tests and verification use temporary paths and isolated `LOCALAPPDATA`, make no live network calls, persist no secrets, and do not touch production data.
- Planning, implementation, testing, evidence, and auditing do not modify LLM/agent/application model, provider, base URL, API key, reasoning effort, context size, or compression threshold.

## Dependencies and parallelism notes

- The completed Wave 3 nodes N14, N15, N16, and N18 are regression baselines, not functional N17 dependencies. N17 must preserve their 25-test behavior, schema v12/source contracts, strict parsing, read-only snapshot evidence, and MCP default-deny behavior.
- N17 functionally reuses N12 job fencing, N13 retry separation, N14 strict job-id behavior, and N8 selected-brain routing. It does not depend on N19 or any later node.
- Once the process note authorizes implementation, disjoint slices may cover job/message storage, handler/MCP validation, and focused migration/test evidence. `src/qbrain/ops/handlers.cpp`, `src/qbrain/mcp/server.cpp`, `src/qbrain/storage/migrate.cpp`, `tests/test_main.cpp`, CMake, and the build script are shared hot files; the parent owns sequencing, merge review, full builds, evidence, both audit gates, status, and ledger.
- No subagent may mark N17 approved/done or author a PASS audit. A Claude Code outcome-audit PASS is mandatory after implementation and native evidence.

## Explicit no-N30 and no-later-wave rule

- N30 is not a dependency, plan, coordinator, evidence container, audit substitute, or follow-up for N17. No `N30-*` file is created, read as a gate, or emitted by this node.
- N19 and every later capability keep separate plans, plan audits, implementations, evidence, outcome audits, and ledger ownership. N17 completion does not authorize starting them inside this node.
- No commit or push is part of this plan unless the human user separately requests it.

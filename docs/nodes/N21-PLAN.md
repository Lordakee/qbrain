# N21 Plan - Source-scoped Takes and Bounded Calibration Bootstrap

**Status**: draft
**Depends on**: Directly on N2/N2.5 (page ownership, registered canonical sources, and source authorization), N7 (authenticated MCP and write-default-deny), N8 (selected physical brain routing), N10 (active facts and page ownership), N15 (current schema v12 and source-scoped operation contract), and N20 (`schema::active_pack_id` for the descriptive profile). N11 is the native Windows evidence baseline. N17-N19 are regression baselines for migration integrity, strict arguments, structured errors, selected/decoy isolation, and deterministic read evidence; they are not functional prerequisites. N22, N23, and later nodes are not dependencies.
**Plan audit**: pending
**Outcome audit**: pending
**Wave**: Wave 5; N20 must complete its own gate before N21 implementation because N21 consumes its active-pack helper. N22 and N23 may progress independently, subject to the migration/test hot-file serialization below.

**Process note**: The short historical N21 plan, its 2026-07-29 retrospective PASS artifacts, the existing implementation, the current ledger claims, and old test results are context only. They do not approve this plan and cannot satisfy its outcome gate. No N21 production, migration, test, verifier, evidence, status, or ledger change may begin until a fresh node-specific Claude Code plan audit returns PASS and this plan is changed to `approved`.

**Pre-implementation schema gate**: After plan-audit PASS and approval, but before any N21 implementation or evidence-input edit, run the already-built current `build\cl\qbrain.exe doctor --json` against a unique temporary selected brain under an isolated temporary `LOCALAPPDATA`. The command must exit zero, parse as JSON with `ok == true`, and report integer `schema_version == 12`. Capture `docs/nodes/n21-evidence/PRE-IMPLEMENTATION-SCHEMA-GATE.json` with UTC start/end, exact command and exit code, approved-plan and plan-audit SHA-256, binary SHA-256, a sorted hash/byte manifest of every existing scoped N21 production/test input, an explicit absent-before list for planned new test/verifier/evidence files, the parsed doctor result, proof that production `%LOCALAPPDATA%\Qbrain` was not accessed, and proof that the temporary root was removed. If the gate fails or reports another version, return this plan to `draft`, revise the migration assumptions, and obtain another Claude Code plan-audit PASS before editing code.

## Goal

Re-verify and correct the five existing N21 ledger operations as one bounded, honest Qbrain takes capability:

1. Store and read active takes within one selected physical brain and one authorized canonical source.
2. List takes by optional entity and search take bodies/entities by a literal bounded query with deterministic output.
3. Return a deterministic descriptive scorecard over active take weights.
4. Retain the historical Qbrain `takes_calibration` behavior as an explicitly named, idempotent facts-to-takes bootstrap write, protected by MCP write-default-deny.
5. Return a read-only descriptive calibration-profile stub tied to the N20 active schema pack and the selected source, without claiming resolved-outcome calibration.

This is a deliberately bounded Qbrain subset. It does not claim upstream holder/token visibility, fenced Markdown take authoring, take update/supersede/resolve workflows, federated multi-source reads, pg_trgm/vector take search, resolved-bet accuracy/Brier/partial-rate metrics, a persisted learned calibration profile, or an upstream-compatible calibration curve. Those absent capabilities must not appear in handler descriptions, evidence, or ledger notes.

## Historical baseline and correction scope

The current code is evidence of what must be re-verified or corrected, not evidence of conformance:

- Schema v9 has a brain-global `takes` table with no `source_id` or fact provenance; existing rows therefore cannot be isolated by canonical source.
- `takes_list` and `takes_search` use permissive `arg_int`, have no maximum bound, accept unvalidated text, and do not reject unexpected arguments.
- `takes_search` treats SQL `%` and `_` as wildcards and maps an empty query to an unfiltered list.
- `takes_scorecard` queries the database directly in the handler without source filtering or an explicit tie-break.
- `takes_calibration` mutates by copying facts but is registered `local_only=false`, so current remote dispatch can bypass the required default-deny rule; repeated calls also duplicate rows.
- `get_calibration_profile` is an unscoped `{version,note,active_pack}` stub.
- `tests/test_n20_23.cpp` proves only one helper insert, non-empty list/search, and one successful registry list call; it does not exercise three ledger rows, argument contracts, write denial, isolation, migration, rollback, or no-mutation guarantees.

The corrective implementation may preserve compatibility only where it does not conflict with this approved contract. Historical permissive, unscoped, duplicate-producing, or remotely writable behavior is not compatibility that N21 must retain.

## Ledger rows to reconcile after outcome PASS

The ledger currently counts these rows as implemented. They remain historical claims until this refreshed node completes PLAN -> PLAN-AUDIT PASS -> implementation/evidence -> HARD-AUDIT PASS. After outcome PASS, reconcile only these five rows and the N20-N23/N21 note; do not increment the current operation total merely because the rows already exist.

| op | scope/locality | exact N21 subset |
|----|----------------|------------------|
| `takes_list` | Read, remote-capable subject to source authorization | Deterministic active-take listing for one canonical source, optionally filtered by exact entity slug |
| `takes_search` | Read, remote-capable subject to source authorization | Deterministic literal substring search over active take body/entity fields in one canonical source |
| `takes_scorecard` | Read, remote-capable subject to source authorization | Per-kind active count and mean stored score for one canonical source; not resolved-outcome accuracy/Brier |
| `takes_calibration` | Write, local-only registration; remote requires explicit allow-write and source authorization | Atomic, idempotent promotion of active page-owned N10 facts from one source into takes; not the upstream read-only calibration curve |
| `get_calibration_profile` | Read, remote-capable subject to source authorization | Deterministic descriptive take-weight profile plus N20 active-pack id and an explicit `outcome_calibration_available=false` marker |

No N20 schema-pack row, N22 code-intel row, N23 Chronicle row, helper API, migration helper, or later-node operation belongs to N21.

## Normative shared contract

1. Every operation acts only on `ctx.brain`, the N8-selected physical brain. It never opens, enumerates, or falls back to another brain. A second physical decoy brain remains byte-equivalent on every N21 path.
2. Every operation accepts optional `source_id`; omission alone means canonical `default`. An explicitly supplied empty source is invalid. Canonicalization, registered-source existence, Windows-safe validation, and N15/N18/N19 remote allow-list authorization complete before any data query or write. Reads never call `ensure_source` or another create-on-read path.
3. Local callers may address any registered canonical source. Remote callers may address `default`; a non-default source must be present case-insensitively in `mcp.allowed_sources`. Invalid, unknown, or unauthorized sources fail closed with `invalid_source`, `source_not_found`, or `source_not_allowed` before enumeration or mutation.
4. All five operations are excluded from MCP ambient `QBRAIN_SOURCE` injection. Omitted `source_id` therefore means `default` identically for local registry and real MCP calls.
5. `takes_list`, `takes_search`, `takes_scorecard`, and `get_calibration_profile` remain `Scope::Read`, `local_only=false`, and callable while writes are disabled. `takes_calibration` remains the bounded Qbrain bootstrap `Scope::Write`, is registered `local_only=true`, and a remote call is denied before handler/database access unless `--allow-write` is explicit. Allow-write never widens source authorization.
6. Advertised schemas are `type=object` with `additionalProperties=false`. The MCP layer rejects non-object arguments, unknown fields, wrong JSON types, signed/floating numeric values, arrays, objects, booleans, and null before string coercion or dispatch. Local registry calls independently reject unknown fields and strictly parse their string-form arguments.
7. A numeric field consumes an entire unsigned base-10 ASCII decimal. Defaults apply only when omitted. Empty strings, signs, whitespace, decimals, suffixes, and overflow return `invalid_argument`; syntactically valid zero clamps to the minimum and values above the maximum clamp to the maximum.
8. All supplied text must be valid UTF-8. `entity_slug` is 1..256 bytes when supplied; `query`/`q` is 1..4096 bytes after rejecting ASCII-whitespace-only input. New internal take writes accept `entity_slug` 1..256, `kind` 1..64, and `body` 1..16384 bytes and require a finite score in `[0,1]`. Bounds are byte bounds and truncation is never used for accepted write input.
9. Every failure returns `ok=false`, nonzero `exit_code`, and bounded valid JSON shaped as `{"error":{"code":"...","field":"...","message":"..."}}`. Errors do not echo raw take text, query text, source ids, paths, config, tokens, provider/model information, or secrets. Database exceptions become `database_error` at the operation boundary.
10. SQL values are bound. Membership and limiting happen after selected-brain, canonical-source, `active=1`, and operation-specific predicates. Explicit ordering prevents SQLite encounter order or unordered containers from deciding results. Repeated reads on an unchanged database produce byte-identical JSON and text.
11. Output take bodies are valid UTF-8 and capped at 4096 bytes on a code-point boundary with an explicit `[truncated]` marker. Other returned text fields are capped at their accepted bounds. Malformed legacy database text is replaced with U+FFFD without leaking adjacent bytes.
12. Read success, empty result, invalid input, unknown/unauthorized source, database error, and denied write paths create no source, take, fact, page, job, config entry, file, network, provider, or access-time state. Empty reads succeed with the exact empty shape defined below.

## Schema and migration contract

1. Add one forward migration, schema v13. Do not rewrite the historical v9 migration or insert a marker without completing the data change.
2. Transactionally rebuild `takes` to preserve every existing `id`, `entity_slug`, `kind`, `body`, `score`, `active`, and `created_at`, backfill legacy rows to `source_id='default'`, and add nullable `origin_fact_id`. Preserve `AUTOINCREMENT` behavior and the next-id sequence.
3. The v13 table has the historical columns plus `source_id TEXT NOT NULL DEFAULT 'default'` and `origin_fact_id INTEGER NULL`; it has `FOREIGN KEY(source_id) REFERENCES sources(id) ON DELETE CASCADE` and `FOREIGN KEY(origin_fact_id) REFERENCES facts(id) ON DELETE SET NULL`. Legacy rows keep `origin_fact_id=NULL` because provenance cannot be invented.
4. Replace the old v9 indexes with exact source-aware indexes: `idx_takes_source_entity_active_id(source_id,entity_slug,active,id DESC)`, `idx_takes_source_active_kind(source_id,active,kind)`, and partial unique `idx_takes_source_origin_fact(source_id,origin_fact_id) WHERE origin_fact_id IS NOT NULL`. Search correctness must not be claimed from an index that cannot accelerate a leading-wildcard query.
5. `storage::check_schema_integrity` must require schema >=13 and detect the exact takes table columns, `AUTOINCREMENT`, both foreign keys/actions, and all three index names/column/order/partial-unique shapes. A current-version damaged table/index/FK is reported, not silently repaired.
6. Fresh creation through v1..v13, populated v12->v13 migration, repeated current-version application, reopen, and failure rollback are mandatory. Injected failures before copy completion and before the v13 marker must leave the complete v12 schema/data/sequence unchanged and permit a later successful retry.
7. Prior N17/N19 tests that assert the then-current latest version `12` may be updated only to recognize the new latest version while retaining their exact v8/v12 historical fixture and operation contracts. Historical plans, audits, and evidence are immutable.
8. If the pre-implementation gate is not clean v12, the proposed DDL cannot preserve a populated fixture, or a different schema shape is required, stop and return this plan to `draft` for a fresh Claude plan audit. Do not improvise a migration during implementation.

## Operation contracts

### `takes_list`

1. Accept only `source_id`, optional `entity_slug`, and optional `limit`. Omitted entity means all active takes in the selected source; explicitly supplied empty entity is invalid.
2. `limit` defaults to 50 and clamps to 1..200 under the shared strict parser.
3. Filter `source_id`, `active=1`, and optional byte-exact `entity_slug` in bound SQL before limit. Order by `id DESC`.
4. Return an array of exact rows `{id,source_id,entity_slug,kind,body,score,created_at}`. `score` is a JSON number when finite and JSON `null` for a preserved non-finite legacy value. No row returns a local path, page body, config, fact body beyond the stored take body, or another source's value.

### `takes_search`

1. Accept only `source_id`, canonical `query`, compatibility alias `q`, and `limit`. One nonblank query is required. If both aliases are present, identical bytes are accepted and different values fail on `query`.
2. `limit` defaults to 50 and clamps to 1..200. Query UTF-8 and size validation completes before SQL.
3. Search is a literal ASCII-case-insensitive substring over `body` and `entity_slug`; `%`, `_`, and `\` are ordinary query characters, not SQL wildcard syntax. Do not fall back to unfiltered listing for an empty/invalid query.
4. Apply source and active predicates before search and limit; order by `id DESC`. Return the same exact row shape as `takes_list`. No vector, embedding, reranker, LLM, provider, or network path is used.

### `takes_scorecard`

1. Accept only optional `source_id`.
2. Aggregate only active rows in that source. Return `{source_id,active_takes,invalid_score_count,by_kind}` where `by_kind` is an array of exact `{kind,count,avg_score}` rows ordered by bytewise `kind ASC`.
3. `active_takes` includes all active rows; `invalid_score_count` counts preserved legacy rows whose score is non-finite or outside `[0,1]`. Per-kind `avg_score` is computed only from finite in-range scores and is JSON `null` when a kind has none. Empty input returns zero counts and `[]`.
4. This is a descriptive weight scorecard only. It does not label a stored score correct/incorrect, calculate Brier/accuracy/partial-rate, or claim a resolved outcome.

### `takes_calibration`

1. Accept only optional `source_id` and optional `limit`. `limit` defaults to 50 and clamps to 1..200.
2. This is the historical Qbrain facts-to-takes calibration bootstrap and therefore remains a write. Select only active facts whose non-null `page_id` joins an active page in the authorized source, ordered by fact id descending before limit. Standalone facts with no page/source provenance are not promoted.
3. In one immediate transaction, insert a take with `source_id`, `origin_fact_id`, fact `entity_slug`, `kind='fact'`, body `predicate || ': ' || object_text`, score `0`, and a fresh UTC timestamp only when that source/fact provenance is absent. The partial unique index is the concurrency/idempotence authority.
4. Return exactly `{source_id,considered,promoted,already_present}`. A second unchanged call promotes zero. Concurrent calls produce at most one take per `(source_id,origin_fact_id)` and report counts consistent with committed rows.
5. Validation, authorization, default write denial, transaction failure, bounds failure, and missing-source paths make no row or sequence change. The operation does not edit/deactivate facts or pages, create a source, or call a provider.

### `get_calibration_profile`

1. Accept only optional `source_id` and perform no initialization.
2. Return exactly `{version,profile_kind,source_id,active_pack,active_takes,invalid_score_count,mean_score,by_kind,outcome_calibration_available}`. `version` is integer `1`, `profile_kind` is `descriptive_take_weights`, `active_pack` is the read-only N20 active pack id from the selected brain, `by_kind` has scorecard ordering/semantics, and `outcome_calibration_available` is always `false` for this bounded node.
3. `mean_score` is the mean of finite in-range active scores or JSON `null` when none exist. Empty input remains a successful deterministic profile with zero/empty/null values.
4. The handler must not auto-create a pack, write config, synthesize a Brier/accuracy claim, expose a path, or imply a persisted learned profile.

## Deliverables

1. `src/qbrain/storage/migrate.cpp` and `include/qbrain/storage/database.hpp` only if declarations are required: v13 transactional migration plus exact takes integrity checks described above.
2. `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`: source-aware take/provenance structures and bounded helpers for insert, list, literal search, scorecard/profile data, and atomic idempotent fact promotion. Handler code must not contain ad hoc unscoped takes SQL.
3. `src/qbrain/ops/handlers.cpp`: strict allowed-field/text/limit/source validation, structured exception mapping, bounded UTF-8 rendering, exact five handlers and response shapes, deterministic descriptions, four Read registrations, and one `takes_calibration` Write registration with `local_only=true`.
4. `src/qbrain/mcp/server.cpp`: exact typed maps for `takes_list={source_id:string,entity_slug:string,limit:unsigned-integer}`, `takes_search={source_id:string,query:string,q:string,limit:unsigned-integer}`, `takes_scorecard={source_id:string}`, `takes_calibration={source_id:string,limit:unsigned-integer}`, and `get_calibration_profile={source_id:string}`; exclude all five names from ambient-source injection.
5. `tests/test_n21.cpp`: dedicated N21 operation, migration, isolation, strict-argument, real-MCP, exact-delta, idempotence/concurrency, UTF-8/boundary, and no-mutation matrix. Register it in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`. Preserve the historical minimal checks in `tests/test_n20_23.cpp` unless a signature correction requires a narrow update.
6. Narrow latest-schema regression updates in `tests/test_n17.cpp` and `tests/test_n19.cpp`, if required by v13, without weakening their v8 shape, N17 job behavior, N19 live-version/counter, or selected/decoy guarantees. No historical audit or evidence file is rewritten.
7. `scripts/n21-verify.ps1` and `docs/nodes/n21-evidence/`: fail-closed pre-gate ingestion, production/test build logs, two complete suite logs, focused runtime/MCP evidence, migration/schema dumps, selected/decoy snapshots, exact write deltas, operation schemas, and hash-bound `EVIDENCE-MANIFEST.json` plus factual `VERIFY-REPORT.md`. Evidence never supplies an audit verdict.
8. A fresh node-specific Claude Code outcome audit against the approved plan and captured native evidence. Only after PASS with no open P0/P1 may the parent set this plan to `done` and reconcile exactly the five ledger rows.

## Tests and evidence

All commands run in native Windows 11 PowerShell with MSVC x64 and `/std:c++20`. WSL and Docker are not build, test, verifier, migration, or runtime dependencies.

1. Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1`; record command, environment/toolchain identity, exit code, log SHA-256, and `build\cl\qbrain.exe` SHA-256.
2. Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1`, then execute `build\cl\qbrain_tests.exe` a second time without rebuilding. Both full suites must register at least the current 26-test baseline plus dedicated `n21` (at least 27 total), report every registered test PASS and zero FAIL, and retain N1-N20 regressions.
3. Migration matrix: fresh v1 path, populated v12 with non-default sources and legacy takes, already-current v13, reopen, repeated migration, failure injection, damaged-v13 table, each missing index, each wrong index shape, each missing/wrong FK, and a preserved legacy `sqlite_sequence`. Compare exact schema/data/sequence snapshots before/after each no-op or failure.
4. Selected/source matrix: selected and decoy physical brains, each with `default` and `team_a`, distinct takes/facts, inactive rows, deleted-page facts, wildcard-text bodies, invalid legacy scores, and equal sort keys. Direct SQL supplies expected rows/counts; every response is compared field-for-field, not merely for non-emptiness or whole-object inequality.
5. Strict argument matrix through both local registry and real NDJSON MCP: non-object arguments, unknown fields, wrong JSON types, null, signed/float/overflow limits, zero/above-cap clamping, omitted versus explicit-empty fields, malformed/oversize UTF-8, query-alias conflict, literal `%`/`_`/`\`, invalid/unknown/unauthorized sources, and database exceptions. Assert exact structured error codes/fields and bounded messages.
6. Read-only matrix: all successful, empty, malformed, unauthorized, and database-error calls to the four reads preserve full logical snapshots of selected and decoy schema, every application table, and `sqlite_sequence`. Repeated unchanged reads have byte-identical JSON/text and exact deterministic ordering.
7. Write authorization/delta matrix: remote `takes_calibration` is denied with `allow_write=false` before handler/database access; valid remote write requires explicit allow-write and source authorization. One success adds only the declared takes rows and sequence delta in one selected source; facts/pages/config/other sources/decoy remain identical. A repeat adds zero, and injected rollback adds zero.
8. Concurrency matrix: race two connections promoting the same fact set. Regardless of busy scheduling, committed state contains exactly one take per source/fact provenance; reported successes and retries reconcile to the exact committed delta, with no duplicate or cross-source row.
9. Profile/scorecard matrix: empty, one-kind, multiple-kind, inactive, invalid-score, default/team, and selected/decoy fixtures prove exact counts, null/mean behavior, bytewise kind order, active-pack selection, and the explicit false outcome-calibration marker.
10. Runtime smoke uses unique temporary database paths and isolated temporary `LOCALAPPDATA`; production `%LOCALAPPDATA%\Qbrain` remains untouched and temporary roots are removed. No live network/provider call is permitted.
11. The final manifest hashes every scoped deliverable, approved plan/audit, pre-implementation gate, logs, binaries, schema dumps, snapshots, and reports. It records the exact HEAD without committing, zero N21 changes outside declared files, no secrets, no protected model/provider/baseURL/key/reasoning/context/compression change, and no commit or push.

## Acceptance assertions (falsifiable)

| # | assertion | required evidence |
|---|-----------|-------------------|
| 1 | A fresh approved-plan-bound schema gate predates N21 edits and proves isolated native doctor success on schema v12; a missing/failed/different-version gate blocks implementation. | Gate JSON with hashes, timestamps, parsed result, isolation, and cleanup |
| 2 | v13 preserves every populated v12 takes value/id and sequence, backfills legacy rows only to canonical `default`, adds nullable provenance without invention, and is atomic, idempotent, reopen-safe, and retryable after injected failure. | Exact migration schema/data/sequence snapshots and failure matrix |
| 3 | Integrity checking detects the exact v13 takes column, AUTOINCREMENT, source/fact FK, and three index shapes on current-version damaged databases without silent repair. | Per-damage integrity results and unchanged snapshots |
| 4 | All five registry and MCP schemas expose only their declared typed fields with `additionalProperties=false`; local calls enforce the same allowed-field and whole-value numeric rules. | Registry schema dump plus local/real-MCP malformed matrix |
| 5 | Every N21 call resolves one registered canonical source before data access, obeys remote allow-list rules, ignores no supplied source, receives no ambient source injection, and never observes another source or physical brain. | Selected/decoy x default/team exact matrix and denial evidence |
| 6 | `takes_list` returns only active requested-source rows, applies exact optional entity filtering before the effective 1..200 limit, orders by id descending, and returns the exact seven-field bounded row shape. | Direct-SQL expected/actual rows and boundary calls |
| 7 | `takes_search` requires one valid query, handles aliases deterministically, treats SQL wildcard characters literally, performs no AI/network path, and returns only requested-source active matches in deterministic order. | Literal-query fixtures, provider/network sentinel, exact rows |
| 8 | `takes_scorecard` returns deterministic per-kind active counts/valid-score means plus invalid-score count for only one source and makes no correctness/Brier claim. | Direct aggregate comparison for empty/mixed/invalid fixtures |
| 9 | `get_calibration_profile` returns the exact descriptive shape, selected-brain N20 active pack, source-scoped aggregates, null empty mean, and `outcome_calibration_available=false` without mutation. | Exact JSON comparison and before/after snapshots |
| 10 | Remote `takes_calibration` is default-denied before access; allowed/local calls promote only active page-owned facts in one source, atomically and idempotently, with exact response/deltas and no source/page/fact/config mutation. | Denial trace, source matrix, exact delta and rollback evidence |
| 11 | Concurrent promotion never creates more than one take per `(source_id,origin_fact_id)` and all successful/retried response counts reconcile with committed rows. | Two-connection schedules and final unique-row snapshot |
| 12 | Invalid text/types/bounds/source/auth/database paths return bounded structured errors and preserve selected/decoy schema, data, and sequence state. | Error table plus full logical before/after snapshots |
| 13 | Four reads and every denied/failed write are logically read-only; successful promotion has only the declared selected-source takes/sequence delta. | Complete application-table snapshot manifest |
| 14 | Native production/test builds exit zero and two full suites each show at least 27 PASS, zero FAIL, dedicated N21 coverage, `/std:c++20`, x64 MSVC, isolated data roots, and no live network. | Hash-bound build/test/runtime logs |
| 15 | Outcome evidence states the bounded Qbrain semantics and explicit upstream exclusions; it does not relabel bootstrap as a calibration curve or descriptive weights as resolved accuracy. | Handler descriptions, report text, and ledger diff |
| 16 | Only the five named N21 ledger rows/one N21 note change after separate Claude outcome PASS; the already-counted operation total does not increase. | Post-audit ledger diff |
| 17 | N21 creates no later-node implementation, no undeclared schema version, no third-party dependency, no secret, no protected model configuration change, no commit, and no push. | Scoped diff and evidence manifest |

## Rollback

- Keep N21 ledger rows unreconciled and the plan not `done` unless the fresh outcome audit passes with no open P0/P1.
- Before deployment rehearsal, back up a populated v12 database. Migration failure must roll back in place; restore the backup rather than hand-editing version markers or dropping user takes. There is no schema downgrade command.
- Revert v13 migration/integrity, take helpers, handlers/MCP maps, tests, and evidence wiring as one coherent slice. Do not leave source-aware code running against the v9 shape or vice versa.
- If source ownership or idempotence cannot be proven, keep `takes_calibration` unavailable and retain MCP writes disabled. Do not fall back to brain-global promotion.
- If a read cannot preserve source/brain isolation, fail closed rather than returning an unscoped partial result.

## Security notes

- Takes and fact-derived bodies can contain sensitive claims. Source authorization precedes count/search/profile/promotion work; errors, manifests, and logs use synthetic fixtures and never record production bodies, tokens, paths, keys, or config values.
- `takes_calibration` is the only N21 write and remains MCP default-deny plus explicit source authorization. `allow_write` does not authorize an unregistered or disallowed source.
- Valid UTF-8, exact byte bounds, strict JSON types, whole-value numeric parsing, literal bound SQL search, bounded result counts, bounded rendering, deterministic errors, and transactionality limit parser, storage, injection, and output abuse.
- `origin_fact_id` is provenance/idempotence data only. Fact text is not executed, used as SQL, interpreted as a path, sent to a model/provider, or treated as authorization.
- Tests and evidence use temporary selected/decoy brains and isolated `LOCALAPPDATA`, make no live network calls, and persist no secrets.
- Planning, implementation, evidence, and auditing must not modify any agent/application model name, provider, base URL, API key, reasoning effort, context size, or compression threshold.

## Dependencies and parallelism notes

- N20 must complete before N21 implementation because `get_calibration_profile` consumes its active-pack helper. N22 and N23 can plan, audit, and implement disjoint code in parallel.
- N21 owns the sole Wave 5 schema bump. Changes to `src/qbrain/storage/migrate.cpp`, `src/qbrain/ops/handlers.cpp`, `src/qbrain/mcp/server.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` are shared hot files and must be serialized by the parent. N22/N23 must not independently edit or assume a competing schema version.
- After approval only, disjoint N21 slices may cover migration/integrity, Brain take helpers, handler/MCP validation, and focused tests/evidence. The parent owns pre-gate verification, merge review, schema ordering, full builds, two complete suites, evidence publication, both audit gates, status, and ledger.
- No subagent may mark N21 approved/done, write a PASS audit, update the ledger early, weaken a failed gate, or treat another node's audit as N21 evidence.

## Explicit exclusions

- N21 does not implement upstream holder ACLs, resolved-bet outcomes, accuracy/Brier/partial-rate, learned profiles, take lifecycle CLI, semantic search, federated reads, or provider-backed calibration. Ledger notes must retain these boundaries.
- N21 does not plan, depend on, create, or use N30 as a coordinator, gate, evidence container, or follow-up. Later nodes retain their own complete loops.
- No commit or push is part of this plan unless the human user separately requests it.

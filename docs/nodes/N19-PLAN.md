# N19 Plan - Source-scoped Identity, Context, Timeline, and Chronicle Reads

**Status**: done
**Depends on**: Directly on N1, N2, N2.5, N3, N7, N8, N11, N15, N16, and N18 only: N1 read-operation/MCP write-default-deny, N2 active-page and soft-delete semantics, N2.5 canonical source ids and remote source authorization, N3 conservative FTS search, N7 authenticated loopback MCP, N8 selected-brain isolation, N11 native Windows evidence, N15 schema v12/source-scoped Chronicle and thin timeline-page contracts, N16 source-filtered page enumeration, and N18 strict read-argument/source-resolution patterns. N17 and N20+ are not dependencies.
**Plan audit**: PASS (`N19-PLAN-AUDIT.md`, Claude Code, 2026-08-04; corrective revision after outcome FAIL)
**Outcome audit**: PASS (`N19-HARD-AUDIT.md`, Claude Code, 2026-08-04; SHA-256 `d4ee4ad14e3768b5470865f092a783ba0d10b9e9155bfb17c4bd5ce594ad4f24`)
**Wave**: Wave 4; N19 may progress independently of N17 only after each node has its own plan-audit PASS
**Process note**: This is the corrective revision after the first refreshed-plan outcome audit returned FAIL on 2026-08-04. The short 2026-07-26 N19 plan, implementation that already existed before the refreshed loop, the first refreshed implementation/evidence attempt, historical ledger text, and every earlier N19 PASS are context only. They cannot satisfy this revised plan or outcome gate. No further N19 production, test, verifier, or runtime-evidence change may begin until this revision receives a fresh node-specific Claude Code plan-audit PASS and is changed to `approved`.
**Pre-corrective-work schema gate**: After that fresh plan-audit PASS and approval, but before any further N19 production, test, verifier, or runtime-evidence edit, run the already-built current `build\cl\qbrain.exe doctor --json` against a unique isolated selected brain. The `doctor` path reaches `Brain::health`, which calls `storage::check_schema_integrity`; require exit zero, valid JSON `ok == true`, and integer `schema_version == 12`. Capture `docs/nodes/n19-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json` with UTC start/end, exact command, approved-plan and plan-audit SHA-256, binary SHA-256, a sorted SHA-256 manifest of every scoped N19 production/test/verifier input before corrective edits, the parsed result, proof that production `%LOCALAPPDATA%\Qbrain` was not touched, and proof that the temporary root was removed. If the gate fails or reports another version, return this plan to `draft`, revise it to the actual schema/constraints, and obtain another Claude Code plan-audit PASS; do not silently migrate or continue.

## Goal

Re-verify and, where necessary, correct exactly four existing read operations as one bounded N19 node:

1. `get_brain_identity`: identify the selected brain and one authorized canonical source, return source-scoped counters and the live schema version, and omit the local database path from every remote response.
2. `volunteer_context`: return bounded zero-LLM context from either conservative source-scoped search or deterministic recent active pages.
3. `get_timeline`: list active `type=timeline` pages in one source as the Qbrain thin timeline-page subset established by N15.
4. `volunteer_chronicle`: return a bounded recent page-activity orientation by delegating to the strict source-scoped N15 `chronicle_since` contract.

N19 is a Qbrain capability subset. It does not claim upstream per-page timeline rows/date-window filtering, confidence-gated conversation-window volunteering, prior-context suppression, feedback statistics, ontology resolution, diary provenance, narratives, semantic salience, or full gbrain Chronicle/context parity. It performs no LLM, embedding, rerank, provider, network, filesystem-write, or database-write work.

## Ledger rows to reconcile after outcome PASS

The four ledger rows are historical claims until N19 completes the refreshed PLAN -> PLAN-AUDIT PASS -> implementation/evidence -> HARD-AUDIT PASS loop. Only these rows and the N19 ledger note may be reconciled after the outcome audit passes:

| op | scope and locality | exact N19 subset |
|----|--------------------|------------------|
| `get_brain_identity` | Read, remote-capable subject to source authorization | Selected brain id, authorized canonical source id, live schema version, source-scoped page/chunk/link counters, and local-only actual database path |
| `volunteer_context` | Read, remote-capable subject to source authorization | Zero-LLM conservative FTS pointers for a query, or recent active-page pointers when the query is omitted/blank, from one source |
| `get_timeline` | Read, remote-capable subject to source authorization | Active `type=timeline` pages from one source; the N15 thin timeline-page model, not upstream page-attached timeline rows |
| `volunteer_chronicle` | Read, remote-capable subject to source authorization | N15-style inclusive UTC page activity from one source, with an explicit `since` or a bounded seven-UTC-calendar-day default |

No N17 job operation, N20+ schema/ontology/takes/code/chronicle operation, or other ledger row belongs to N19.

## Normative shared contract

1. All four operations are `Scope::Read`, `local_only=false`, and callable while MCP writes are disabled. N19 must not weaken the existing global MCP write-default-deny rule. `allow_write=true` has no effect on N19 source authorization and grants no wider read scope.
2. Each operation accepts one optional `source_id`. Omission alone means canonical `default`; an explicitly supplied empty value is invalid. Canonicalization, Windows-safe validation, remote authorization, and registered-source existence checks use the established N2.5/N15/N18 contract and complete before any identity count, search, timeline, or Chronicle data query. Reads must never call `ensure_source` or another create-on-read path.
3. Local callers may read any registered canonical source. Remote callers may read `default`; a non-default source must appear case-insensitively in `mcp.allowed_sources`. Invalid, unknown, and unauthorized values fail closed with `invalid_source`, `source_not_found`, or `source_not_allowed` and do not reveal whether a page, slug, title, path, or row exists in the denied scope.
4. The four operations are excluded from MCP ambient-source injection. When `source_id` is omitted, the operation uses `default` even if a process-level or transport-level ambient source is configured. This makes the advertised JSON schema, local registry behavior, and MCP behavior identical.
5. Operation schemas use `type=object` and `additionalProperties=false`. Runtime MCP validation rejects a non-object arguments value, unknown fields, non-string string fields, signed/floating numeric values, arrays, objects, booleans in string positions, and null supplied as a value. Local registry calls independently reject unknown fields and strictly parse their string-form arguments.
6. A supplied numeric value must be a complete unsigned base-10 ASCII decimal string. Defaults apply only when the field is omitted. Explicit empty strings, signs, whitespace, decimals, suffixes, and parse overflow return `invalid_argument` before data enumeration. A syntactically valid zero clamps to the operation minimum; a syntactically valid value above the maximum clamps to the maximum.
7. Every failure returns `ok=false`, nonzero `exit_code`, and valid bounded JSON shaped as `{"error":{"code":"...","field":"...","message":"..."}}`. Error text does not echo raw untrusted input, local paths, configuration values, tokens, provider/model information, page bodies, or cross-source identifiers. Database exceptions are caught at the operation boundary and become a structured `database_error`.
8. Successful row-producing operations return JSON arrays; an empty result is successful `[]`. Every row contains canonical `source_id`. Display text is produced from the same rows in the same order. Titles and snippets are sanitized to valid UTF-8 with malformed input replaced by U+FFFD; titles are capped at 512 bytes and snippets at 512 bytes on complete code-point boundaries with an explicit truncation marker.
9. Ordering and limiting happen after the selected-brain, canonical-source, active-page, and operation-specific predicates. SQL inputs are bound. Repeated calls against an unchanged database must produce byte-identical JSON and text; SQLite encounter order or unordered-container iteration must not decide membership or order.
10. All success and failure paths are logically read-only. They create no source, page, version, chunk, link, fact, ingest row, job, config entry, access timestamp, file, or network state. They perform no provider/API-key resolution that can initiate a request.

## Normative operation contracts

### `get_brain_identity`

1. Accept only optional `source_id`; any other field is invalid. Resolve and authorize the source before counting rows.
2. Return exactly these remote fields: `brain_id`, `source_id`, `schema_version`, `pages`, `chunks`, `links`, and `embedded_chunks`. A local call additionally returns `db_path`; a remote call omits the key entirely rather than returning `null`, a basename, a redacted placeholder, or another path-derived value.
3. `brain_id` is the selected N8 brain id. `db_path`, when local, is the actual UTF-8 path of the database opened by that `Brain` instance, including `open_at` test databases; it must not be recomputed from a global/default brain root when the selected brain was opened elsewhere.
4. `schema_version` comes from the current schema-integrity/version query and is not hard-coded to 12. N19 is implemented and tested on the current schema v12, but the payload reports the actual selected database value.
5. Counter semantics are source-scoped and explicit: `pages` is the number of active pages (`deleted_at IS NULL`) in the requested source; `chunks` is the number of stored `content_chunks` rows owned by pages in that source; `embedded_chunks` is the subset with a non-NULL embedding; and `links` is the number of stored link rows whose `links.source_id` is the requested source. These counters never aggregate another source or another brain.
6. The identity packet contains no job counts, config dump, allowlist, provider/model/base URL/API key, update check, environment value, host name, username, or filesystem metadata. It performs no update/network check.

### `volunteer_context`

1. Accept only `source_id`, optional canonical `query`, legacy compatibility alias `q`, and optional `limit`. If both `query` and `q` are supplied with different non-empty byte strings, return `invalid_argument` on `query`; if both are supplied with identical byte strings, accept them as equivalent to one query parameter. An omitted, empty, or ASCII-whitespace-only effective query selects recent-page mode. A non-empty query must be valid UTF-8 and at most 4096 bytes; oversize or malformed input fails before search.
2. `limit` defaults to 8 and clamps to 1..50 under the shared strict parser.
3. Query mode is a zero-LLM conservative FTS read. It sets the canonical source on the search options, disables vector search, reranking, tokenmax, and provider calls, and never falls back to an unscoped search. Return rows shaped exactly as `{source_id, slug, title, snippet, score}`.
4. Query-mode candidate and final ordering is deterministic: FTS rank/score descending, then slug bytewise ascending as the visible tie-break. The SQL candidate query also has a bytewise slug tie-break before its candidate limit so equal-score candidates at the boundary cannot vary. Shared hybrid behavior outside this tie-break must remain compatible with N3/N12 regression tests.
5. Recent mode calls `Brain::list_pages_for_source` or an equivalent source-filtered SQL helper, enumerates active pages with the source predicate in SQL before limiting, and orders by `updated_at DESC, id DESC`. Return rows shaped exactly as `{source_id, slug, title, type, updated_at}`.
6. Neither mode returns a page body, frontmatter, local path, source allowlist, raw embedding, provider result, or another source's pointer. A valid query/recent request with no matching rows returns `[]`.

### `get_timeline`

1. Accept only optional `source_id` and `limit`. `limit` defaults to 50 and clamps to 1..200 under the shared strict parser.
2. Query active pages whose canonical source matches and whose `type` is byte-exact `timeline`. Both predicates are in bound SQL before ordering and limiting; fetching a broad page window and filtering `type` or source in memory is forbidden.
3. Order by effective activity timestamp (`max(created_at, updated_at)`) descending and page id descending. Return rows shaped exactly as `{source_id, slug, type, title, created_at, updated_at, effective_at}`; `type` is always `timeline`.
4. This operation consumes the N15 `add_timeline_entry` thin-page model. It does not accept a page slug, `after`, `before`, `since`, or `until`, and it does not claim upstream per-page attached timeline entries. It never returns page bodies.

### `volunteer_chronicle`

1. Accept only optional `source_id`, optional `since`, and optional `limit`. `limit` defaults to 50 and clamps to 1..200 under the shared strict parser.
2. When `since` is supplied, reuse the N15 `chronicle_since` validation and normalization unchanged: accept only a real `YYYY-MM-DD` or `YYYY-MM-DD[T ]HH:MM:SSZ`; date-only means `00:00:00Z`; reject offsets, partial strings, impossible dates/times, trailing data, and malformed UTF-8. Do not add a second looser date parser.
3. When `since` is omitted, use a pure/testable UTC date helper that accepts an optional fixed-now UTC timestamp only for tests; production passes no override and uses the real UTC now. It selects the seven UTC calendar dates ending with the current UTC date: the inclusive boundary is `00:00:00Z` six calendar days before today. Normal weeks, month-end, year-end, and leap-day transitions must be correct. There is no fallback to `2000-01-01`, all history, or another source when the bounded window is empty.
4. Delegate the final read to the source-aware N15 `Brain::chronicle_since(..., source_id)` path. Preserve its active-page filter, inclusive created-or-updated boundary, `effective_at DESC, id DESC` ordering, and 1..200 bound rather than duplicating Chronicle SQL in the handler.
5. Return rows shaped exactly as `{source_id, slug, title, created_at, updated_at, effective_at, type}`. This is page-activity orientation only. It does not resolve ontology, entities, facts, diary provenance, narratives, confidence, or semantic relevance.

## Deliverables

1. `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`:
   - a read-only source-scoped identity/count helper with the exact counter semantics above;
   - an accessor or equivalent tracked state for the actual selected database path used by `open`/`open_at`;
   - source- and type-filtered active-page enumeration for N19 recent/timeline reads with predicates before limit and deterministic ordering; recent mode must use `Brain::list_pages_for_source` or an equivalent source-filtered SQL helper with `updated_at DESC, id DESC`;
   - add a testable seven-day UTC-boundary helper in `include/qbrain/util/time_util.hpp` and `src/qbrain/util/time_util.cpp`, equivalent to `utc_seven_day_boundary(std::optional<std::chrono::system_clock::time_point> fixed_now = std::nullopt)`, returning `00:00:00Z` six UTC calendar days before the supplied/current date; production passes `std::nullopt`, while tests inject fixed normal-week, month-end, year-end, and leap-day timestamps;
   - reuse, without weakening, of the existing N15 source-aware Chronicle helper, keeping the three-argument `Brain::chronicle_since(since_iso, limit, source_id)` signature unchanged and calling it directly after source resolution.
2. `src/qbrain/search/hybrid.cpp` (and its header only if required): the smallest deterministic conservative-FTS tie-break change needed for `volunteer_context`, including candidate-boundary and final slug ordering. Existing N3/N12 search modes, vector/rerank behavior, limits, and fail-open contracts must remain regression-compatible.
3. `src/qbrain/ops/handlers.cpp`:
   - shared N19 allowed-field validation, strict limit parsing, query-alias validation, source resolution/authorization, bounded UTF-8 rendering, and structured exception handling;
   - the four exact handlers/output shapes above;
   - replace the current `volunteer_context` recent-mode `list_pages(limit)` call with `Brain::list_pages_for_source(source_id, limit)` after canonical source resolution, preserving `updated_at DESC, id DESC` ordering;
   - all four registrations remain Read/non-local-only with accurate `additionalProperties=false` schemas, defaults, limits, and descriptions that state the bounded Qbrain subset.
4. `src/qbrain/mcp/server.cpp`: add these exact optional typed-argument maps: `get_brain_identity` = `{source_id:string}`; `volunteer_context` = `{source_id:string, query:string, q:string, limit:unsigned-integer}`; `get_timeline` = `{source_id:string, limit:unsigned-integer}`; and `volunteer_chronicle` = `{source_id:string, since:string, limit:unsigned-integer}`. Reject mismatches before dispatch. The four names are currently absent from `uses_ambient_source`; add `get_brain_identity`, `volunteer_context`, `get_timeline`, and `volunteer_chronicle` to its false-return/exclusion list. Do not broaden typed handling or change unrelated operation coercion as an incidental N19 edit.
5. Expand `tests/test_n19.cpp` into the dedicated N19 matrix and retain its registration in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`. Do not remove or weaken any prior test. The selected/decoy by default/team identity matrix must compare every returned counter and live schema version to direct SQL/integrity results in all four cells; broad object inequality is not acceptance evidence. Add focused pure-helper tests where the seven-day UTC boundary requires a deterministic clock/date seam.
6. Maintain `scripts/n19-verify.ps1` and `docs/nodes/n19-evidence/VERIFY-REPORT.md` plus captured production-build, test-build/full-suite, focused runtime, registry/real-MCP smoke, selected/decoy snapshot, and manifest/hash evidence. The verifier must ingest and fail closed on the approved-plan-bound `PRE-CORRECTIVE-SCHEMA-GATE.json`, prove it predates the corrective test/verifier input hashes, and include its hash and parsed result in final evidence. It must not claim to retroactively manufacture the failed first attempt's missing gate. Evidence reports facts and never supplies an audit verdict.
7. No schema migration is planned. The pre-corrective-work gate above must report success and `schema_version == 12` after this revised plan is approved and before corrective edits. N19 must then run against fresh and populated schema v12 databases without altering schema, migration code, or data. If the gate fails, the version is not v12, or corrective work discovers that a schema/index/constraint change is necessary, stop, return this plan to `draft`, add populated-database migration/idempotence/failure/rollback assertions, and obtain a new Claude Code plan-audit PASS before editing schema or migration code.
8. After implementation and evidence, obtain a fresh node-specific Claude Code outcome audit against the approved plan. Only after its PASS may the parent set this plan to `done` and reconcile exactly the four N19 ledger rows.

## Tests and evidence

All commands run on native Windows 11 PowerShell with MSVC x64 and C++20. WSL and Docker are not part of the build, test, verifier, or runtime path.

1. Native production build and full regression suite:
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1`.
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1` and `build\cl\qbrain_tests.exe` as required by the script contract.
   - Record the exact commands/exit codes, Windows edition/architecture, full `cl.exe` version, `/std:c++20`, exact registered count, and every result. The suite must be all PASS, contain the dedicated N19 test, and remain at or above the completed Wave 3 baseline of 25 tests.
2. Schema v12 compatibility matrix:
   - After this revision's plan-audit PASS and approval, and before any corrective implementation/evidence edit, capture the required `PRE-CORRECTIVE-SCHEMA-GATE.json` through the existing `doctor -> Brain::health -> storage::check_schema_integrity` path. Require exit zero, `ok == true`, integer `schema_version == 12`, exact approved-plan/plan-audit/binary/pre-edit input hashes, isolated `LOCALAPPDATA`, no persisted config, and cleanup. A failed result or any other version blocks corrective work and returns this plan to `draft` for revision and a fresh Claude Code plan-audit PASS.
   - A fresh temporary brain opens at schema v12 with all N19 queries available.
   - A populated v12 fixture contains default/team sources, active/deleted pages, content chunks/embeddings, links, timeline pages, N15 ingest rows, jobs, facts, and config sentinels. Every N19 success/failure call leaves schema SQL, version, indexes, sequences, and all rows unchanged.
   - Open the populated fixture a second time and prove N19 requires no migration. If the repository schema advances before N19 implementation, return the plan to `draft` rather than silently changing this assertion.
3. Source and typed-argument matrix:
   - For every operation, exercise omitted/default, mixed-case registered `Team_A`, empty, malformed, reserved, overlength, valid-but-unknown, remote unauthorized, remote authorized, and remote `allow_write=true` unauthorized cases.
   - Prove local `team_a` works without an allowlist; remote `default` works; remote `team_a` requires case-insensitive allowlisting; source count never changes on a read.
   - Exercise non-object MCP arguments, unknown fields, wrong JSON types, null, signed/floating limits, and local string forms with empty/sign/whitespace/decimal/suffix/overflow. Assert exact error code/field, nonzero exit, no untrusted echo, no handler data query on pre-dispatch failure, and unchanged full snapshots.
   - Configure an ambient non-default source and prove an omitted `source_id` still selects `default` for all four operations.
4. Identity and path-redaction matrix:
   - Seed exact source-specific active/deleted page, stored chunk/embedded chunk, and link counts in two sources and two physical brains. For selected/default, selected/team, decoy/default, and decoy/team independently, compare `pages`, `chunks`, `embedded_chunks`, `links`, and `schema_version` to direct bound SQL and `storage::check_schema_integrity` results. Whole-object inequality or comparison with another cell is not a substitute for any exact assertion.
   - Assert the local field set includes the actual temporary `open_at` database path. Assert the remote field set omits `db_path`, the temporary root, drive-letter, UNC, and volume-GUID path forms and separators derived from that path, usernames, and decoy path sentinels from both JSON and text.
   - Damage a required table only in a disposable fixture and prove a structured `database_error`, no process termination, no repair, and no path/config leakage.
5. `volunteer_context` matrix:
   - Query mode seeds matching/nonmatching, deleted, other-source, and decoy pages; proves canonical source on every row, conservative FTS only, no provider call, no body/path exposure, exact limit clamp, valid UTF-8/truncation, and `score DESC, slug ASC` ordering.
   - Create equal-rank candidates on both sides of the candidate/final limit and insert them in reverse order; repeated and reverse-fixture calls must serialize byte-identically with the same membership.
   - Recent mode seeds equal timestamps, newer out-of-source pages, deleted pages, and a small limit; prove source/active predicates precede limit and ordering is `updated_at DESC, id DESC`.
   - Exercise `query`, `q`, equal aliases, conflicting aliases, omitted/empty/whitespace query, valid 4096-byte input, 4097-byte input, and malformed UTF-8. Every rejected input preserves both database snapshots.
6. `get_timeline` matrix:
   - Seed active/deleted `timeline` pages and active non-timeline pages in default/team sources and a decoy brain. Make out-of-scope rows newer than selected rows and use a small limit to prove both predicates occur before limiting.
   - Assert exact fields, canonical source, no body, effective-time calculation, `effective_at DESC, id DESC` order, default/min/max/over-max limits, and byte-identical repeated output.
   - Create timeline pages through local N15 `add_timeline_entry` or direct `put_page` with `type=timeline`, and prove they appear without claiming or creating a separate event table.
7. `volunteer_chronicle` matrix:
   - Reuse N15 boundary fixtures for date-only, `T`/space timestamp forms, inclusive created/updated boundaries, active/deleted pages, source isolation, effective-time/id ordering, and 1..200 limit behavior.
   - Reject impossible date/leap/time values, offsets, partial/trailing input, empty supplied `since`, and malformed UTF-8 with the shared structured error and no mutation.
   - With a deterministic UTC-date seam, cover a normal week plus month, year, and leap-day transitions. Prove omission selects exactly today plus the previous six UTC dates and an empty window returns `[]` without falling back to 2000 or all history.
8. Selected-brain and read-only snapshot matrix:
   - Create selected and decoy databases with the same brain-facing source ids/slugs but distinct identity counts, titles, timeline rows, Chronicle rows, paths, and search sentinels.
    - For every successful, empty, clamped, malformed, unknown-source, unauthorized, and damaged-fixture call, hash a deterministic full logical snapshot of schema SQL and every column and row in every Qbrain application/user table for the selected and decoy databases immediately before and after. User tables mean all application tables (including pages, chunks, links, sources, facts, config, jobs, and related tables), excluding `sqlite_master` and `sqlite_sequence`. Every pair must match and no decoy/other-source/path sentinel may appear.
9. Registry and real MCP matrix:
   - Inspect exactly four operation registrations and `tools/list` schemas: Read, non-local-only, `additionalProperties=false`, exact allowed fields/types/defaults/minima/maxima, and accurate bounded descriptions.
   - Exercise real `tools/call` framing with MCP writes disabled for authorized success, empty result, path redaction, alias conflict, malformed limit/type, unknown field, unknown source, and denied source. MCP error signals and embedded structured operation JSON must match the declared contract.
   - Use only temporary brain ids and a unique temporary `LOCALAPPDATA`; make no live provider/network request and persist no secret or model setting.
10. Evidence manifest:
   - Record the approved plan and plan-audit hashes; the `PRE-CORRECTIVE-SCHEMA-GATE.json` hash, UTC interval, parsed `ok/schema_version`, pre-edit input-manifest digest, and ordering proof; relevant core/search/handler/MCP/test/verifier/build/schema input hashes; third-party SQLite input hash; production/test binary hashes; exact commands and exit codes; full-suite count; focused markers; source/auth/path-redaction cases; exact four-cell identity comparisons; and every selected/decoy before/after snapshot pair.
    - Record explicit facts that schema remained v12, the evidence manifest/file list contains no `docs/nodes/N30-*` file, build/test/runtime logs contain no N30 reference, no later-node artifact was used, no production `%LOCALAPPDATA%\Qbrain` data was touched, and no LLM/agent/application model, provider, base URL, API key, reasoning effort, context size, or compression threshold changed.

## Acceptance assertions (falsifiable)

1. After this revised plan receives a fresh Claude Code plan-audit PASS and becomes `approved`, but before any N19 corrective production/test/verifier/runtime-evidence edit, the isolated selected-brain `doctor -> Brain::health -> storage::check_schema_integrity` gate produces `PRE-CORRECTIVE-SCHEMA-GATE.json` with exit zero, `ok == true`, integer `schema_version == 12`, exact approved-plan/plan-audit/binary/pre-edit input hashes, an auditable UTC interval/order, no production-data/config access, and successful cleanup; otherwise corrective work is blocked and this plan returns to `draft`. Fresh and populated v12 databases then support all four operations with no schema or migration edit; reopening and every N19 call leave populated schema/data snapshots unchanged.
2. Every operation defaults only an omitted source to canonical `default`, ignores ambient-source injection, verifies registered-source existence without creation, and rejects invalid/unknown/unauthorized sources before any scoped data query.
3. Local registered-source reads require no allowlist; remote non-default reads require N2.5 authorization; `allow_write=true` cannot bypass it; all four operations work with MCP writes disabled and do not weaken global write-default-deny.
4. `get_brain_identity` returns the exact declared source-scoped counters and actual schema version from only the selected brain. Selected/default, selected/team, decoy/default, and decoy/team each compare all four counters and schema version to direct bound SQL/integrity evidence, never broad inequality. Local output includes the actual opened database path; remote JSON/text contains no `db_path` key or local/decoy path-derived value.
5. `volunteer_context` enforces the query/legacy-alias and 4096-byte UTF-8 contract, accepting identical `query`/`q` values as one query and rejecting different non-empty values. Query mode is source-scoped conservative FTS with no vector/rerank/LLM/provider call; recent mode uses `Brain::list_pages_for_source` or an equivalent source-filtered SQL helper before limit; both return only the declared bounded fields.
6. Query-mode membership/order is stable at candidate and final limits (`score DESC, slug bytewise ASC`), and recent mode is byte-stable in `updated_at DESC, id DESC` order. Deleted, other-source, and other-brain rows cannot consume a limit or appear.
7. `get_timeline` returns only active selected-source `type=timeline` pages, applies source/type predicates before limit, orders by `effective_at DESC, id DESC`, and honestly remains the N15 thin timeline-page subset with no page-attached event claim.
8. `volunteer_chronicle` calls `Brain::chronicle_since(since_iso, limit, source_id)`, preserves its strict UTC/inclusive/active-page/order contracts, and defaults through the deterministic optional-fixed-now helper to exactly seven UTC calendar dates with no unbounded historical fallback; production uses real UTC now.
9. All supplied limits consume the complete unsigned decimal, use defaults only when omitted, clamp to 1..50 for context and 1..200 for timeline/Chronicle, and reject empty/sign/whitespace/decimal/suffix/overflow inputs before enumeration without mutation.
10. MCP and local validation reject non-object arguments, wrong types, null supplied values, unknown fields, conflicting aliases, and invalid dates with the declared structured nonzero error. No error echoes raw untrusted values or exposes paths, bodies, configuration, tokens, or provider/model data.
11. Every successful row contains canonical source, exact declared fields, valid UTF-8 bounded display text, and deterministic JSON/text equivalence. No response includes page bodies, frontmatter, local paths except local identity, secrets, provider results, or unauthorized sentinels.
12. Full logical snapshots of selected and decoy databases are identical before/after every authorized, empty, clamped, malformed, unknown, denied, and damaged-fixture call. Each snapshot covers schema SQL plus every column and row in all Qbrain application/user tables except `sqlite_master` and `sqlite_sequence`. N19 never writes SQLite/files/jobs/config/logs or performs a network/provider operation.
13. Two physical brains with overlapping source ids/slugs and stronger decoy results prove selected-brain and source isolation for all four operations; no decoy or other-source counter, title, snippet, timeline row, Chronicle row, or path appears.
14. Native Windows x64 MSVC evidence records `/std:c++20`, full compiler/OS details, exact production/test commands and exit codes, an all-PASS suite at or above 25 registered tests including expanded `test_n19`, real MCP markers, manifest hashes, all snapshot/path-redaction evidence, no `docs/nodes/N30-*` manifest entry, and no N30 build/test/runtime log reference before the outcome audit.
15. Only after a fresh complete Claude Code outcome-audit PASS may the plan become `done` and exactly the four N19 ledger rows/notes be reconciled. No N17 work, N20+ work, schema migration, new dependency, N30 artifact, protected model-configuration change, commit, or push is part of N19.

## Explicit exclusions and no-N30 rule

- N30 is not a dependency, coordinator, plan, deliverable, evidence container, audit substitute, follow-up, or prerequisite for N19. No `N30-*` file is created, read as a gate, or required. N19 closes through its own node-specific plan and outcome audit loop.
- N17 is an independent Wave 4 node, not an N19 dependency or deliverable. N20-N29 and any later capability are excluded even if historical code exists in the worktree. Their plans, tests, evidence, audits, and ledger rows remain separate.
- N19 does not add a timeline/event/ontology table, migration, filesystem crawler, context feedback store, confidence model, full-history fallback, LLM/vector/rerank path, update checker, network request, CLI command, or new third-party dependency.
- No commit or push is part of this node unless the human user separately requests it.

## Rollback

- Keep or make the four N19 operations unavailable if source authorization, path redaction, deterministic ordering, strict validation, or read-only behavior cannot be maintained. Do not restore the historical unscoped/path-leaking handler behavior as a fallback.
- Clear `mcp.allowed_sources` to restore remote access to `default` only. Never use `allow_write`, an ambient source, silent fallback, or an error-message leak to work around authorization failures.
- Revert the N19 core/path/enumeration API, conservative-search tie-break, handlers, MCP typed maps, tests, and verifier together. Preserve N3/N12 search regression behavior and N15/N16/N18 contracts.
- No schema downgrade or data rewrite is needed because N19 plans no migration. If a migration is discovered to be required, stop and re-plan before editing schema code.
- Verification touches only disposable databases and temporary `LOCALAPPDATA`. Restore only temporary fixture backups if a rehearsal fails; never modify production `%LOCALAPPDATA%\Qbrain` data.

## Security notes

- Brain ids, source counters, titles, snippets, timeline rows, Chronicle activity, and database paths are sensitive. Source validation/existence/authorization must finish before data queries, and remote identity must omit path material from both JSON and text.
- N19 reads do not require write enablement, but remote source authorization remains fail-closed. `allow_write=true` cannot broaden visibility, and N19 must not alter the global write-default-deny registry behavior.
- Bind all SQL inputs. Query text is length/UTF-8 validated and goes through the established conservative FTS quoting path; it never becomes SQL syntax, a regex, path, command, or executable content.
- Row limits, source/type predicates before limits, UTF-8-safe truncation, deterministic tie-breaks, and no-body output bound CPU and disclosure. Errors do not echo attacker-controlled values or distinguish unauthorized row existence.
- Full-snapshot evidence covers selected and decoy brains and all schema/user tables, not a single count. Tests use only dummy sentinels and temporary roots, with no live network/provider call and no production data.
- Planning, implementation, verification, and auditing must not modify any LLM/agent/application base URL, API key, provider, model, reasoning effort, context size, compression threshold, or related protected configuration.

## Dependencies and parallelism notes

- The exact direct dependency set is N1, N2, N2.5, N3, N7, N8, N11, N15, N16, and N18. N4-N6, N9-N10, N12-N14, N17, and N20+ are not N19 dependencies. The current ledger status, historical N19 hard audit, or another node's evidence cannot replace the node-specific PASS artifacts below.

| node | plan-audit evidence | outcome-audit evidence | contract consumed by N19 |
|------|---------------------|------------------------|--------------------------|
| N1 | `docs/nodes/N1-PLAN-AUDIT.md`; PASS; SHA-256 `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `docs/nodes/N1-HARD-AUDIT.md`; PASS; SHA-256 `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` | Read operation and MCP write-default-deny |
| N2 | `docs/nodes/N2-PLAN-AUDIT.md`; PASS; SHA-256 `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` | `docs/nodes/N2-HARD-AUDIT.md`; PASS; SHA-256 `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` | Active-page and soft-delete semantics |
| N2.5 | `docs/nodes/N2.5-PLAN-AUDIT.md`; PASS; SHA-256 `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` | `docs/nodes/N2.5-HARD-AUDIT.md`; PASS; SHA-256 `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` | Canonical source ids and remote source authorization |
| N3 | `docs/nodes/N3-PLAN-AUDIT.md`; PASS; SHA-256 `ab99d7c2d0553575f16124fba807067a8039839321fb9f3469c949dbdc8a4994` | `docs/nodes/N3-HARD-AUDIT.md`; PASS; SHA-256 `2ca977089b0564f7ff60752c8cff4b968e1f528163239ef4c19e1bd22c094d24` | Conservative FTS search |
| N7 | `docs/nodes/N7-PLAN-AUDIT.md`; PASS; SHA-256 `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` | `docs/nodes/N7-HARD-AUDIT.md`; PASS; SHA-256 `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` | Authenticated loopback MCP |
| N8 | `docs/nodes/N8-PLAN-AUDIT.md`; PASS; SHA-256 `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `docs/nodes/N8-HARD-AUDIT.md`; PASS; SHA-256 `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` | Selected-brain isolation |
| N11 | `docs/nodes/N11-PLAN-AUDIT.md`; PASS; SHA-256 `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` | `docs/nodes/N11-HARD-AUDIT.md`; PASS; SHA-256 `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` | Native Windows evidence |
| N15 | `docs/nodes/N15-PLAN-AUDIT.md`; PASS; SHA-256 `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` | `docs/nodes/N15-HARD-AUDIT.md`; PASS; SHA-256 `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59` | Schema v12, Chronicle, and thin timeline-page contracts |
| N16 | `docs/nodes/N16-PLAN-AUDIT.md`; PASS; SHA-256 `ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef` | `docs/nodes/N16-HARD-AUDIT.md`; PASS; SHA-256 `591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba` | Source-filtered page enumeration |
| N18 | `docs/nodes/N18-PLAN-AUDIT.md`; PASS; SHA-256 `87db9821c255555ab6a42aab8d22cac945a5e0141aeeb3dd02e76a07e743af6d` | `docs/nodes/N18-HARD-AUDIT.md`; PASS; SHA-256 `f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053` | Strict read arguments and source resolution |

- At the pre-implementation gate, verify that every listed file still exists, still states PASS, and still has the recorded SHA-256. A missing artifact, non-PASS verdict, or unexplained hash change blocks implementation pending dependency review and plan re-audit. This table explicitly confirms that the N1, N2, and N2.5 plan-audit and outcome-audit files now exist.
- N19 directly reuses the listed contracts, especially N15 schema v12/Chronicle/timeline-page semantics, N16 source-filtered enumeration, and the N18 source/strict-validation pattern. Any proposed change that weakens a completed dependency contract returns this plan to `draft` for re-audit.
- After N19 plan approval only, disjoint slices may cover core identity/enumeration, conservative-search ordering, handler/MCP validation, and focused tests/verifier evidence. No implementation slice starts while the plan is `draft`.
- `src/qbrain/ops/handlers.cpp`, `src/qbrain/core/brain.cpp`, `src/qbrain/mcp/server.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` are shared hot files with N17 and other integrated work. The parent agent must serialize edits or assign exclusive ownership, review the merged diff, and rerun the full native suite before either outcome audit.
- The parent agent owns integration, production/test builds, real MCP smoke, evidence manifest, ledger reconciliation, and both N19 Claude Code gates. No implementation subagent may mark N19 approved/done or author a PASS audit.

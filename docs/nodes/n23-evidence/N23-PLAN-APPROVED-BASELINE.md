# N23 Plan - Source-scoped Chronicle History and Backfill Closure

**Status**: approved
**Depends on**: Directly on N1, N2, N2.5, N8, N11, N15, and N19 only: N1 operation scopes and MCP write-default-deny, N2 active-page/soft-delete/tag semantics, N2.5 canonical source ids and remote source authorization, N8 selected-brain routing and isolation, N11 native Windows evidence, N15 strict source-scoped UTC Chronicle page-activity contracts, and N19 strict MCP argument/source/error patterns plus bounded UTF-8 rendering. N20-N22 and N24+ are not dependencies.
**Plan audit**: PASS (`N23-PLAN-AUDIT.md`, Claude Code, 2026-08-04; audited draft SHA-256 `138d68f638bea13f1bc8198339d6e1d0422fa11a85185c7a49698fde87bb5398`; audit SHA-256 `0bff70920652adbe9929d0cd76697fadaa9912a9014ae6b41ddccb648c869aef`)
**Outcome audit**: pending (fresh node-specific Claude Code audit required after implementation and evidence)
**Wave**: Wave 5 (N20-N23); N23 is independent of N20-N22 after the dependencies above are complete
**Process note**: This is a retrospective re-verification and correctness closure required by the current node loop. The short 2026-07-27 plan, the 2026-07-29 N29 reconciliation audits, existing implementation/tests, and historical ledger claims are background only. They do not approve this plan and cannot satisfy its outcome gate. No N23 production, test, verifier, evidence, or ledger change may begin while this plan is `draft` or while its fresh plan audit is missing or FAIL.

## Goal

Re-verify and, where necessary, correct exactly three existing Chronicle operations:

1. `chronicle_on_this_day`: a bounded read of active page activity from the same UTC month/day in years before an anchor date, within one authorized canonical source in the selected brain.
2. `chronicle_last_seen`: a bounded page-based answer for when one required entity page was last active, relative to an optional UTC as-of date, within one authorized canonical source in the selected brain.
3. `chronicle_backfill`: an explicit, idempotent, source-scoped write that marks eligible Chronicle source pages with the `chronicle` tag, with dry-run support and no partial-success masking.

This node deliberately implements a Qbrain page-activity/tagging subset. It does not claim upstream page-attached timeline-event storage, `who` projections, Chronicle extraction jobs, narrative generation, diary ingestion, salience/confidence, or full gbrain Life Chronicle parity. The ledger and operation descriptions must retain those exclusions.

## Ledger rows to reconcile after outcome PASS

These rows are historical claims until this refreshed node passes its outcome audit. They are already counted in the ledger, so N23 must update their notes without increasing the implemented-operation total.

| op | scope and exact N23 subset |
|----|-----------------------------|
| `chronicle_on_this_day` | Read; prior-year UTC month/day matches over active pages in one authorized source |
| `chronicle_last_seen` | Read; last effective activity of one active entity page in one authorized source |
| `chronicle_backfill` | Write; idempotent `chronicle` tagging of eligible active pages in one authorized source, with dry-run |

No N20-N22 operation, N24+ operation, schema migration, or new ledger row belongs to N23.

## Dependency evidence

All direct dependencies must still have node-specific plan-audit and outcome-audit PASS before N23 approval. Hashes below are the planning baseline and must be rechecked by the plan auditor; a changed dependency is not automatically a failure, but its current PASS contract must be inspected.

| Node | Contract used by N23 | Plan SHA-256 | Plan-audit SHA-256 | Hard-audit SHA-256 |
|------|----------------------|-----------------|-----------------------|--------------------|
| N1 | Scope metadata and remote write default-deny | `7fc14a6921984f850fdf82b77e8042827de1bd8e97505df5938ce55d4d6a614a` | `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` |
| N2 | Active pages, soft deletion, and tags | `9a4f99cfd9385e8e245d9828d74ac4ac54bf15320877cb6da0fab7060e149b72` | `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` | `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` |
| N2.5 | Canonical source ids and remote allowlist | `5b7a56bee110eefec199bf2c3b5d0fbc70171ad634923a9a97dc85150d4b2983` | `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` | `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` |
| N8 | Selected-brain routing and physical isolation | `84438fa09559aef0c5471f4de339cad5f710b774e0f61ba7253d285ba89e9449` | `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` |
| N11 | Windows x64 MSVC C++20 build/test evidence | `1fb0898b37306d56b92b89eef40f824acf26a7a45d9626fbdecfd28fa8ea9b6d` | `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` | `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` |
| N15 | Strict UTC validation, effective activity, source-scoped Chronicle | `1ea4a30a2c82b54adf1a1c2722168c9d82e187165214de27606b9a82f6fc88c8` | `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` | `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59` |
| N19 | Selected-brain/source handler pattern, strict numeric types, bounded output | `61b1553403b7afcb1411b6e62ce9788e73981d0c2d00b94135704ed5a32bebb3` | `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470` | `d4ee4ad14e3768b5470865f092a783ba0d10b9e9155bfb17c4bd5ce594ad4f24` |

Planning Git baseline: `5ced8ccb511672536d0f9767a2bc1777baf561ab`. The worktree is intentionally dirty with user-owned earlier-wave work; N23 verification must use an explicit scoped manifest rather than assuming a clean tree.

## Current baseline and corrective scope

The existing N23 implementation is not presumed conformant. The approved implementation phase may correct these observed gaps and only closely related support code:

- The three Brain APIs are not source-scoped.
- `chronicle_on_this_day` accepts partial/invalid date strings, includes the anchor/current year, and lacks a deterministic id tie-break.
- `chronicle_last_seen` permits a global fallback and returns only `updated_at`, rather than requiring one entity and using effective activity.
- `chronicle_backfill` tags arbitrary recent pages across sources, catches and discards errors, and increments its count even when a tag already exists.
- The handlers use permissive `arg_int`, expose loose schemas, omit structured errors, and do not call the established source resolver.
- MCP lacks typed argument maps for all three operations and injects ambient `QBRAIN_SOURCE` into them.
- The shared `test_n20_23` coverage does not prove prior-year semantics, strict parsing, source/brain isolation, idempotency, default-deny, or no-partial-write behavior.

No schema migration is planned. After a fresh plan-audit PASS and status `approved`, but before any N23 production, test, verifier, or runtime-evidence edit, the parent must run the already-built current `qbrain.exe doctor --json` against a unique isolated temporary root and capture `docs/nodes/n23-evidence/PRE-CORRECTIVE-SCHEMA-GATE.json`. The immutable gate records UTC start/end, exact command and working directory, approved-plan/plan-audit/binary SHA-256 values, the Git HEAD commit id, a sorted hash/byte manifest of every scoped N23 production/test/verifier/build input as it existed before corrective work, parsed `ok=true` and integer `schema_version=12`, proof that production `%LOCALAPPDATA%\Qbrain` was not touched, and proof the temporary root was removed. The final verifier must validate this gate before any pending-evidence write and include its hash/facts in the final manifest. If the gate fails, reports another schema, or cannot prove isolation/cleanup, return this plan to `draft`, revise it, and obtain another Claude Code plan-audit PASS before editing implementation or migration code.

## Normative shared contract

1. All three operations accept optional `source_id`; omission means literal `default`, independent of ambient `QBRAIN_SOURCE`. Canonicalization, registered-source existence, and remote authorization finish before data enumeration or mutation.
2. Local calls require no source allowlist. Remote non-default calls require the N2.5 allowlist. `allow_write=true` cannot bypass source authorization.
3. Only declared fields are accepted. Non-object MCP arguments, unknown fields, nulls, booleans/objects in string fields, and non-unsigned numeric limits fail before handler work.
4. A supplied numeric value must consume the complete unsigned base-10 value. Omission alone selects a default. Zero clamps to the operation minimum; values above the maximum clamp to the maximum. Signs, whitespace, decimal/floating forms, suffixes, and overflow are invalid.
5. Errors are bounded structured JSON shaped as `{"error":{"code":"...","field":"...","message":"..."}}`, with `ok=false` and nonzero exit status. Errors do not echo raw input, page content, paths, allowlists, tokens, provider/model data, or cross-source identifiers.
6. SQL inputs are bound. Source/type/active/date predicates are applied before limits. Every visible ordering has a deterministic `id DESC` or bytewise key tie-break.
7. Read results contain canonical `source_id`, exact declared fields, and valid UTF-8. Display titles are capped at 512 bytes on complete code-point boundaries with an explicit truncation marker. Reads never return bodies, frontmatter, paths, config, tags from other sources, or provider data.
8. `chronicle_on_this_day`, `chronicle_last_seen`, and `chronicle_backfill` perform no LLM, embedding, rerank, network, filesystem, provider, or API-key work.

## Operation contracts

### `chronicle_on_this_day`

1. Accept only `source_id`, canonical `date`, legacy compatibility alias `mmdd`, and `limit`.
2. `date`, when supplied, is exactly one real UTC `YYYY-MM-DD`; omission uses the real current UTC date. The legacy `mmdd` alias, when supplied alone, is exactly a real `MM-DD` month/day (February 29 is allowed as a recurring leap-day query). If both are supplied, `mmdd` must equal the month/day of `date`; otherwise fail on `date`.
3. Return each active page in the selected source at most once when its `created_at` or `updated_at` month/day matches the anchor and that matching timestamp's year is strictly less than the anchor year. Activity in the anchor year never qualifies by itself.
4. For a page with two qualifying timestamps, `matched_at` is the later qualifying timestamp. Results order by `matched_at DESC, page id DESC`. Source, active, prior-year, and month/day predicates occur before the limit.
5. Limit defaults to 50 and clamps to 1..200.
6. Return an array of exact rows `{source_id, slug, title, type, created_at, updated_at, matched_at, years_ago}`. `years_ago` is the non-negative anchor year minus the matched timestamp year. No full upstream event/timeline-row claim is made.

### `chronicle_last_seen`

1. Accept only `source_id`, required canonical `entity`, legacy compatibility alias `slug`, and optional `asof`.
2. The effective entity is non-empty valid UTF-8 and at most 4096 bytes. If both aliases are supplied, their byte strings must match. Missing, empty, malformed, oversized, or conflicting values fail on `entity`.
3. `asof`, when supplied, is exactly one real UTC `YYYY-MM-DD`; omission uses the current UTC date.
4. Resolve exactly one active page by `(source_id, slug)`. There is no global fallback, substring match, deleted-page match, other-source match, or other-brain match. A missing page returns a structured `not_found` without revealing whether another source contains the slug.
5. `last_seen` is the later of that page's `created_at` and `updated_at`. Return exactly `{source_id, entity, last_seen, days_ago}`. `days_ago` is the signed whole UTC calendar-day difference from `last_seen`'s date to `asof`, making backdated as-of queries deterministic.

### `chronicle_backfill`

1. Accept only `source_id`, optional `since`, optional `limit`, and optional `dry_run`.
2. `since`, when supplied, reuses the N15 strict UTC date/timestamp forms (`YYYY-MM-DD` or `YYYY-MM-DD[T ]HH:MM:SSZ`, normalized inclusively). Omission applies no lower date bound. Empty supplied values, offsets, partial/trailing data, impossible dates/times, and malformed UTF-8 fail before enumeration.
3. Limit defaults to 1000 and clamps to 1..1000. It is a total bound after source, active, eligible-type, and since predicates.
4. Eligible pages are active selected-source pages whose type is exactly `meeting`, `conversation`, or `calendar-event`. Results are selected in effective activity descending, then page id descending order.
5. This Qbrain subset adds the exact tag `chronicle`; it does not enqueue `chronicle_extract`, synthesize timeline rows, inspect bodies/frontmatter, or claim upstream extraction parity.
6. `dry_run=true` performs the same bounded selection and reports counts without changing any table or sequence. A real run is idempotent: existing tags are not duplicated or counted as new.
7. A successful response is exactly `{source_id, scanned, eligible, tagged, already_tagged, dry_run}`. `scanned` and `eligible` are equal for the declared SQL-filtered subset; `tagged + already_tagged == eligible` on a successful real run; dry-run has `tagged == 0` and reports current `already_tagged`.
8. A real run is one explicit transaction. Busy/locked and other SQLite failures become `database_busy` or `database_error` and leave pages, tags, schema, and `sqlite_sequence` exactly unchanged. No per-row exception is swallowed.
9. The operation remains `Scope::Write` and sets `local_only=true` under the existing Qbrain registry semantics: remote invocation is denied unless explicit allow-write is enabled, and source authorization still applies when it is enabled.

## Deliverables

1. `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`:
   - source-aware N23 result types and APIs;
   - strict prior-year on-this-day SQL with effective-match ordering;
   - exact entity last-seen calculation;
   - transactional, idempotent, eligible-type backfill/dry-run;
   - no unrelated N20-N22, N24+, schema, search, provider, or filesystem behavior changes.
2. `include/qbrain/util/time_util.hpp` and `src/qbrain/util/time_util.cpp` only if a small shared pure UTC date helper is required. Existing N15/N19 date behavior must not be weakened.
3. `src/qbrain/ops/handlers.cpp`:
   - allowed-field, alias, strict limit/date/source validation;
   - bounded structured errors and exact JSON shapes;
   - correct Read/Write/local-only metadata and descriptions that state the Qbrain subset.
4. `src/qbrain/mcp/server.cpp`:
   - exact typed argument maps, including a real Boolean type for `dry_run` if needed;
   - N23 exclusion from ambient source injection;
   - no weakening of any existing operation's type validation.
5. Focused `tests/test_n23.cpp`, registered in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`.
6. `scripts/n23-verify.ps1` and `docs/nodes/n23-evidence/` with the immutable pre-corrective schema gate, a frozen scoped prebuild manifest, native production/test build logs, two full-suite logs, focused N23 markers, selected/decoy snapshot evidence, platform/compiler evidence, final manifest, and `VERIFY-REPORT.md`. The verifier validates the gate and evidence before writing pending/final state; it never writes an audit verdict or node/ledger status.
7. After outcome PASS only: this plan status and exactly the three N23 ledger row notes.

## Test matrix

1. Direct API and source matrix:
   - Use selected and decoy physical brains, each with `default` and `team_a`, overlapping slugs, distinct sentinel titles/timestamps, and registered sources.
   - Prove every result is from the selected brain and requested source. Make newer/stronger out-of-scope rows consume no limit.
2. On-this-day matrix:
   - Explicit anchors for normal dates, year boundary, leap day, and century leap rules.
   - Seed created-only, updated-only, both-match, anchor-year-only, future-year, other-day, deleted, other-source, and decoy rows.
   - Verify prior-year exclusion, one row per page, exact `matched_at`/`years_ago`, predicate-before-limit, tie ordering, repeat byte stability, and dynamic omitted-date behavior against current UTC.
   - Exercise `date`, `mmdd`, matching aliases, conflicting aliases, and invalid/partial/impossible forms.
3. Last-seen matrix:
   - Created newer, updated newer, equal timestamps, deleted page, unknown entity, same slug in another source/brain, explicit as-of before/equal/after, omitted as-of, alias equality/conflict, UTF-8 and 4096/4097-byte boundaries.
   - Verify exact signed calendar-day arithmetic and structured `not_found` with no cross-source disclosure.
4. Backfill matrix:
   - Eligible and ineligible types, active/deleted pages, before/at/after since boundary, two sources and two brains, existing and missing tags, and more eligible rows than a small limit.
   - Verify dry-run full snapshot identity, real-run exact tag delta, idempotent second run, deterministic membership, no duplicate tag, and unchanged nonselected source/brain.
   - Force a transaction failure/busy condition and prove no partial tag or sequence mutation; errors must not be swallowed.
5. Limit and MCP type matrix:
   - Omitted, zero, one, exact maximum, above maximum, sign, whitespace, decimal, suffix, and overflow for every numeric field.
   - Non-object arguments; null/boolean/object/array wrong types; unknown fields; malformed UTF-8; explicit empty date/since/entity; and typed `dry_run` true/false versus wrong types.
   - Compare registered operation metadata and `tools/list` schemas exactly, including scopes, locality, defaults, bounds, required/alias fields, and `additionalProperties=false`.
6. Security matrix:
   - Reads work with MCP writes disabled; unauthorized remote non-default reads fail before enumeration; `allow_write` cannot bypass source authorization.
   - Remote backfill without allow-write is denied before mutation; an explicitly enabled authorized call succeeds; an enabled unauthorized call is still rejected without mutation.
   - Ambient `QBRAIN_SOURCE=team_a` does not change omitted N23 source from `default`.
7. Snapshot and disclosure matrix:
   - Full logical snapshots of both selected and decoy databases before/after every read, dry-run, rejection, denial, and failure.
   - For successful backfill, compare exact allowed tag-row delta and prove all other tables/schema plus decoy remain unchanged.
   - Responses/errors contain no bodies, frontmatter, database paths, allowlists, secrets, provider/model data, or other-source/decoy sentinels; all JSON is valid UTF-8 and titles respect the 512-byte bound.
8. Native regression evidence:
   - Build production and tests using the documented Windows x64 MSVC C++20 scripts.
   - Run the complete registered suite twice from the same frozen binaries. The pre-N23 N19 baseline is 26/26; the final count must be derived and frozen after registration, be at least 27, include exactly one N23 PASS, and be identical in both runs.
   - Record exact commands, exits, compiler/target/standard, input/binary hashes, test count, N23 matrix counters, Git HEAD, and protected-config/no-commit/no-push checks.

## Acceptance assertions (falsifiable)

1. The immutable pre-corrective gate proves that an isolated current binary returned `ok=true` and schema v12 before every corrective N23 edit, binds the approved plan/audit/binary/HEAD/scoped inputs, proves production-data isolation and cleanup, and is revalidated by the final verifier; N23 introduces no DDL/version migration.
2. All three operations resolve the literal default or supplied canonical source, ignore ambient source injection, enforce registered-source/remote authorization before data access, and never cross selected-brain or source boundaries.
3. `chronicle_on_this_day` accepts only the declared date/alias forms, returns each qualifying active prior-year page once, computes exact `matched_at` and non-negative `years_ago`, applies predicates before the 1..200 limit, and serializes deterministically.
4. `chronicle_last_seen` requires one valid entity, has no global fallback, uses selected-source active-page effective activity, returns exact signed UTC calendar `days_ago`, and reports unknown entities as bounded structured `not_found` without cross-source disclosure.
5. `chronicle_backfill` selects only active `meeting`, `conversation`, and `calendar-event` pages in the chosen source after the optional inclusive since boundary and before the 1..1000 limit; it never tags arbitrary pages or another source/brain.
6. Backfill dry-run is fully read-only; a real run is one idempotent transaction with exact counts and tag deltas; a repeated run adds no duplicate, and forced SQLite failure leaves the full logical state unchanged.
7. Reads are `Scope::Read`; backfill is `Scope::Write` with `local_only=true`; remote default-deny and explicit authorized allow-write paths behave exactly as declared, while allow-write never bypasses source authorization.
8. Local handlers and MCP reject unknown fields, non-object arguments, wrong JSON types, null supplied fields, invalid dates, conflicting aliases, malformed/oversized entity text, and invalid numeric forms before enumeration/mutation with structured nonzero errors.
9. Registered metadata and `tools/list` expose exact schemas, scopes, locality, defaults, aliases, bounds, Boolean dry-run, and `additionalProperties=false`; advertised valid forms are callable and no undeclared form is silently accepted.
10. All successful rows/objects have exact declared fields, canonical source, valid bounded UTF-8, and deterministic ordering; no response or error exposes bodies, paths, config/allowlist values, secrets, provider/model data, or unauthorized sentinels.
11. Full selected/decoy snapshots prove every read, dry-run, rejection, denial, and error is nonmutating; successful backfill changes only the exact selected-source tag rows allowed by the plan.
12. Native Windows x64 MSVC C++20 production and test builds exit 0, two complete frozen-binary suite runs have the same registered count at or above 27 with every test PASS and one N23 PASS, and focused markers prove every matrix above.
13. Evidence manifests bind the approved plan, plan audit, HEAD, scoped inputs, commands, platform, binaries, logs, counts, and hashes; the verifier has no audit/status/ledger authority and detects stale or mismatched evidence.
14. No N20-N22 or N24+ capability, N30 artifact/reference, protected model/provider/API configuration change, secret, production-data access, commit, or push is part of N23.
15. Only after a fresh complete node-specific Claude Code outcome-audit PASS may the parent set this plan to `done` and reconcile exactly the three historical N23 ledger row notes without changing the implemented-operation total.

## Rollback

- Before outcome PASS, revert only N23-owned corrective hunks and focused test/verifier/evidence artifacts; preserve all user-owned and other-node work.
- No schema rollback is required because N23 adds no migration.
- Backfill itself is idempotent but not silently reversed. Tests use disposable databases. If an implementation must expose product rollback for added tags, that is a plan change and requires re-audit before implementation.
- A failing build, test, evidence check, or outcome audit leaves the plan `approved` (or returns it to `draft` if the plan is wrong), leaves ledger notes unreconciled, and blocks the next wave.

## Security notes

- Chronicle history can disclose personal activity patterns. Validate source existence and authorization before querying and do not reveal whether an unauthorized source contains an entity.
- Backfill is a bulk mutation and remains MCP write-default-deny. Validate every input and determine the exact eligible set before opening the transaction; use bound SQL and never swallow per-row failures.
- Bounds, active/source/type predicates before limits, deterministic ordering, and no-body output constrain CPU and disclosure. Titles are rendered with the established UTF-8-safe bound.
- Tests use unique temporary roots and dummy sentinels only. They do not access `%LOCALAPPDATA%\Qbrain`, real accounts, credentials, provider endpoints, or the network.
- Planning and implementation do not modify model name, provider, base URL, API key, reasoning effort, context size, compression threshold, or any other protected agent/LLM configuration.

## Parallelism notes

- N23 planning and both audit gates are parent-owned.
- After plan approval, disjoint slices may run in parallel: core/date SQL, handler/MCP validation, focused tests, and verifier/evidence tooling.
- `src/qbrain/ops/handlers.cpp`, `src/qbrain/mcp/server.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` are shared Wave 5 hot files. The parent serializes and reviews those merges; agents do not overwrite another node's work.
- N23 has its own plan audit, evidence directory, verifier report, and outcome audit. It cannot share an audit file or verdict with N20-N22.

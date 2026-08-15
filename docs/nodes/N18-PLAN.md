# N18 Plan - Source-scoped Graph Heuristics

**Status**: done (plan + outcome audit PASS 2026-08-04)
**Depends on**: N1-N13 approved and done; specifically N1 read-operation and MCP default-deny contracts, N2 active-page/soft-delete semantics, N2.5 canonical source ids and remote source authorization, N3 link storage semantics, N7 authenticated loopback MCP, N8 selected-brain isolation, N10/N13 page-owned facts, and the N13 native test baseline
**Plan audit**: PASS (`N18-PLAN-AUDIT.md`, Claude Code, 2026-07-30)
**Outcome audit**: PASS (`N18-HARD-AUDIT.md`, Claude Code, 2026-08-04)
**Wave**: Wave 3 with N14, N15, and N16; N18 closes independently before Wave 4 may begin
**Process note**: This is a retrospective re-plan under the current N1-N13 governance contracts. The 2026-07-26 N18 plan and hard audit, the existing implementation, `tests/test_analytics.cpp`, historical combined/wave evidence, and existing ledger claims are context only. They cannot satisfy the current plan or outcome gate. No N18 implementation may begin while this plan is `draft`; a new node-specific Claude Code plan-audit PASS is required before changing the status to `approved`.

## Goal

Re-verify and, where necessary, correct the existing N18 operations as one bounded, read-only heuristic analytics node:

1. `find_anomalies`: report broken same-source link targets and high same-source out-degree for the active selected graph.
2. `find_contradictions`: report only the documented syntactic fact-pair heuristics whose active facts have provable ownership by active pages in the selected source.
3. `find_experts`: rank active pages by inbound link-row count within the active selected graph.
4. Make source validation, remote authorization, numeric bounds, output bounds, deterministic ordering, selected-brain isolation, and zero-mutation behavior explicit and independently testable.

N18 is deliberately heuristic-only. It does not call an LLM, infer truth, infer expertise from prose or facts, perform semantic entity resolution, repair graph data, or claim the full upstream gbrain graph-analysis surface.

## Ledger rows moved to implemented

The current ledger text is a historical claim and remains provisional during this refreshed loop. Reconcile exactly these three rows and the N18 notes only after a node-specific outcome-audit PASS.

| op | scope and locality | exact N18 subset |
|----|--------------------|------------------|
| `find_anomalies` | Read, remote-capable subject to source authorization | Selected-source active-origin links whose target is missing or soft-deleted in that same source, plus selected-source active origins with more than 20 stored outgoing link rows |
| `find_contradictions` | Read, remote-capable subject to source authorization | Selected-source active, page-owned fact pairs with the same ASCII-case-folded predicate and different objects, or one documented opposing-predicate pair/prefix rule |
| `find_experts` | Read, remote-capable subject to source authorization | Selected-source active target pages ranked by inbound stored link rows from active pages in that same source |

`find_orphans`, fact extraction, graph traversal, source lifecycle, anomaly repair, full-text/semantic analysis, and every N14-N17/N19+ operation are outside N18.

## Normative operation contracts

### Shared request, source, error, and output contract

1. Each operation accepts only `source_id` and `limit`. An omitted `source_id` means canonical `default`; an explicitly supplied empty value is invalid rather than silently defaulted.
2. Canonicalize and validate `source_id` with the N2.5 rules: 1..64 ASCII bytes, `^[A-Za-z0-9_-]+$`, not a case-insensitive Win32 reserved device name, and lowercase canonical identity. A read must verify that the canonical source already exists and must never call a create-on-read path such as `ensure_source`.
3. Local callers may read any registered canonical source. Remote callers may read `default`; a remote non-default source must appear case-insensitively in `mcp.allowed_sources`. An empty allowlist therefore permits remote reads of `default` only. `allow_write=true` does not bypass this read authorization.
4. Invalid, unknown, and unauthorized sources fail before analytics SQL executes. They return `ok=false`, nonzero `exit_code`, and valid JSON shaped as `{"error":{"code":"invalid_source|source_not_found|source_not_allowed","field":"source_id","message":"..."}}`. Error text does not echo the raw untrusted source value.
5. `limit` defaults remain 100 for anomalies, 100 for contradictions, and 50 for experts. A supplied value must be a complete unsigned ASCII decimal string with no sign, whitespace, decimal point, or suffix. Parse overflow is a structured `invalid_argument` error on field `limit` before querying.
6. Effective `limit` is clamped to the inclusive range 0..200. `0` is a successful empty result; values above 200 return at most 200 rows. Omission alone selects the operation default. Numeric errors use the same structured-error envelope and never fall back to a default.
7. Successful JSON is always an array. Every row includes canonical `source_id`, so the applied scope is explicit. An empty result is `[]`, not an error. Text output is derived from the same bounded authorized rows.
8. Each `detail` is valid UTF-8 and at most 512 bytes after deterministic code-point-safe truncation with an explicit truncation marker. JSON escaping handles quotes, backslashes, tabs, and newlines. N18 never emits page bodies, link context, complete fact records, page ids, provider responses, configuration values, tokens, or secrets.
9. All queries use bound parameters for `source_id` and `limit`. The three operations have no database write, job/config side effect, filesystem write, network request, provider invocation, or model invocation.

### `find_anomalies`

1. Analyze only link rows `l` with `l.source_id = requested_source` whose origin resolves to a live page with `p.source_id = l.source_id`, `p.slug = l.from_slug`, and `p.deleted_at IS NULL`. Links whose origin is missing or soft-deleted are outside this active-graph view.
2. Classify a target only against pages in `l.source_id`:
   - `link_to_deleted_page`: no live target exists and a soft-deleted page with that source/slug exists.
   - `link_to_missing_page`: no live target exists and no page, live or deleted, with that source/slug exists.
   - A live page with the same slug in another source never satisfies the selected-source target.
3. Emit at most one broken-target anomaly per `(kind, source_id, from_slug, to_slug)`, even when multiple stored link types/link sources connect the same slugs. The row is `{source_id, kind, slug, detail}` where `slug` is the live origin and `detail` identifies the bounded target.
4. `high_out_degree` means strictly more than 20 stored link rows from one live origin in the requested source. The count is grouped by `(l.source_id, l.from_slug)`; rows from another source never contribute. Broken-target rows still count as stored outgoing rows. Exactly 20 is not anomalous; 21 is.
5. Apply one final deterministic ordering before `limit`: kind rank `link_to_deleted_page`, `link_to_missing_page`, `high_out_degree`; then origin slug bytewise ascending; then target slug bytewise ascending for broken-target rows; then count descending for the remaining tie. SQL row order and insertion order cannot affect serialization.

### `find_contradictions`

1. A fact is eligible only when `facts.active = 1` and `facts.page_id` inner-joins to an active page whose `pages.source_id` is the requested source. This page ownership is the sole source-provenance contract for facts.
2. Facts with `page_id IS NULL`, a dangling `page_id`, a soft-deleted owner page, or an owner page in another source are excluded. Legacy null-page facts are not assigned to `default`, inferred from `entity_slug`, backfilled, deleted, or otherwise mutated by N18 because their provenance cannot be proven.
3. Group eligible facts by byte-exact `entity_slug`. The first heuristic, `same_predicate_different_object`, compares predicates with ASCII case-folding and reports two byte-distinct `object_text` values for the same folded predicate.
4. The second heuristic, `conflicting_predicates`, is intentionally limited to ASCII-case-insensitive pairs `is/is_not`, `is/isnt`, `is/isn't`, `supports/opposes`, `likes/dislikes`, `has/lacks`, `titled/not_titled`, `titled/untitled`, `true/false`, `yes/no`, `works_at/left`, `employed_by/former_employee_of`, `located_in/not_located_in`, and `member_of/not_member_of`; it also treats exactly one `not_`, `no_`, or `anti_` prefix as opposing its exact base predicate.
5. This operation does not perform natural-language negation, temporal reconciliation, confidence scoring, synonym expansion, LLM judging, or truth adjudication. A returned row means only that the stored pair matched one declared syntactic rule.
6. Canonicalize each pair independent of encounter order and deduplicate by `(source_id, kind, entity_slug, folded predicate pair, byte-exact object pair)`. Duplicate storage rows do not duplicate output.
7. Return `{source_id, kind, slug, detail}` where `slug` is the entity and `detail` is a bounded rendering of the two predicates/objects. Sort final rows bytewise by entity slug, then kind rank `same_predicate_different_object` before `conflicting_predicates`, then folded predicate tuple, object tuple, and fact-id tuple only as a final invisible tie-break. Do not iterate an `unordered_map` directly into output.

### `find_experts`

1. Consider only link rows in the requested source whose origin and target both resolve to live pages in that same source. A missing/soft-deleted origin or target contributes zero.
2. Count stored inbound link rows by `(source_id, to_slug)`. Different link types or link sources are separate stored rows and each contributes one; rows from another source never contribute.
3. Return only pages with inbound count greater than zero as `{source_id, slug, inbound_count}`. `inbound_count` is a non-negative JSON integer derived from SQLite `COUNT(*)`.
4. Rank by `inbound_count DESC`, then target slug bytewise `ASC`. Apply the effective limit after source/live-page predicates and grouping. Identical unchanged calls must serialize byte-for-byte identically.
5. The label "expert" is only an inbound-link popularity heuristic. N18 does not inspect facts, body text, authorship, credentials, recency, or semantic quality.

## Deliverables

1. `include/qbrain/graph/analytics.hpp` and `src/qbrain/graph/analytics.cpp`: source-aware signatures/queries, active-graph and page-owned-fact scoping, exact heuristic set, deduplication, bounded details, and explicit deterministic final ordering.
2. The smallest compatible source-resolution/authorization seam in existing core/operation helpers: canonicalize and verify a registered source without mutation, and enforce the N2.5 remote allowlist before analytics execution. Do not duplicate subtly different source rules in three handlers.
3. `src/qbrain/ops/handlers.cpp`: all three handlers remain `Scope::Read` and non-local-only, expose `source_id` plus `limit` in their JSON schemas, use strict full-string limit parsing, return structured failures, and include canonical source in successful rows.
4. A dedicated `tests/test_n18.cpp`, registered in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`. Historical `tests/test_analytics.cpp` remains a regression input but is not sufficient evidence for this refreshed node.
5. `scripts/n18-verify.ps1` and `docs/nodes/n18-evidence/` containing `VERIFY-REPORT.md`, native build/test output, focused runtime output, registry/MCP smoke output, full logical database snapshot hashes, and a manifest/hash list of N18 deliverables and the approved plan.
6. A new node-specific `N18-PLAN-AUDIT.md` before implementation and a new node-specific `N18-HARD-AUDIT.md` after implementation/evidence. Only Claude Code may issue their PASS verdicts under the project rules.
7. No schema migration is planned. If implementation discovers that a schema/index/constraint change is required, stop, return this plan to `draft`, document populated-database migration/idempotence/rollback tests, and obtain a new Claude Code plan-audit PASS before editing schema or migration code.
8. After and only after a complete outcome-audit PASS, set this plan to `done` and reconcile only the three N18 ledger rows/notes to the exact source-scoped heuristic subset and evidence links.

## Tests and evidence

All build and runtime verification uses native Windows 11 PowerShell, MSVC x64, and C++20. WSL and Docker are not part of the build/test path.

1. Native full-suite baseline:
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1` and the produced `build\cl\qbrain_tests.exe` as applicable to the script contract.
   - Record exact commands, exit codes, Windows/x64 markers, complete `cl.exe` version, `/std:c++20`, exact registered test count, and every result. The complete suite must be all PASS, include the dedicated N18 test, and remain at or above the N13 baseline of 21 tests.
2. Anomaly fixture matrix:
   - Seed `default` and `team_a` with overlapping origin/target slugs. Prove a live target in another source cannot hide a selected-source missing target and a deleted target in another source cannot change its classification.
   - Cover live, missing, and soft-deleted same-source targets; missing/deleted origins; multiple link types between one slug pair; exactly 20 and exactly 21 outgoing link rows; and large counts split across sources.
   - Assert deduplication, exact kind/slug/detail/source fields, fixed kind priority, bytewise ties, effective limits, bounded details, and byte-identical repeated JSON.
3. Contradiction fixture matrix:
   - Cover same folded predicate/different objects, each explicit opposing-predicate family, every supported negating prefix, non-conflicting predicates, identical objects, duplicate rows, inactive facts, soft-deleted owner pages, and facts owned by another source.
   - Seed `page_id IS NULL` and dangling legacy facts and prove both are excluded from every source, including `default`, without being modified or backfilled.
   - Use overlapping entity slugs in two sources and prove no cross-source pair is formed. Reverse insertion/query encounter order and assert canonical pair rendering, deterministic ordering, deduplication, UTF-8-safe truncation, and valid JSON escaping.
4. Expert fixture matrix:
   - Seed live and deleted/missing origins and targets, multiple link types, tied inbound counts, overlapping slugs in another source, and an out-of-scope source with a larger rank.
   - Assert only live selected-source endpoints contribute, row-count semantics are exact, ranking is `count DESC, slug ASC`, zero-inbound pages are absent, and limiting occurs after source filtering/grouping.
5. Limit and source validation matrix:
   - Verify omitted per-operation defaults, `0`, `1`, exact maximum 200, and values above 200. Prove `0` returns `[]` without analytic SQL work and no call returns more than 200 rows.
   - Reject empty supplied limit, signs, whitespace, decimals, suffixes such as `1junk`, and overflow with the declared error envelope and no query/mutation.
   - Verify omitted source selects `default`, mixed-case `Team_A` resolves to registered `team_a`, and empty, malformed, reserved, overlength, and valid-but-unknown sources return the exact structured errors without creating a source.
6. Remote authorization and selected-brain isolation matrix:
   - Remote `default` succeeds. Remote `team_a` fails with an empty allowlist, succeeds when allowlisted case-insensitively, and a different non-default source remains denied. Repeat with `allow_write=true` and prove it does not bypass source authorization.
   - Create two physical brain databases containing identical source ids/slugs but distinct anomaly targets, contradiction object sentinels, and expert ranks. Invoke every operation on one selected brain and prove no source or detail sentinel from the decoy brain appears.
7. Read-only full-snapshot matrix:
   - Produce a deterministic full logical snapshot of schema plus every row and column in every SQLite user table, including pages, links, facts, sources, jobs, config, logs, and sequences as present. Hash both selected and decoy databases immediately before and after each successful, empty, malformed, unknown-source, unauthorized, and limit-clamped call.
   - Every before/after pair must match. The operations may not repair, canonicalize in storage, create sources, backfill facts, update access timestamps, enqueue jobs, or change WAL-visible logical state.
8. Registry/MCP security and runtime matrix:
   - Inspect registry metadata and tool schemas: exactly the three N18 operations are `Scope::Read`, `local_only=false`, and declare only optional `source_id`/`limit` with the documented defaults/bounds.
   - Exercise local registry calls and real MCP `tools/list`/`tools/call` serialization with write disabled. Authorized reads succeed; malformed/unknown/denied calls set the operation/MCP error signal, preserve the structured JSON error, and leave full snapshots unchanged.
   - Verify remote default and allowlisted non-default data are bounded and parseable, non-allowlisted details never appear, text/JSON representations use the same rows/order, and no live network/provider call occurs.
9. Evidence manifest:
   - `scripts/n18-verify.ps1` runs from a unique temporary `LOCALAPPDATA` and temporary brain ids, never production `%LOCALAPPDATA%\Qbrain` data.
   - Record approved-plan hash, deliverable hashes, exact commands/exit codes, focused markers for every acceptance row, dependency PASS references through N13, snapshot hashes, and explicit markers that no N30 artifact or model-configuration change was used.

## Acceptance assertions (falsifiable)

1. N18 implementation starts only after a new Claude Code plan-only audit returns PASS and this plan is marked `approved`; the 2026-07-26 plan/audit, old analytics test, historical hard audit, and shared wave evidence are not gate substitutes.
2. Each operation defaults only an omitted source to canonical `default`, verifies source existence without creating it, and fails invalid, unknown, or unauthorized sources with the declared structured error before analytics SQL.
3. Local registered-source reads work without an allowlist; remote non-default reads work only when N2.5-authorized, and `allow_write=true` never bypasses that restriction.
4. Supplied limits consume the full unsigned decimal string, reject malformed/overflow input, clamp to exactly 0..200, preserve per-operation omitted defaults, and never permit more than 200 rows.
5. `find_anomalies` evaluates only selected-source links from live same-source origins; missing/deleted target checks cannot match another source; duplicate slug-pair links produce one broken-target result.
6. `high_out_degree` is emitted only above 20 selected-source stored outgoing rows grouped by source and live origin. Other-source rows cannot push an origin over the threshold, while broken selected-source rows retain the declared row-count semantics.
7. `find_contradictions` uses only active facts inner-joined through active selected-source page ownership. Null-page, dangling, deleted-owner, inactive, and other-source facts cannot participate or be mutated.
8. Contradiction results match only the exact same-predicate or documented opposing-predicate/prefix rules, are canonicalized/deduplicated independent of encounter order, and make no truth, temporal, semantic, LLM, or full-gbrain parity claim.
9. `find_experts` counts only stored links between live pages in the selected source, ranks by `inbound_count DESC, slug ASC`, excludes zero-inbound/deleted/missing targets, and does not infer expertise from facts or content.
10. Every success row includes canonical source and the documented fields. Details remain valid UTF-8 and at most 512 bytes; outputs contain no body, link context, complete fact record, secret, token, provider response, or configuration dump.
11. Final ordering for all three operations is explicitly content-sorted and byte-identical on unchanged data; SQL row order, insertion encounter order, `unordered_map`, and `unordered_set` iteration cannot determine the returned order.
12. Fixtures with overlapping slugs/entities and stronger decoy results prove source and selected-brain isolation for every operation; no unauthorized sentinel appears in JSON or text.
13. Full logical snapshots of both selected and decoy databases are unchanged across authorized, empty, malformed, unknown, denied, and clamped calls. No N18 operation writes SQLite, files, jobs, config, logs, or network state.
14. Registry and real MCP evidence prove all three operations remain Read, expose the exact schemas, enforce source authorization before detail exposure, preserve structured failures, and work with MCP writes disabled.
15. Native Windows x64 MSVC evidence records `/std:c++20`, compiler version, exact commands/exit codes, an all-PASS full suite of at least 21 tests including `test_n18`, runtime markers, deliverable hashes, and snapshot hashes before the outcome audit.
16. Only after a complete new Claude Code outcome-audit PASS may the plan become `done` and only the three N18 ledger rows/notes be reconciled. No schema migration, N30 artifact, LLM/model configuration change, commit, or push is part of N18.

## Explicit exclusions and no-N30 rule

- N30 is not a dependency, coordinator, plan, deliverable, evidence container, audit substitute, or follow-up for N18. No `N30-*` file is created, read as a gate, or required. N18 closes through its own PLAN -> PLAN-AUDIT PASS -> implementation/evidence -> HARD-AUDIT PASS loop.
- N14, N15, and N16 may progress independently within Wave 3 after their own plan approvals, but none of their audits or evidence can satisfy N18. Wave 4 does not begin until all Wave 3 nodes have independently closed.
- N18 does not include schema migration, graph repair, anomaly remediation, fact provenance backfill, orphan analysis, source creation/removal, filesystem crawling, LLM calls, semantic contradiction detection, semantic expertise scoring, or full upstream parity.
- Planning, implementation, verification, and evidence must not change any LLM/agent/application base URL, API key, provider, model, reasoning effort, context size, compression threshold, or related protected configuration.
- No commit or push is part of this node unless the human user separately requests it.

## Rollback

- Keep or make the three N18 operations unavailable if selected-source filtering, remote authorization, deterministic output, or read-only behavior cannot be maintained. Existing N1-N13 capabilities remain available.
- Clear `mcp.allowed_sources` to return remote access to `default` only. Do not widen source access or suppress structured failures as a rollback workaround.
- Revert the source-aware analytics signatures/handlers/tests together. N18 plans no stored-data transformation, so no database downgrade or provenance rewrite is required.
- If a migration becomes necessary, stop implementation, return this plan to `draft`, revise migration/populated-database/rollback acceptance, and obtain another Claude Code plan-audit PASS before any schema edit.
- Verification uses disposable databases. Restore only temporary fixture backups if a rehearsal fails; never modify production `%LOCALAPPDATA%\Qbrain` data.

## Security notes

- Slugs, anomaly details, contradiction object snippets, and graph ranks are source-sensitive knowledge. Perform canonical validation, existence checks, and remote allowlist authorization before any analytic query or serialization.
- These are read operations, so MCP `--allow-write` is irrelevant. Default-deny for non-allowlisted remote sources still applies and must fail closed without leaking whether a slug/entity exists.
- Bind every SQL input. Do not concatenate source ids or limits, and do not use another source's page row to satisfy a link endpoint or fact owner.
- Null-page legacy facts have unknowable source provenance. Excluding them is the only N18-safe contract; assigning them to `default` or guessing from an entity slug would create a cross-source disclosure risk.
- Bounded detail rendering must escape control data and avoid page bodies, link contexts, full fact records, secrets, provider/model configuration, and raw error echoes.
- Tests use only dummy sentinels, temporary brains, and no live network/provider calls. Evidence contains no secrets or production graph content.
- No agent/model/provider/baseURL/key/reasoning/context/compression setting may be read for disclosure or changed by this node.

## Dependencies and parallelism notes

- Before implementation, verify and record that N1-N13 have node-specific plan-audit and outcome-audit PASS artifacts. A status line or historical wave note cannot replace those documents.
- After N18 plan approval only, disjoint slices may cover analytics SQL/data structures, shared source/handler validation, and focused tests/verifier evidence. `src/qbrain/ops/handlers.cpp`, analytics headers, test registration, CMake, and the MSVC script are shared hot files and require parent-owned merge review with concurrent Wave 3 work.
- N14/N15/N16/N18 may run in parallel only where file ownership is disjoint. Serialize edits to shared handlers/test/build registration and verify the integrated Wave 3 worktree before node outcome audits.
- The parent agent owns integration, the native full suite, real MCP smoke, snapshot evidence, N18 evidence manifest, ledger reconciliation, and both Claude Code gates. Subagents cannot mark N18 done or author a PASS audit.

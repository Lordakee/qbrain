# N16 Plan - Source-scoped Heuristic Code Intelligence

**Status**: done (plan + outcome audit PASS 2026-08-04)
**Depends on**: N1-N13 approved and done; specifically N1 operation/SQLite read contracts, N2 active-page and soft-delete semantics, N2.5 canonical source ids and remote source authorization, N7 authenticated loopback MCP, N8 selected-brain isolation, N11 native Windows quality evidence, and the N13 native test baseline
**Plan audit**: PASS (`N16-PLAN-AUDIT.md`, Claude Code, 2026-07-30)
**Outcome audit**: PASS (`N16-HARD-AUDIT.md`, Claude Code, 2026-08-04)
**Process note**: This is a retrospective re-plan under the current node rules after N1-N13. The 2026-07-26 N16 plan and hard audit, the existing implementation, and any shared or wave-level evidence are context only. They cannot satisfy the current plan or outcome gate. Implementation must not begin while this plan is `draft`; a new Claude Code plan-audit PASS is required before changing the status to `approved`.

## Goal

Re-verify and, where necessary, correct the existing N16 capability as one bounded read-only wave:

1. Find definition-like lines for a validated symbol in active page bodies from exactly one canonical source.
2. Find literal identifier-boundary references for that symbol in the same bounded source scope.
3. Find call-like references where that symbol is followed by optional whitespace and `(`.
4. Make validation, bounds, ordering, selected-brain isolation, remote source authorization, and error behavior explicit and independently testable.

N16 remains a lightweight C++20 line scanner. It is not tree-sitter, an AST, a compiler index, semantic name resolution, or full upstream code-intelligence parity. It introduces no third-party parser, schema migration, filesystem crawl, network request, or LLM configuration change.

## Current risks to close

- `codeintel::scan` currently calls `Brain::list_pages(page_limit, "")`, whose query has no source predicate. A request can therefore scan every registered source and can consume `page_limit` on pages outside the requested source.
- `code_def`, `code_refs`, and `code_callers` currently neither accept nor authorize `source_id`, even though their snippets and slugs are source-sensitive content.
- Invalid non-empty symbols currently collapse to a successful empty result inside the scanner. Callers cannot distinguish invalid input from a valid symbol with no matches.
- The handlers use the permissive shared `arg_int`; non-numeric values fall back to defaults and `std::stoi` accepts a numeric prefix with trailing data. Effective bounds are not declared or consistently enforced.
- `Brain::list_pages` orders only by `updated_at`, so ties do not have a stable page order. The historical test does not prove source, brain, tombstone, authorization, parameter, or full-database read-only isolation.
- The historical 2026-07-26 audit checked the earlier short plan only. It is not evidence that the current governance, N2.5 source contract, or N8 isolation requirements pass.

## Ledger rows to reconcile after outcome PASS

The ledger's current N16 entries are historical claims until this node completes its new approved-plan/outcome-audit loop. N16 may reconcile exactly these rows and no others:

| op | intended bounded evidence |
|----|---------------------------|
| code_def | source-scoped, line-oriented definition-like matches for one validated symbol |
| code_refs | source-scoped literal identifier-boundary matches for one validated symbol |
| code_callers | source-scoped call-like `symbol (` matches for one validated symbol |

`code_callees`, `code_flow`, `code_blast`, traversal caches, and every other code-intelligence row remain owned by their later nodes.

## Scope and exclusions

In scope: the N16 scanner API in `include/qbrain/codeintel/scan.hpp` and `src/qbrain/codeintel/scan.cpp`; the smallest source-filtered active-page enumeration change needed in `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`; the three N16 operation registrations in `src/qbrain/ops/handlers.cpp`; focused tests and native Windows evidence; and final reconciliation of only the three N16 ledger rows after both audits pass.

Out of scope: scanning files outside the selected brain database; scanning page titles, frontmatter, deleted bodies, or all sources at once; language parsing; symbol graphs; cross-page call resolution; overload/type resolution; comment/string-aware tokenization; tree-sitter or another dependency; N17-N28 implementation; schema changes; model/provider settings; commit or push; and any N30 plan, artifact, coordinator, evidence container, dependency, or follow-up.

## Deliverables

1. Source-filtered active-page enumeration:
   - Add a source-aware `Brain` enumeration path or overload used by N16 that applies `pages.source_id = ?` and `deleted_at IS NULL` in bound SQL before ordering and limiting. Do not fetch a cross-source page window and filter it in memory.
   - Preserve unrelated callers of the existing two-argument `list_pages` contract unless a separately audited plan requires changing them.
   - Within one source, order candidate pages by `updated_at DESC, id DESC`. Scan each page from line 1 upward. Result order is therefore page order followed by ascending line number, and repeated calls on an unchanged database are byte-stable JSON.
   - Apply `page_limit` only after the source and active-page predicates. Stop after exactly the effective hit `limit`; never return a hit from a different source or selected brain.
2. Canonical source and authorization contract:
   - All three operations accept `source_id`, defaulting to canonical `default`. Normalize and validate it according to N2.5, and require the source to exist without creating it from a read operation.
   - Local callers may read any registered canonical source. For remote callers, `default` is allowed and a non-default source must be present in the case-insensitive `mcp.allowed_sources` list. Reject an invalid, unknown, or unauthorized source before page enumeration.
   - Add canonical `source_id` to every JSON hit so the applied scope is explicit. A hit remains `{source_id, slug, line, snippet, kind}`; text output remains bounded and identifies only authorized hits.
3. Strict symbol contract and structured failures:
   - `symbol` is canonical. Preserve `name` as a documented compatibility alias when `symbol` is absent; if both are supplied with different non-empty values, reject the request.
   - Accept only 1..256-byte ASCII qualified identifier-like symbols matching components of `~?[A-Za-z_$][A-Za-z0-9_$]*` separated by `::`. Reject whitespace, control bytes, lone/repeated colons, empty components, regex metacharacters, trailing data, and overlength input.
   - Missing, conflicting, or invalid symbol input returns `ok=false`, a nonzero exit code, and valid JSON in the form `{"error":{"code":"invalid_argument","field":"symbol","message":"..."}}`. Do not echo the untrusted raw symbol. A valid symbol with no match remains a successful empty JSON array.
   - Keep regex construction escaped even after validation. Symbol input can never become executable regex syntax.
4. Strict bounded numeric contract:
   - `limit` defaults to 50 and clamps to 1..200. `page_limit` defaults to 500 and clamps to 1..2000.
   - Parse each supplied value as an entire unsigned base-10 decimal string. A syntactically valid zero clamps to 1 and a syntactically valid value above the maximum clamps to the maximum.
   - Signs, whitespace, decimals, suffixes, empty supplied values, and integer overflow are structured `invalid_argument` errors rather than defaults. Validation and authorization complete before the scan begins.
   - Use an N16-scoped strict parser or an already audited shared strict parser. Do not change permissive parsing behavior for unrelated operations as an incidental N16 side effect.
5. Honest heuristic behavior:
   - `code_def` remains a line-oriented subset covering the documented declaration forms already intended by the scanner: class/struct/interface/enum/type/namespace declarations, `function`/`async function`/`def`, const/let/var assignment or annotation, typed function-like declarations, and simple method/constructor forms.
   - `code_refs` performs a literal identifier-boundary match, so `foo` does not match `food`. `code_callers` requires the same boundary plus optional whitespace and `(`.
   - Hits use one-based line numbers, the exact operation kind (`def`, `ref`, or `call`), and a trimmed snippet capped at 200 bytes. JSON escaping must make quotes, backslashes, and control characters safe.
   - Basic leading-comment suppression may remain, but N16 does not claim complete comment/string removal, preprocessing awareness, Unicode identifier support, template/overload resolution, AST correctness, or zero false positives/negatives.
6. Read-only operation registration:
   - Register exactly `code_def`, `code_refs`, and `code_callers` as `Scope::Read`, with schemas that describe `symbol`/legacy `name`, `source_id`, `limit`, and `page_limit` consistently with runtime behavior.
   - These operations require no write enablement, enqueue no jobs, create no sources, touch no configuration, and execute no filesystem or network operation.
   - Invalid and unauthorized calls must fail through the same bounded structured-error contract and leave both the selected and decoy brain databases unchanged.
7. Focused verification artifacts:
   - Add a dedicated `tests/test_n16.cpp`; retain the historical `tests/test_codeintel.cpp` as regression context. Register the new test in `tests/test_main.cpp`, CMake, and `scripts/build-tests-cl.ps1` without removing or weakening another test.
   - Add `scripts/n16-verify.ps1` and `docs/nodes/n16-evidence/VERIFY-REPORT.md`, with captured build, full-suite, focused runtime/CLI, manifest, and snapshot-hash outputs under `docs/nodes/n16-evidence/`. Evidence reports facts only and are not audit verdicts.
   - After implementation and verification, obtain a new complete Claude Code outcome audit against this approved plan before setting this plan to `done` or reconciling the ledger.

## Tests

All verification runs on native Windows 11 PowerShell/MSVC x64 C++20. It must not require WSL or Docker.

1. Native build and regression baseline:
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1` and the produced `build\cl\qbrain_tests.exe` as applicable to the script contract.
   - Record exact commands, exit codes, Windows/x64 markers, full `cl.exe` version, `/std:c++20`, exact registered test count, and every test result. The full suite must be all PASS, include the dedicated N16 test, and remain at or above the N13 baseline of 21 tests.
2. Definition/reference/caller matrix:
   - Exercise every declared `code_def` subset with compact C++/TypeScript/Python-like lines and confirm one-based line, kind, slug, canonical source, and bounded snippet fields.
   - Prove `code_refs` finds definition and use lines but not `food` for `foo`; prove `code_callers` accepts `foo(` and `foo (` but not a bare reference or a longer identifier.
   - Include a valid no-match symbol and prove success with `[]`, distinct from invalid-symbol errors. Include quotes, backslashes, tabs, and a line longer than 200 bytes to prove valid JSON and truncation.
3. Symbol validation matrix:
   - Accept representative plain, `$`/underscore, destructor, and `ns::member` symbols within the declared grammar.
   - Reject missing input, conflicting `symbol`/`name`, whitespace, control input, `foo.bar`, `foo[0]`, regex metacharacters, lone/repeated/trailing colons, leading digit, byte length 257, and other malformed forms.
   - Every rejection returns the declared error code/field, does not fall back to an empty success, and preserves a full logical database snapshot.
4. Limit parser and clamp matrix:
   - Verify omitted defaults, `0`, `1`, exact maxima, and above-maximum numeric values for both limits. Prove the effective 1..200 hit cap and 1..2000 source-page cap.
   - Reject signs, surrounding/internal whitespace, decimal points, suffixes such as `1junk`, empty supplied values, and overflow. Each rejection happens before enumeration and preserves the complete snapshot.
5. Source, page-window, tombstone, and ordering matrix:
   - Seed `default` and `team_a` with overlapping slugs and symbols. Give out-of-scope pages newer timestamps than in-scope pages, use a small `page_limit`, and prove filtering occurs before limiting.
   - Soft-delete a matching page and prove its body is never returned. Create identical `updated_at` values with distinct ids and multiple matching lines, then assert `updated_at DESC, id DESC, line ASC` order and byte-identical repeated JSON.
   - Verify mixed-case `Team_A` resolves to canonical `team_a`, while an invalid or unknown source fails without creating a source or mutating any table.
6. Remote authorization and selected-brain isolation matrix:
   - Remote `default` succeeds. Remote `team_a` fails with an empty allowlist, succeeds when allowlisted case-insensitively, and a different non-default source remains denied. Authorization must occur before scanner invocation.
   - Create two physical brain databases containing the same source ids, slugs, and symbols but different sentinel snippets. Run all three operations against one selected brain and prove no sentinel from the other brain or another source appears.
   - Hash a full logical snapshot of schema plus every row in every user table for both databases before and after successful reads, empty reads, malformed calls, and denied calls; both snapshots must remain identical.
7. Registry, schema, and runtime smoke:
   - Discover and invoke exactly the three N16 operations through the operation registry and the real CLI/MCP serialization path. Validate `Scope::Read`, schemas, defaults, legacy alias behavior, JSON parseability, exit codes, deterministic order, and bounded output.
   - Run smoke fixtures under temporary databases and a temporary `LOCALAPPDATA`. Make no live provider/network call, do not scan the host filesystem, and do not persist a secret or dummy model setting.
8. Evidence manifest:
   - Record hashes of every N16 deliverable and the approved plan, exact commands/exit codes, full-suite and focused markers, effective clamp observations, authorization cases, selected/decoy brain snapshot hashes, and references to node-specific PASS gates through N13.
   - Assert the N16 plan/audit/evidence chain contains no N30 artifact or dependency and that no LLM/agent/application base URL, API key, provider, model, reasoning effort, context size, or compression threshold changed.

## Acceptance assertions (falsifiable)

1. N16 implementation starts only after a new Claude Code plan-only audit returns PASS and this plan is marked `approved`; the 2026-07-26 plan/audit and shared evidence are not used as gate substitutes.
2. Each operation filters active pages by the requested canonical source in SQL before applying `page_limit`, and returns no deleted, other-source, or other-brain body, slug, or snippet.
3. Local registered-source reads work without an allowlist; remote non-default reads work only when N2.5-authorized, and invalid, unknown, or unauthorized sources fail before page enumeration without creating a source.
4. The declared symbol grammar and 256-byte bound are enforced. Invalid input returns the structured error object and a nonzero exit code, while a valid symbol with no hits returns successful `[]`.
5. `limit` and `page_limit` consume the entire unsigned decimal input, use defaults only when omitted, clamp to exactly 1..200 and 1..2000, and reject signs, whitespace, suffixes, decimals, and overflow without scanning or mutation.
6. `code_def` finds the documented definition-like fixture forms, `code_refs` respects literal identifier boundaries, and `code_callers` requires a boundary plus optional whitespace and `(`, with no AST/semantic parity claim.
7. Every hit contains canonical source, slug, one-based line, operation kind, and a JSON-safe snippet of at most 200 bytes. Repeated unchanged calls return byte-identical JSON in `updated_at DESC, page id DESC, line ASC` order.
8. A small source-scoped `page_limit` is unaffected by newer pages in another source, and the effective hit limit is never exceeded.
9. All successful, empty, malformed, and denied N16 calls preserve full logical snapshots of both the selected and decoy databases; all three operations remain read-only and perform no job, config, filesystem, or network side effect.
10. Operation registry and real CLI/MCP smoke evidence match the declared argument schemas, source authorization, error behavior, exit codes, and output shape for exactly `code_def`, `code_refs`, and `code_callers`.
11. Native Windows x64 MSVC evidence records `/std:c++20`, exact commands and exit codes, an all-PASS full suite at or above the 21-test N13 baseline, the dedicated N16 test, runtime markers, and snapshot hashes before the new outcome audit.
12. After a complete Claude Code outcome-audit PASS, the plan becomes `done` and only the three N16 ledger rows are reconciled. No schema migration, third-party parser, full code-intelligence parity, model configuration change, N30 artifact, commit, or push is part of N16.

## Rollback

- Keep or make the three N16 operations unavailable if their source, authorization, validation, deterministic-order, or read-only contract cannot be maintained. Existing page/search and N1-N13 capabilities remain available.
- Revert the source-aware enumeration overload and scanner/handler wiring together; the existing database data and schema need no downgrade because N16 plans no migration.
- Keep remote non-default source access disabled by leaving `mcp.allowed_sources` empty. Never weaken authorization or widen the scan as a rollback workaround.
- If implementation discovers that a schema migration is required, stop implementation, return this plan to `draft`, revise it, and obtain a new Claude Code plan-audit PASS before making any schema edit.
- Restore only temporary test fixtures from their backups if verification fails. Do not modify production `%LOCALAPPDATA%\Qbrain` data, and do not commit or push unless the human user separately requests it.

## Security notes

- Snippets and slugs expose source-sensitive page content. Canonical source validation and remote allowlist authorization must complete before any page enumeration; default-deny applies to unauthorized non-default reads even though the operations are `Scope::Read`.
- Bind source and limit values in SQLite. The source predicate is part of the database query, not a post-query filter, preventing cross-source page-window leakage.
- Strict symbol grammar, length limits, escaping, hit/page caps, active-page filtering, and bounded snippets constrain regex/CPU/output abuse. No untrusted input becomes regex syntax, SQL, a path, a command, or executable content.
- Structured errors do not echo attacker-controlled symbol text, page bodies, configuration, environment values, tokens, provider URLs, model names, or secrets.
- Read-only evidence covers the whole logical database for both selected and decoy brains, not a single row count. Tests use temporary databases and a temporary `LOCALAPPDATA`, with no live network/provider request.
- Planning, implementation, verification, and auditing must not modify any LLM/agent/application base URL, API key, provider, model, reasoning effort, context size, or compression threshold.

## Dependencies and parallelism notes

- N16 depends only on completed N1-N13 contracts. It does not depend on N14, N15, N18, any later node, or N30. No `N30-*` file is created, read as a gate, or emitted as an N16 artifact.
- After plan approval only, disjoint slices may cover source-aware enumeration/scanner behavior, handler validation/authorization, and focused tests/evidence. No implementation slice may begin while the plan is `draft`.
- `src/qbrain/core/brain.cpp`, `src/qbrain/ops/handlers.cpp`, `tests/test_main.cpp`, CMake, and `scripts/build-tests-cl.ps1` are shared hot files in the N14/N15/N16/N18 wave. The parent agent owns sequencing, conflict resolution, integration review, the full native suite, evidence manifest, ledger reconciliation, and both N16 Claude Code gates.
- No subagent may mark N16 approved/done or author a PASS audit. Wave advancement waits for this node's own complete outcome-audit PASS.

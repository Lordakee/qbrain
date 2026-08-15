# N22 Plan - Source-scoped Heuristic Callees, Flow, Blast, and Cache Compatibility

**Status**: done
**Depends on**: Directly on N1, N2, N2.5, N7, N8, N11, and N16. N19 is the completed Wave 4 entry baseline (schema v12 and 26-test native baseline), not an N22 product-code dependency. N20, N21, N23, N24+, and N30 are not dependencies.
**Plan audit**: PASS (`N22-PLAN-AUDIT.md`, Claude Code, 2026-08-04; audited draft SHA-256 `696485222d71c86dff805d5d4d20b22d9433e190ff401de90017894e00a06a1c`; audit SHA-256 `bac3404aa98a492c6da00fb76b0f55325c7fa26ec1c486c3a1587539b691d986`)
**Outcome audit**: PASS (`N22-HARD-AUDIT.md`, Claude Code, 2026-08-07; SHA-256 `657601adc61d88fdb9b7e356fc3d764641312f0eb763b47d61b9a1d9a20cdcda`; fresh node-specific audit, not the 2026-07-29 N29 retrospective)
**Wave**: Wave 5; N22 may proceed in parallel with independent Wave 5 nodes only after each node has its own plan-audit PASS
**Process note**: Existing N22 code, tests, ledger claims, and historical audit files predate this refreshed plan. They are inputs to review, not proof of compliance. No N22 implementation, test, verifier, evidence, status, or ledger change may begin while this plan remains `draft`. After a fresh node-specific Claude Code plan-audit PASS, change only the status/audit metadata to `approved`, then implement against that exact approved-plan hash.

## Goal

Re-verify and, where necessary, correct the existing N22 capability as one bounded Windows-native C++20 node. The current N22 handlers are known to be non-compliant: they call legacy unscoped scanner APIs, lack N22 typed MCP validation and ambient-source exclusions, and register `code_traversal_cache_clear` with `local_only=false`. The approved implementation phase will replace/correct those paths; the historical implementation is not treated as already satisfying this plan.

1. `code_callees` identifies call-like symbols inside a definition's bounded brace-delimited body in active pages from exactly one authorized canonical source.
2. `code_flow` performs a deterministic, depth-limited breadth-first traversal over that same heuristic callee relation.
3. `code_blast` returns a deterministic one-hop neighborhood composed of definition, reference, caller, and callee hits.
4. `code_traversal_cache_clear` remains an honest administrative compatibility operation: Qbrain's scanner is stateless, so the operation clears zero cached rows and reports that fact without mutating storage.
5. Validation, authorization, output bounds, ordering, selected-brain isolation, MCP behavior, and read-only evidence are explicit and independently falsifiable.

N22 is deliberately a lightweight page-body heuristic. It is not tree-sitter, an AST, a compiler index, overload or type resolution, persisted call edges, upstream recursive blast parity, terminal/sink classification, or a real traversal cache. Those limits must appear in operation descriptions and ledger notes.

## Current risks to close

- The current N22 scanners call legacy unscoped `find_defs`, `find_refs`, `find_callers`, and `Brain::list_pages`; other-source pages can consume `page_limit` or leak snippets.
- Current N22 handlers do not accept or authorize `source_id`, omit canonical source from results, use permissive integer parsing, accept undeclared `name`/`page_limit` inputs, and expose schemas without `additionalProperties=false` or effective bounds.
- N22 operations are absent from the MCP typed-argument gate and ambient-source exclusion list. Wrong JSON types can be stringified, unknown fields can pass through, and `QBRAIN_SOURCE` can silently alter an omitted source.
- `find_callees` can scan to the end of a page after finding a definition, so calls in later unrelated definitions can be attributed to the requested symbol. If no definition exists, a page containing the symbol text can still yield false callees.
- `find_flow` emits repeated edges before applying its seen set, and current blast concatenation can contain duplicate exact hits. Neither path has source-scoped deterministic evidence or strict depth/limit bounds.
- `code_traversal_cache_clear` is advertised as `Scope::Admin` but is not `local_only`, accepts ignored arguments, returns no structured JSON, and has no proof that the compatibility no-op leaves storage unchanged.
- The historical combined `test_n20_23.cpp` covers one database, one default source, and a happy path only. It does not prove definition boundaries, cycles, exact ordering, authorization, typed MCP rejection, tombstones, limits, or full-database immutability.

## Ledger rows to reconcile after outcome PASS

The ledger's current N22 entries are historical claims until this refreshed node completes its own approved-plan, implementation-evidence, and outcome-audit loop. N22 may reconcile exactly these rows and no others:

| op | intended bounded evidence after closure |
|----|-----------------------------------------|
| code_callees | source-scoped brace-body heuristic callee occurrences; no AST/semantic-resolution claim |
| code_flow | source-scoped deterministic breadth-first heuristic callee traversal with strict depth/result bounds |
| code_blast | source-scoped deterministic one-hop def/ref/caller/callee neighborhood; not upstream transitive blast parity |
| code_traversal_cache_clear | guarded Admin compatibility no-op returning zero because Qbrain has no traversal cache |

N22 adds no new operation count beyond those four historical rows. Final reconciliation updates their notes/evidence only after outcome PASS.

## Scope and exclusions

In scope: the N22 scanner API and implementation in `include/qbrain/codeintel/scan.hpp` and `src/qbrain/codeintel/scan.cpp`; the four N22 registrations in `src/qbrain/ops/handlers.cpp`; the smallest N22 additions to MCP typed-argument and ambient-source policy in `src/qbrain/mcp/server.cpp`; a dedicated N22 test, native build wiring, node-specific verifier/evidence, and final reconciliation of exactly the four ledger rows.

Existing N16 source-filtered active-page enumeration, symbol grammar, UTF-8-safe snippet handling, source resolution, and strict bounded-number helpers should be reused rather than weakened or duplicated. Legacy unscoped scanner APIs may remain for compatibility, but no N22 registered operation may call them.

Out of scope: schema or migration changes; filesystem/repository crawling; page title/frontmatter/deleted-body scanning; cross-source or `__all__` traversal; persisted code edges or caches; comment/string-aware parsing; Unicode identifiers; qualified-name disambiguation; exact overload/type resolution; upstream `depth_groups`, confidence, `did_you_mean`, truncation/cycle envelopes, terminal-node tags, or `exact` mode; N20/N21/N23 work; a new dependency; model/provider configuration; commit/push; and every N30 artifact or coordination role.

## Deliverables

1. Source-scoped N22 scanner APIs:
   - Add and use `find_callees_in_source`, `find_flow_in_source`, and `find_blast_in_source` (or equivalently explicit source-required APIs) whose first page enumeration is the N16 audited `Brain::list_pages_for_source` path.
   - These APIs do not exist in the current header and must be newly declared/implemented. Replace all three current N22 handler calls to legacy `find_callees`, `find_flow`, and `find_blast` with the new source-required APIs; the current unscoped calls are explicitly non-compliant.
   - Apply `pages.source_id = ?` and `deleted_at IS NULL` in bound SQL before `page_limit`, ordered `updated_at DESC, page id DESC`; scan a page from line 1 upward and call tokens left-to-right.
   - Populate canonical `source_id` on every hit. Preserve one-based line numbers and N16's valid UTF-8, JSON-safe, at-most-200-byte trimmed snippets.
2. Honest callee-body heuristic:
   - Require a valid requested symbol and at least one matching N16 definition-like line. A mere symbol reference on a page with no matching definition yields no callee.
   - For N22, a traversable definition body is brace-delimited. Starting on the matched declaration line, scan that line and at most the next 10 physical lines for the first `{` byte; whitespace-only lines may be skipped, but the scan never proceeds beyond those 10 lines. If no `{` is found, emit no callee for that definition. Start depth at that first brace, increment for every `{` byte and decrement for every `}` byte (the heuristic is intentionally not comment/string aware), and stop at the first line where depth returns to zero.
   - Extract ASCII call-like tokens matching `[A-Za-z_][A-Za-z0-9_]*` followed by optional whitespace and `(`. Exclude control keywords and the declaration occurrence itself; a recursive self-call inside the body remains a legitimate callee occurrence.
   - Do not attribute calls after the matching close or inside a later definition to the requested symbol. Multiple matching definitions are processed in deterministic page/line order. Raw brace and token scanning remains an acknowledged heuristic and does not claim string/comment awareness.
   - Emit callee occurrences as `{source_id, slug, line, snippet, kind}` with `kind="callee:<target>"`; remove duplicate exact occurrences keyed by source, slug, line, and kind while retaining distinct lines.
3. Deterministic bounded flow:
   - Traverse the source-scoped callee relation breadth-first from the requested root. Emit only the first discovery of each target, with `kind="flow:d<N>:<target>"`, in deterministic parent and callee order.
   - Seed the seen set with the root so self-cycles and longer cycles terminate. A node may be expanded at most once; traversal stops at the effective depth or global result limit.
   - `depth` defaults to 2 and clamps to 1..8. `limit` defaults to 50 and clamps to 1..200. `page_limit` defaults to 500 and clamps to 1..2000 for each bounded source-scoped expansion.
   - A valid root with no definition/callees returns successful `[]`; no synthetic path, fallback source, or whole-database scan is allowed.
4. Deterministic bounded blast subset:
   - Build a one-hop neighborhood from the source-scoped N16 definition/reference/caller results plus N22 callees for the same requested symbol.
   - Preserve deterministic category priority `def`, `ref`, `call`, `callee`, then the stable order within each category. De-duplicate blast rows by `(source_id, slug, line)` across categories, retaining only the first occurrence under that priority (for example, a line classified as both `def` and `ref` emits only `def`). De-duplicate repeated rows within a category by the same key, then apply one global limit after assembly.
   - `limit` defaults to 80 and clamps to 1..200; `page_limit` defaults to 500 and clamps to 1..2000. The operation description and ledger must say one-hop heuristic subset, not recursive upstream blast parity.
5. Shared strict operation contract:
   - `symbol` is canonical for `code_callees` and `code_blast`; `name` remains a compatibility alias. `code_flow` accepts canonical `entry_point` and preserves `symbol`/`name` aliases. Multiple supplied non-empty aliases are accepted only when byte-identical; conflicts use the canonical field in a structured error.
   - Reuse N16's 1..256-byte qualified ASCII identifier grammar. Missing, empty, conflicting, malformed, control-containing, or overlength symbols return nonzero `invalid_argument` errors without echoing raw input. A valid no-match symbol returns successful `[]`.
   - Parse every supplied number as a complete unsigned base-10 decimal. Defaults apply only when omitted; syntactically valid zero and above-maximum values clamp. Empty, sign, whitespace, decimal, suffix, and overflow inputs fail before page enumeration.
   - Every read defaults only an omitted `source_id` to canonical `default`, ignores ambient `QBRAIN_SOURCE`, verifies source existence without creation, and uses N2.5 authorization before scanning. Local registered sources need no allowlist; remote non-default sources require case-insensitive `mcp.allowed_sources`; `allow_write=true` cannot bypass read authorization.
6. Registrations and MCP validation:
   - Replace the current loose registrations with `code_callees`, `code_flow`, and `code_blast` as non-local-only `Scope::Read` operations, with accurate descriptions and exact `additionalProperties=false` schemas for their canonical fields, aliases, source, limits, defaults, minima, and maxima.
   - Add the currently missing MCP typed-argument maps for all three reads. Reject non-object arguments, unknown fields, null, booleans, objects/arrays, signed/floating numeric JSON, and wrong string types before dispatch. Add the reads to the currently missing ambient-source exclusion policy.
   - Return JSON arrays whose rows have exactly `source_id`, `slug`, `line`, `snippet`, and `kind`; text output is bounded and contains no body, path, config, token, provider, or unauthorized sentinel.
7. Guarded stateless-cache compatibility operation:
   - Correct the current `code_traversal_cache_clear` registration from `local_only=false` to `local_only=true` while keeping `Scope::Admin`, so remote MCP is denied unless explicit write enablement is present. Give it an exact empty-object schema with `additionalProperties=false` plus an empty typed MCP map/ambient exclusion.
   - The handler accepts no arguments, performs no database/file/config/job/network mutation, and returns deterministic structured JSON such as `{"cleared":0,"stateless":true}` plus matching bounded text. It must not pretend a cache existed or was persisted.
   - Test direct trusted-local success, remote default denial, explicit authorized MCP compatibility success under the repository's existing `--allow-write` contract, and rejection of every unexpected argument without mutation.
8. Focused verification artifacts:
   - Add `tests/test_n22.cpp`; register it in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` without removing or weakening any existing test. Keep `tests/test_n20_23.cpp` as historical regression context only.
   - Add `scripts/n22-verify.ps1` and factual evidence under `docs/nodes/n22-evidence/`, including a manifest and `VERIFY-REPORT.md`. Evidence is not an audit verdict.
   - Build production and tests through the native PowerShell/MSVC paths, run the complete suite twice from the same frozen inputs, and obtain a fresh node-specific Claude Code outcome audit before changing the plan to `done` or reconciling ledger notes.

## Tests

All verification runs on native Windows 11 PowerShell, x64 MSVC, and C++20. It must not require WSL, Docker, a live network/provider call, or production `%LOCALAPPDATA%\Qbrain` data.

1. Native build and frozen-input regression evidence:
   - Run `scripts/build-cl.ps1` and `scripts/build-tests-cl.ps1` using their documented PowerShell contracts; run `build\cl\qbrain_tests.exe` twice after freezing the scoped input manifest.
   - Record exact commands/working directories, exit codes, Windows build, architecture, full `cl.exe` version, `/std:c++20`, production/test binary hashes, registered test count, and every PASS/FAIL line. Both full-suite runs must be all PASS, include the dedicated N22 test exactly once, and remain at or above the N19 baseline of 26 registered tests.
2. Callee definition/body matrix:
   - Cover same-line and next-line opening braces, nested blocks, multiple calls on one line, repeated targets on distinct lines, recursive self-call, multiple matching definitions, and a later unrelated definition on the same page.
   - Prove the declaration token is not emitted, control keywords and longer identifiers are excluded, calls after the matching close do not leak, unmatched/unbalanced definitions do not consume unrelated text, and a reference-only page produces successful `[]`.
   - Assert exact source/slug/line/snippet/kind and stable left-to-right/page order. Include quotes, backslashes, tabs, malformed UTF-8 source bytes, and a >200-byte line to verify valid bounded JSON rather than serialization failure.
3. Flow matrix:
   - Seed `root -> alpha,beta`, `alpha -> gamma`, `beta -> gamma,root`, `gamma -> delta`; verify exact breadth-first membership/order/kind at depth 1, 2, 3, and above-max clamp.
   - Prove shared descendants and cycles are emitted/expanded only under the declared first-discovery rule, no node is expanded twice, a small global limit is exact, and repeated calls serialize byte-identically.
4. Blast matrix:
   - Seed separate and overlapping definition, reference, caller, and callee evidence for one symbol and assert exact category order, first-category-wins `(source_id, slug, line)` de-duplication, default/min/max/over-max limits, selected source, active pages, and successful empty behavior.
   - Assert no `depth_groups`, confidence, terminal-node, cache, AST, or transitive-upstream claim appears in output or descriptions.
5. Symbol, alias, and numeric rejection matrix:
   - Reuse the N16 valid/malformed symbol matrix. Exercise `symbol`, `name`, `entry_point`, equal aliases, conflicting aliases, omitted/empty values, byte 256/257 boundaries, and control/NUL input.
   - For `limit`, `page_limit`, and `depth`, exercise omitted, 0, 1, exact maximum, above maximum, empty, plus/minus signs, leading/trailing/internal whitespace, decimal, suffix, and unsigned overflow. Assert exact error code/field, nonzero exit, no raw-input echo, no scan, and unchanged snapshots.
6. Source, tombstone, ordering, authorization, and brain isolation matrix:
   - Seed `default` and `team_a` with overlapping slugs/symbols plus newer deleted and out-of-source pages. Use a small `page_limit` to prove source/active predicates precede limiting.
   - Create a decoy physical brain with stronger overlapping hits. Exercise all three reads for omitted/default, mixed-case registered source, invalid/reserved/overlength source, valid unknown source, remote default, remote denied non-default, case-insensitive allowlisted non-default, and `allow_write=true` unauthorized non-default.
   - Set ambient `QBRAIN_SOURCE` to a non-default value and prove omission still selects `default`. No other-source/decoy/deleted sentinel may appear.
7. Registry, real MCP, and cache-clear matrix:
   - Inspect exactly four N22 registrations and `tools/list` definitions for scope, locality, description, exact schema/defaults/bounds, aliases, and `additionalProperties=false`.
   - Exercise real `tools/call` success/empty/clamp cases and non-object, wrong-type, null, signed/floating number, unknown-field, conflicting-alias, unknown-source, and denied-source failures with writes disabled.
   - For cache clear, prove local success and deterministic zero/stateless response, remote default denial, explicit authorized compatibility success, unexpected-argument rejection, and no database or filesystem change.
8. Read-only and schema matrix:
   - Use fresh and populated schema-v12 disposable databases. Reopen the populated database and prove N22 requires no migration, schema/config edit, or cache table.
   - Before and after every success, empty result, clamp, malformed input, unknown/denied source, and cache-clear call, hash schema SQL, `sqlite_sequence`, and every column/row of every Qbrain application table in both selected and decoy brains. Every pair must match.
9. Evidence manifest:
   - Record approved-plan and plan-audit hashes; dependency evidence; all scoped production/header/test/verifier/build inputs; SQLite input; production/test binaries; exact commands/exits; both full-suite runs; focused matrix markers; and every selected/decoy before/after snapshot pair.
   - Record that schema stayed v12; no N30-prefixed file was created, read, or required; the manifest and all build/test/runtime logs contain no N30 file, operation, or coordination reference; no later-node artifact supplied N22 behavior; no production data or secret was touched; no LLM/agent/application model, provider, base URL, API key, reasoning effort, context size, or compression threshold changed; and no commit/push occurred.

## Acceptance assertions (falsifiable)

1. All three read operations apply canonical source and active-page predicates before `page_limit`, and return no deleted, other-source, or other-brain slug/snippet/callee.
2. Omitted source means `default` regardless of ambient environment; local registered sources work without an allowlist; remote non-default reads require N2.5 authorization; invalid, unknown, or denied sources fail before enumeration without source creation, and `allow_write` does not broaden reads.
3. The declared symbol/alias grammar and 256-byte bound are enforced with structured nonzero errors. Valid no-match inputs return successful `[]`, while conflicting aliases and malformed inputs do not scan or echo raw values.
4. Every numeric field consumes the entire unsigned decimal, defaults only when omitted, clamps to its exact declared range, and rejects empty/sign/whitespace/decimal/suffix/overflow input before scanning.
5. `code_callees` emits only deterministic call-token hits within matched balanced brace-body windows, includes legitimate recursive calls, and never leaks calls from a reference-only page or a later unrelated definition.
6. `code_flow` emits deterministic breadth-first first-discovery hits through the effective depth, stops at the global limit, and terminates cycles without duplicate expansion.
7. `code_blast` returns the deterministic, first-category-wins line-deduplicated one-hop `def`, `ref`, `call`, `callee` subset under one global limit, with no transitive/upstream envelope claim.
8. Every read hit has exactly canonical source, slug, one-based line, valid UTF-8 snippet of at most 200 bytes, and the declared kind. Repeated unchanged calls are byte-identical in the specified order.
9. Registry and real MCP evidence matches the exact schemas, typed rejection, aliases, scopes, source policy, output shapes, and write-disabled behavior for all four N22 operations.
10. `code_traversal_cache_clear` is a guarded Admin compatibility no-op that accepts no arguments, deterministically reports zero/stateless, and leaves database, filesystem, jobs, and config unchanged on every allowed or rejected path.
11. Every N22 success/failure path preserves complete selected/decoy logical snapshots and schema v12; N22 adds no migration, table, persisted cache, job, file write, config write, network/provider call, or protected model-setting change.
12. Frozen native Windows x64 MSVC C++20 evidence shows successful production/test builds and two all-PASS full-suite runs at or above 26 registered tests, including the dedicated N22 test, focused markers, manifest hashes, and no N30 input/reference before outcome audit.
13. Only after a fresh complete Claude Code outcome-audit PASS may this plan become `done` and exactly the four historical N22 ledger notes be reconciled. N20/N21/N23+, N30, commit, and push remain outside N22.

## Rollback

- Keep or make the three N22 read operations unavailable if source isolation, authorization, body bounding, deterministic traversal, validation, or read-only behavior cannot be maintained. Do not restore the historical unscoped handlers as a fallback.
- Revert the source-scoped N22 APIs, handler/MCP wiring, dedicated tests, and verifier together while preserving the completed N16 contracts and unrelated Wave 5 work.
- Keep the cache compatibility operation default-denied remotely by retaining `local_only=true`; if its no-op contract cannot be represented honestly, remove it from the advertised registry and return this plan to `draft` rather than inventing cache state.
- No database downgrade/data rewrite is needed because N22 plans no schema change. If implementation discovers a required migration, cache table, third-party parser, or filesystem index, stop, return the plan to `draft`, revise scope/rollback/security/tests, and obtain another Claude Code plan-audit PASS before editing those areas.
- Verification uses only unique disposable databases and temporary `LOCALAPPDATA`. Restore only disposable fixture backups after failure; never modify production `%LOCALAPPDATA%\Qbrain` data. Do not commit or push unless the human user separately requests it.

## Security notes

- Code snippets, slugs, call relationships, and even symbol existence are source-sensitive. Validate/canonicalize the source, verify existence, and authorize remote access before any page/body enumeration.
- Bind source and limits in SQLite through the N16 audited enumeration path. Filtering in memory after a global page window is prohibited because it leaks data and permits out-of-scope rows to consume the limit.
- Strict symbol grammar, full-string numeric parsing, depth/page/result caps, active-page filtering, bounded brace traversal, visited sets, and UTF-8-safe snippets constrain regex, CPU, memory, and output abuse. No input becomes SQL syntax, a path, command, or executable content.
- Structured errors and text/JSON responses must not expose raw hostile input, page bodies beyond authorized bounded snippets, database paths, configuration, environment values, auth tokens, provider/model data, or another source/brain sentinel.
- Read operations work with MCP writes disabled. Non-default read authorization remains fail-closed even if `allow_write=true`. The Admin compatibility no-op remains remote-default-deny under the repository's explicit write-enable contract.
- Tests use temporary roots and dummy sentinels only. Planning, implementation, verification, and auditing must not modify any LLM/agent/application base URL, API key, provider, model, reasoning effort, context size, compression threshold, or related protected configuration.

## Dependencies and parallelism notes

| node | plan-audit SHA-256 | outcome-audit SHA-256 | contract consumed by N22 |
|------|-----------------------|--------------------------|--------------------------|
| N1 | `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` | read operation and MCP write-default-deny |
| N2 | `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` | `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` | active-page and soft-delete semantics |
| N2.5 | `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` | `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` | canonical source ids and remote source authorization |
| N7 | `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` | `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` | authenticated loopback MCP/tool framing |
| N8 | `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` | selected-brain routing and isolation |
| N11 | `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` | `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` | native Windows build/test evidence |
| N16 | `ad6794067444a56658d52d23c3ca29f7092cd7024829f1c4313b295b10c77fef` | `591865f6647e175c4aa02ec90abad1075c554eca49e3a15e5f63ad1639c24aba` | source-filtered scanner, symbol grammar, snippets, strict args |
| N19 wave entry | `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470` | `d4ee4ad14e3768b5470865f092a783ba0d10b9e9155bfb17c4bd5ce594ad4f24` | completed Wave 4, schema v12, 26-test baseline |

- Before implementation, verify every listed audit still exists, states PASS, and matches the recorded hash. A missing/non-PASS/changed dependency blocks implementation pending review and a revised plan audit; another node's ledger text or shared evidence cannot substitute.
- The plan-audit gate must explicitly read and verify `docs/nodes/N7-HARD-AUDIT.md` as `VERDICT: PASS` with SHA-256 `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421`, and `docs/nodes/N8-HARD-AUDIT.md` as `VERDICT: PASS` with SHA-256 `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9`. These are explicit dependency facts, not historical ledger assumptions.
- N20, N21, and N23 may be developed in parallel only if their own plan audits pass and the parent assigns exclusive ownership of shared hot files. `src/qbrain/codeintel/scan.cpp`, `src/qbrain/ops/handlers.cpp`, `src/qbrain/mcp/server.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` require serialized integration.
- After N22 approval, disjoint subagent slices may cover scanner/body traversal, handler/MCP contracts, and focused tests/verifier. The parent owns dependency verification, merge review, frozen manifest, native production/test builds, both complete suite runs, ledger reconciliation, and both Claude Code gates.
- No subagent may mark N22 approved/done, edit an audit into PASS, or start implementation while this plan is `draft`. N22 closes only through its own node-specific audit artifacts; no N30 file is created, read, or required.

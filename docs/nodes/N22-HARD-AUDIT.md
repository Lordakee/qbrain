# N22 HARD AUDIT (outcome)

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: `docs/nodes/N22-PLAN.md` (`Status: approved`; SHA-256 `8ae960dfd88ab43f91605c39cc963843e1c5866f61b94691f700393cc984d2b4`)
**Plan audit**: `docs/nodes/N22-PLAN-AUDIT.md` (`VERDICT: PASS`, Claude Code, 2026-08-04; SHA-256 `bac3404aa98a492c6da00fb76b0f55325c7fa26ec1c486c3a1587539b691d986`)
**Date**: 2026-08-07
**Audit type**: Fresh node-specific outcome hard audit against the approved refreshed plan. The 2026-07-29 N29 retrospective that previously occupied this file was **not** used as evidence and is superseded by this document.

## Audit basis

This audit re-derived its facts from the current worktree. It did not accept any prior audit, ledger note, or verifier summary as proof.

Read and independently checked:

- `AGENTS.md`, `docs/nodes/README.md`, `docs/nodes/_TEMPLATE-HARD-AUDIT.md` (gate rules, ordered loop, required artifact shape)
- `docs/nodes/N22-PLAN.md`, `docs/nodes/N22-PLAN-AUDIT.md`
- `include/qbrain/codeintel/scan.hpp`, `src/qbrain/codeintel/scan.cpp` (878 lines)
- `src/qbrain/ops/handlers.cpp` (N22 registrations and shared validation helpers), `src/qbrain/mcp/server.cpp`
- `src/qbrain/core/brain.cpp` (`Brain::list_pages_for_source`), `tests/wave3_test_support.hpp` (`logical_snapshot`)
- `tests/test_n22.cpp` (1778 lines), `tests/test_main.cpp`, `CMakeLists.txt`, `scripts/build-tests-cl.ps1`
- `scripts/n22-verify.ps1` (193 808 bytes) — fingerprint scope, two-run equivalence, registry/schema assertions, governance binding
- All 14 files under `docs/nodes/n22-evidence/`
- `docs/OPS-PARITY-LEDGER.md`, git worktree/HEAD state

Independently computed with `sha256sum` and compared (all matched):

| Artifact class | Result |
|---|---|
| 7 live N22 inputs (`scan.hpp`, `scan.cpp`, `handlers.cpp`, `server.cpp`, `test_n22.cpp`, `test_main.cpp`, `n22-verify.ps1`) | all 7 equal the manifest's recorded hashes |
| 2 binaries (`build/cl/qbrain.exe`, `qbrain_tests.exe`) | equal `84c3427a…f71a` / `a1491acd…4ac0` |
| 13 evidence files listed in `output_files` | all 13 equal recorded hashes |
| 4 N19 schema-v12 storage inputs | byte-identical to the recorded baseline |
| 16 dependency audit hashes (N1, N2, N2.5, N7, N8, N11, N16, N19 × plan+outcome) | all 16 equal the plan's dependency table; each file states PASS |
| Git HEAD | `5ced8ccb511672536d0f9767a2bc1777baf561ab`, branch `main` — equals manifest `git.head` |

Because every recorded input hash still matches the file on disk, the evidence set is **fresh against the current worktree**, not stale.

## Comparison to the approved plan

The approved plan is corrective: it declares the pre-existing N22 handlers non-compliant (legacy unscoped APIs, no typed MCP gate, no ambient exclusion, `local_only=false`) and requires those paths to be replaced. Each declared non-compliance is now closed in current code:

| Plan-declared defect | Current state |
|---|---|
| N22 handlers call legacy unscoped `find_callees`/`find_flow`/`find_blast` | Handlers call `find_callees_in_source` / `find_flow_in_source` / `find_blast_in_source`. Legacy symbols retain **zero** callers anywhere outside their own definitions (plan permits them to remain). |
| No `source_id` acceptance/authorization; permissive parsing; loose schemas | `parse_n22_source_request` → `validate_allowed_args` → alias parse → `parse_bounded_uint` → `resolve_source(require_existing=true)`; schemas carry `additionalProperties=false` with exact fields, defaults, minima, maxima. |
| N22 absent from MCP typed gate and ambient-source exclusion | `typed_argument_schema` returns maps for all three reads and `no_arguments` for cache-clear; all four ops are excluded in `uses_ambient_source`, so `QBRAIN_SOURCE` is never injected. |
| `find_callees` scans to end of page / false callees without a definition | `find_opening_brace` bounded to declaration line + 10 lines, aborts on `;`-terminated declarations and on an intervening independent definition; `find_closing_brace` tracks depth and stops at zero. |
| `find_flow` emits repeated edges; blast contains duplicates | Root-seeded `seen` set, per-parent target de-duplication, first-discovery emission; blast de-duplicates by `(source_id, slug, line)` under fixed category priority. |
| `code_traversal_cache_clear` not `local_only`, no structured JSON | Registered `Scope::Admin` with `local_only=true`; returns `{"cleared":0,"stateless":true}`; rejects every argument via `validate_allowed_args(ctx, {})`. |
| Historical combined `test_n20_23.cpp` proves only a happy path | Dedicated `tests/test_n22.cpp` wired into `test_main.cpp`, `CMakeLists.txt`, `build-tests-cl.ps1`; `test_n20_23` retained unweakened as historical regression context. |

No out-of-scope area was touched: the scoped diff covers exactly the 9 declared N22 paths (`changed_path_count=9`), and `protected_path_change_count` / `protected_assignment_change_count` are both 0.

## Acceptance

| # | Assertion from approved plan | Evidence | Status |
|---|---|---|---|
| 1 | Source + active predicates precede `page_limit`; no deleted/other-source/other-brain leakage | `brain.cpp:352-357` binds `source_id=? AND deleted_at IS NULL ORDER BY updated_at DESC, id DESC LIMIT ?`; all three reads enter via `load_n22_pages` → `list_pages_for_source`. `test_n22.cpp:930-969` uses `page_limit=2` against newer deleted + out-of-source pages and asserts exact 2-row results for all three ops. Decoy brain seeded with `DECOY_BRAIN_SENTINEL`; 367 snapshot rows show selected ≠ decoy and no sentinel in output. | PASS |
| 2 | Omitted source ⇒ `default` regardless of ambient; local needs no allowlist; remote non-default needs N2.5 auth; `allow_write` cannot broaden reads | All four ops excluded in `server.cpp:88-89` `uses_ambient_source`, so the `QBRAIN_SOURCE` injection at `server.cpp:345` cannot apply. `resolve_source` (`handlers.cpp:374-393`) canonicalises → `remote_source_allowed` → `source_exists` (no creation). `remote_source_allowed` never reads `allow_write`. Runtime: `mcp:real:<op>:ambient-default` (with `QBRAIN_SOURCE=team_a`) returns `default` data; `…:denied-source`, `…:unknown-source`, `…:write-does-not-authorize` per op. | PASS |
| 3 | Symbol/alias grammar + 256-byte bound; valid no-match ⇒ `[]`; conflicts/malformed do not scan or echo | `is_valid_symbol` (`scan.cpp:380-394`) enforces 1..256-byte qualified ASCII with `::` and optional `~`. `parse_n22_symbol` fails empty, missing, and non-byte-identical aliases with `invalid_argument` on the canonical field. Markers: `alias:<op>:{canonical,symbol,name,all-equal,conflict,empty,missing,unexpected}`, `symbol:<op>:valid-no-match:*`, `symbol:<op>:invalid:*`. Errors carry static messages only (`argument_error`, `handlers.cpp:57-66`). | PASS |
| 4 | Every numeric consumes the whole unsigned decimal, defaults only when omitted, clamps exactly, rejects malformed before enumeration | `parse_bounded_uint` (`handlers.cpp:124-145`) uses `from_chars` and requires `parsed.ptr == last`, rejecting sign, whitespace, decimal, suffix and overflow; then `std::clamp`. Applied before `resolve_source` and before any page read. Markers cover omitted / 0 / 1 / max / over-max / empty / signs / whitespace / decimal / suffix / overflow for `limit`, `page_limit`, `depth`. | PASS |
| 5 | `code_callees` emits only deterministic tokens inside matched balanced brace bodies, includes recursion, never leaks reference-only or later-definition calls | `for_each_definition_callee` (`scan.cpp:624-654`) scans only `[opening…closing]` with column bounds at both ends. Fixture `n22/body` includes `AFTER_CLOSE_SENTINEL();` and `void Unrelated() { LATER_LEAK_SENTINEL(); }`; the asserted 6-row expectation contains neither. `local:code_callees:recursive` asserts the self-call is emitted; `…:empty:RefOnlyRoot`, `…:PrototypeRoot`, `…:UnbalancedRoot`, `…:brace-window-boundary`, `…:TooFarRoot` assert empty. | PASS |
| 6 | `code_flow` emits deterministic BFS first-discovery to effective depth, stops at global limit, terminates cycles without duplicate expansion | `find_flow_in_source` (`scan.cpp:730-765`) seeds `seen` with the root and expands each node at most once. Plan's exact seed graph is present (`flow-root/alpha/beta/gamma`). Depth 1/2/3 assert exact row sets and `flow:d<N>:` kinds; repeat call asserts `flow:d2:FlowRoot` absent (cycle terminated) and `flow:d2:Gamma` appears exactly once (shared descendant emitted once). `depth-max`, `depth-over-max`, `global-limit`, `deterministic-repeat` present. | PASS |
| 7 | `code_blast` returns first-category-wins line-deduplicated one-hop def/ref/call/callee under one global limit, with no transitive claim | `find_blast_in_source` (`scan.cpp:767-792`) appends in `def → ref → call → callee` order behind a `(source_id, slug, line)` set and bounds each append by `limit`. `local:code_blast:priority-dedup` asserts the exact 4-row shape; `…:global-limit-one` asserts exactly the `def` row; output and descriptions are asserted free of `depth_groups`, `confidence`, `terminal_node`, `ast_result`, `transitive_upstream`. | PASS |
| 8 | Hits carry exactly canonical source, slug, 1-based line, valid UTF-8 ≤200-byte snippet, declared kind; repeats byte-identical | `n22_hits_to_result` emits exactly `source_id, slug, line, snippet, kind`; `require_hit_shape` enforces the key set. `trim_snippet` → `bounded_utf8(…, 200)`. `local:code_callees:utf8-disclosure` asserts tab, quote, backslash retention, `U+FFFD` replacement for malformed bytes, and `size() <= 200`. `deterministic-repeat` asserts identical `json` and `text`. | PASS |
| 9 | Registry and real MCP evidence match exact schemas, typed rejection, aliases, scopes, source policy, output shapes, write-disabled behaviour for all four ops | `REAL-MCP-EVIDENCE.json`: 47 probes over two real `qbrain.exe serve` stdio sessions (writes disabled / `--allow-write`), 33 structured errors, covering per-op success, empty, clamp, non-object, unknown-field, wrong-type symbol, null source, signed and floating numerics, alias conflict, unknown source, denied source, ambient default, and write-does-not-authorize. Verifier asserts `tools/list` exposes exactly four N22 tools with exact property sets, bounds, `additionalProperties=false`, required limitation phrases, and no affirmative AST/tree-sitter/compiler-index/resolution/persisted-edge claim. | PASS |
| 10 | Cache clear is a guarded Admin no-op: no arguments, deterministic zero/stateless, nothing mutated on any path | `handlers.cpp:2570-2582` — `Scope::Admin`, `local_only=true`, empty-object schema, `validate_allowed_args(ctx, {})`. Runtime: `mcp:real:cache:remote-denied` → `write_denied`; `…:remote-allowed` (allow_write) → `{"cleared":0,"stateless":true}`; `…:unexpected` and `…:non-object` rejected. Test paths wrapped in filesystem-snapshot comparisons plus the logical-snapshot matrix. | PASS |
| 11 | Every path preserves selected/decoy logical snapshots and schema v12; no migration, table, cache, job, file write, config write, network call, or protected-setting change | `SnapshotMatrix::run` compares full `logical_snapshot` **strings** (schema SQL, `sqlite_sequence`, and every row/column of every table) for both brains before/after each call and throws on difference, in addition to hash equality. 367 rows recorded with `before == after` in every row; verifier asserts contiguous indexes, exact label order and exact label set. Runtime schema markers `schema:fresh-v12`, `schema:populated-reopen-v12`, `schema:final-v12` appear exactly once each; real-binary doctor reports `schema_version 12`, `ok=true`; the 4 storage inputs are byte-identical. | PASS |
| 12 | Frozen native Windows x64 MSVC C++20 evidence: successful builds and two all-PASS full-suite runs at ≥26 registered tests including the dedicated N22 test, focused markers, manifest hashes, no N30 | Run 1 = `TEST-BUILD-OUTPUT.txt` (`stage=test-build-and-suite-run-1`, `suite_run_index=1`): 29 PASS / 0 FAIL, exit 0. Run 2 = `FULL-SUITE-RUN-2.txt` (`stage=full-suite-run-2`): 29 PASS / 0 FAIL, exit 0. Same test binary hash in both. `[PASS] n22` appears exactly once per run; 29 ≥ the N19 baseline of 26. Platform: Windows 11 build 22624, x64, `cl.exe 19.51.36248`, `/std:c++20`. Zero N30 files exist and zero N30 tokens appear anywhere in the evidence directory. | PASS |
| 13 | Only after a fresh Claude Code outcome PASS may the plan become `done` and exactly the four ledger notes be reconciled | Plan still reads `Outcome audit: pending`; manifest `state = "verified-pending-claude-outcome-audit"`; `VERIFY-REPORT.md` states it is not an audit verdict and that a fresh audit remains blocking. Ledger still shows bare `N22` notes for the four rows — unreconciled, as required. No commit or push occurred; HEAD is unchanged and N22 files are unstaged. | PASS |

## Deliverables check

| # | Deliverable | Status |
|---|---|---|
| 1 | Source-scoped N22 scanner APIs over the N16 enumeration path, canonical `source_id`, 1-based lines, bounded UTF-8 snippets | PASS |
| 2 | Honest callee-body heuristic with bounded ≤10-line brace lookahead and depth-balanced window | PASS |
| 3 | Deterministic bounded flow (depth 1..8 default 2; limit 1..200 default 50; page_limit 1..2000 default 500) | PASS |
| 4 | Deterministic bounded blast subset with category priority and line de-duplication (limit default 80) | PASS |
| 5 | Shared strict operation contract: alias grammar, full-string numeric parsing, source authorization | PASS |
| 6 | Registrations plus MCP typed-argument maps and ambient-source exclusions; exact row shape | PASS |
| 7 | Guarded stateless cache-compatibility operation corrected to `local_only=true` with structured JSON | PASS |
| 8 | `tests/test_n22.cpp` + build wiring; `scripts/n22-verify.ps1`; evidence manifest and `VERIFY-REPORT.md`; two frozen full-suite runs | PASS |

## Findings

### P0 (blocks done)

None.

### P1

None.

### P2 (non-blocking; recorded for accuracy)

**P2-01 — Run 1 has no dedicated `FULL-SUITE-RUN-1.txt`.** The first complete suite run is embedded in `TEST-BUILD-OUTPUT.txt` under `stage=test-build-and-suite-run-1`. Both runs exist, are all-PASS, exit 0, and share one test-binary hash, so the plan's two-run requirement is met; only file naming is asymmetric and mildly harder to discover.

**P2-02 — `production_data_access_telemetry = "not-collected"`.** The manifest honestly declares non-collection rather than asserting a measured negative. The substantive protection is evidenced instead by `isolated_localappdata = true`, disposable temp databases, `isolated_localappdata_tree_before/after_cleanup = absent/absent`, and the affirmative statement in `VERIFY-REPORT.md` that the verifier did not traverse production `LOCALAPPDATA`. Non-disclosure of a metric was preferred over fabricating one; no production-data mutation is claimed or implied.

**P2-03 — Push cannot be proven locally.** `VERIFY-REPORT.md` explicitly declines to claim global push telemetry. Local evidence is nevertheless concrete: git HEAD and the reference-log fingerprint are frozen at preparation and re-asserted after the runs, and I independently confirmed HEAD is unchanged with N22 files unstaged.

**P2-04 — The `call` blast category is structurally unreachable.** Any line matching `looks_like_call` also matches `has_word_ref`, and `ref` outranks `call` in the plan's own priority order, so caller lines surface as `kind:"ref"`. The test suite asserts this explicitly. This is the plan's specified first-category-wins rule applied consistently rather than a deviation, but the ledger phrase "def/ref/caller/callee" may lead a reader to expect an observable `call` kind.

**P2-05 — The audited draft plan bytes are not recoverable.** The draft (`696485…a1c`) was never committed, so the draft→approved delta cannot be reconstructed from git; HEAD still holds the superseded 2026-07-27 plan. Corroboration is strong but indirect: the draft hash is recorded in three independent places (plan metadata, `PREBUILD-MANIFEST.json`, verifier constant), the verifier requires the plan-audit's declared `Plan SHA-256` to equal it, and all six plan-audit line citations I spot-checked (lines 55-57, 61-62, 72-73, 85, 126, 176) resolve to the exact quoted text in the current approved plan.

**P2-06 — `protected_repo_config` is an empty array.** I confirmed this is correct rather than a skipped check: no `.codex`, `.claude`, or `.opencode` directory and no root-level `model-config`/`llm-config` file exists in the repository. The load-bearing guard is `scoped_diff.protected_assignment_change_count == 0`, which the verifier requires after the runs.

**P2-07 — Single `N30` token in the verifier is a deliberate negative self-test.** `n22-verify.ps1:3138` builds the string by concatenation (`'synthetic future token ' + 'N30'`) to exercise its own future-node detector. No N30 file exists, and no N30 token appears in any evidence artifact.

**P2-08 — Legacy unscoped scanners remain as dead code.** `find_callees`, `find_flow`, `find_blast` and the older `scan()` path are retained (explicitly permitted by the plan) and now have no callers. They are unreachable from any registered operation but still compile into the product.

## Residual risks

- **Heuristic by construction.** Brace and token scanning is not comment- or string-aware, so a `{`, `}`, or `name(` inside a comment or string literal can shift a body window or produce a spurious callee. The plan scopes this deliberately, and every operation description plus the ledger intent states the limitation, but consumers must not read N22 output as semantic truth.
- **Definition detection is regex-shaped.** `DefinitionMatcher` recognises a fixed family of C/C++/TS-like declaration forms. Unusual formatting yields silent empty results rather than errors — correct fail-closed behaviour, but absence of hits is not proof of absence of code.
- **Fingerprint boundary is a real, bounded exclusion.** The persistent-data fingerprint deliberately ignores directory `LastWriteTimeUtc` because SQLite WAL read lifecycles mutate it without any surviving data change. The verifier self-tests this boundary in both directions (root and nested directory time changes provably do not move the fingerprint; directory attributes, file time, file attributes, same-length content edits, and inventory add/remove provably do). A hypothetical change visible *only* in a directory mtime would therefore go unrecorded; the logical-database snapshots and file-level hashes cover the data dimension.
- **Disposable-tree before/after pair is `absent`/`absent`.** That specific pair is trivially equal and carries no integrity information; the meaningful fixture evidence is the three identical non-absent digests across fixture, post-probe, and post-doctor states.
- **Evidence is distributed across hash-bound sidecars.** `EVIDENCE-MANIFEST.json` itself does not inline the full scoped-input set, SQLite input hashes, build/suite commands, PASS lines, or the 367 individual snapshot pairs; it binds them via `prebuild_manifest_sha256` and the 13 `output_files` digests. I verified every link in that chain on disk, so the record is complete — but it cannot be validated by reading the manifest alone.
- **Two runs bound the same binary, not the toolchain.** Both runs used one test binary; the evidence proves run-to-run determinism, not reproducibility of the build from source on a different machine.

## Conclusion

Every blocking requirement of the approved N22 plan is supported by current code and by fresh evidence that I re-derived against the live worktree. All four operations are source-scoped through the audited N16 enumeration path with bound SQL predicates applied before limiting; the callee heuristic is bounded and proven not to leak past a body close or into a later definition; flow is deterministic, cycle-safe and bounded; blast is priority-ordered and line-deduplicated; the shared contract enforces strict symbol, alias, numeric and source-authorization rules; all four operations are covered by the MCP typed-argument gate and excluded from ambient-source injection; and the cache-compatibility operation is now correctly `local_only=true` with an honest zero/stateless response. Read-only behaviour is demonstrated by full logical-snapshot equality across 367 recorded calls in two brains, schema stayed v12 under runtime markers and a real-binary doctor check, and two complete suite runs are all-PASS at 29 registered tests with the dedicated N22 test present exactly once — above the N19 baseline of 26.

The eight P2 items are accuracy and disclosure observations, not capability gaps. Notably, the evidence set is candid about what it does not measure (production-access telemetry, global push state) instead of overclaiming, which I weigh in its favour rather than against it.

The verdict recorded at the top of this document is PASS, with no open P0 or P1. N22 may now be marked `Status: done`, and exactly the four historical ledger rows — `code_callees`, `code_flow`, `code_blast`, `code_traversal_cache_clear` — may have their notes reconciled to the bounded, heuristic, non-AST language the plan prescribes. No other ledger row, node, or artifact is in scope. Commit and push remain outside N22 and require a separate human request.

This audit changed no code, test, verifier, plan, plan-audit, ledger, completion document, evidence artifact, configuration, or git state; it wrote only this file. No N30 artifact was created, read, or relied upon. No LLM, provider, API key, base URL, model, reasoning-effort, context-size, or compression-threshold setting was inspected for modification or altered.

# N20 Plan - Schema Pack and Pack-Ontology Correctness Closure

**Status**: done
**Depends on**: Directly on N1, N2, N2.5, N7, N8, N11, N15, N18, and N19 only. N1 supplies MCP write-default-deny; N2 supplies active-page/soft-delete semantics; N2.5 supplies canonical source ids and remote source authorization; N7 supplies authenticated loopback MCP; N8 supplies Windows data-root and selected-brain isolation; N11 supplies the native Windows quality gate; N15 supplies the live schema-v12 baseline; N18 supplies strict read/source/error contracts; and N19 supplies the current typed-MCP, ambient-source-exclusion, selected/decoy, and full-snapshot verification patterns. N21 and later nodes are not dependencies.
**Plan audit**: PASS (`N20-PLAN-AUDIT.md`; fresh Claude Code re-audit of revised draft SHA-256 `d6600297081e983876894d9da893f2ff5769c518e74fb72483687eaae1f02787`)
**Outcome audit**: PASS (fresh Claude Code outcome audit 2026-08-15, dispatched in N30 outcome phase; see N20-HARD-AUDIT.md)
**Wave**: Wave 5; N20 may run alongside another independently approved node only when shared-file ownership is serialized
**Process note**: The short 2026-07-27 N20 plan, the existing implementation, the combined `tests/test_n20_23.cpp`, the historical `N20-HARD-AUDIT.md`, and current ledger claims are context only. Before this refreshed loop, N20 had no node-specific plan-audit artifact. The first refreshed audit, bound to the draft hash above, returned FAIL because the retrospective baseline and prospective gate boundary were not explicit enough. That FAIL does not authorize implementation; only a fresh audit of this revised draft can replace it. No new N20 production, test, verifier, evidence, ledger, or status change under this plan may begin while the plan is `draft`.

## Retrospective baseline and prospective work boundary

This is a retrospective correctness closure, not a claim that the pre-existing N20 code already satisfies the new contract. Plan audit evaluates whether the proposed corrective work and evidence are sufficient and feasible. It does not require the post-approval deliverables to exist before approval; creating `tests/test_n20.cpp`, `scripts/n20-verify.ps1`, or security fixes while the plan is `draft` would itself violate the ordered node loop.

For this refreshed node, "implementation starts" means the first new scoped corrective edit made under this plan after the revised plan receives a Claude Code PASS, the parent performs the metadata-only transition to `approved`, and the pre-implementation gate succeeds. Source, tests, and historical artifacts that already existed before this draft are frozen baseline inputs, not implementation or evidence produced under this plan. The gate is prospective: it proves the temporal boundary between that hashed baseline and every new N20 corrective edit. It does not claim that the historical baseline was created after the gate or retroactively approve it.

The following observed baseline defects are the reason for the planned work, not approval prerequisites or accepted behavior:

| current baseline surface | observed gap | required post-approval correction |
|--------------------------|--------------|-----------------------------------|
| `src/qbrain/schema/packs.cpp` / `packs.hpp` | N20 reads call create-on-read `ensure_default_pack`; listing carries a local path; ids, enumeration, file size, reparse status, JSON, and manifest bounds are insufficient | Implement the embedded read-only default, safe direct-child resolver, strict bounded loader/discovery, validated in-memory manifest, and no-path public metadata in Deliverable 1 |
| `src/qbrain/schema/packs.cpp` active selection | `set_active_pack` calls generic configuration persistence and does not provide the declared DB-only validate-before-write/no-op behavior | Replace the N20 selection path with an atomic selected-brain SQLite `config['schema.active_pack']` update that cannot rewrite global `config.json` |
| `src/qbrain/ops/handlers.cpp` N20 registrations | `schema_stats` is not source-bound/authorized/bounded and historical pack/ontology handlers accept weak data and fallback behavior | Replace exactly the six handlers with the normative contracts and exact response/error shapes in Deliverable 2 |
| `src/qbrain/mcp/server.cpp` | The six operations lack the complete typed maps and ambient-source exclusions declared here; reload locality does not establish the required remote default-deny behavior | Add the exact six typed maps, exclusions, and reload gate metadata in Deliverable 3 |
| `tests/test_n20_23.cpp` and current registrations | Historical combined checks are shallow and no dedicated `test_n20` exists | Retain the combined regression and add/register the new dedicated matrix only after approval, as Deliverable 4 |
| verifier/evidence | No current `scripts/n20-verify.ps1` or plan-bound N20 evidence exists | Create the verifier and frozen artifacts only after approval and successful gate, as Deliverable 5 |

The existing mismatches above must not be weakened out of the plan merely to match historical code. They must be corrected and proven before the outcome audit. Conversely, they must not be treated as missing preconditions that force implementation before plan approval.

## Pre-implementation gates

After a fresh Claude Code plan audit accepts this revised draft and the parent changes only the `Status` and `Plan audit` metadata to `approved`, but before any new N20 production, test, verifier, or ordinary runtime-evidence edit under this plan, run these gates. The metadata transition and `PRE-IMPLEMENTATION-GATE.json` itself are the only N20 writes allowed before the gate completes; the gate artifact is written last after all checks succeed. Historical baseline files remain unchanged during this interval.

1. Verify that the plan audit names the exact revised-draft SHA-256. Compare the approved plan to that audited draft and require that only the two approval metadata fields changed. Record both the audited-draft hash and approved-plan hash. Snapshot each scoped baseline file's path, SHA-256, bytes, last-write UTC, and Git state/diff fingerprint. If any scoped baseline changes after the audit and before the gate snapshot, return the plan to `draft` and re-audit the new inputs.
2. Verify that every dependency audit listed in the dependency table at the end of this plan still exists, still has its recorded SHA-256, identifies Claude Code as auditor, and has a PASS verdict. A missing, changed, or non-PASS dependency returns this plan to `draft` for dependency review.
3. Run the already-built `build\cl\qbrain.exe doctor --json` against a unique selected brain under a unique temporary `LOCALAPPDATA`. Require exit code 0, valid JSON, `ok == true`, and integer `schema_version == 12` from the actual `Brain::health -> storage::check_schema_integrity` path.
4. Capture `docs/nodes/n20-evidence/PRE-IMPLEMENTATION-GATE.json` with UTC start/end, exact command and exit code, audited-draft/approved-plan/plan-audit SHA-256, binary SHA-256, the sorted baseline manifest and Git/diff fingerprints from step 1, the parsed doctor result, the temporary root and brain id, proof that the production `%LOCALAPPDATA%\Qbrain` root was not used, and proof that the temporary root was removed. The verifier must later prove that all N20 corrective changes are differences from this frozen baseline, not pre-gate evidence claims.
5. If the gate fails, reports a schema version other than 12, shows concurrent scoped changes, or shows that N20 needs a table/index/constraint migration, stop. Return this plan to `draft`, add a populated-database migration/idempotence/failure/rollback matrix, and obtain another Claude Code plan-audit PASS before editing schema or migration code. Do not silently migrate or continue.

The gate is evidence of ordering and current schema only. It is not an outcome audit and cannot mark N20 done.

## Goal

Re-verify and, where necessary, correct exactly six existing operations as a bounded Qbrain schema-pack subset:

1. `list_schema_packs`: deterministically list bounded, validated pack identities without exposing local paths.
2. `get_active_schema_pack`: return the selected brain's active, validated pack manifest.
3. `reload_schema_pack`: validate a named/current pack from disk and atomically select it for only the selected brain, behind MCP write-default-deny.
4. `schema_stats`: return deterministic active-page counts by type for exactly one authorized canonical source in the selected brain.
5. `ontology_get`: return a validated schema-pack manifest as Qbrain's pack-ontology subset.
6. `ontology_dimensions`: return the bounded dimension declarations from that validated pack.

N20 does not implement an upstream schema compiler, extends chain, cache graph, pack installer/uploader, per-call pack override, entity-resolved or bi-temporal ontology, ontology observations/confidence/provenance, semantic inference, database schema mutation, cross-brain aggregation, provider/LLM behavior, or full gbrain schema/ontology parity. In particular, Qbrain's `reload_schema_pack` is an honest validate-and-select/re-read subset because the current C++ implementation has no pack cache to invalidate, and Qbrain's two ontology reads expose pack declarations rather than upstream per-entity ontology facts.

## Ledger rows to reconcile after outcome PASS

The current six `implemented` entries are historical claims until this refreshed node completes its own gates. "Reconcile" does not assume a new implemented-status transition: after and only after a fresh N20 outcome-audit PASS, retain `implemented` only if proven, replace each terse `N20` note with the honest subset/evidence wording below, and replace the historical combined N20-N23 summary with a node-specific refreshed-closure note. Do not change the implemented operation count or any other row.

| op | scope and locality | exact N20 subset |
|----|--------------------|------------------|
| `list_schema_packs` | Read, remote-capable | Bounded deterministic identities from the process data-root pack library plus the built-in default; no path disclosure |
| `get_active_schema_pack` | Read, remote-capable | Validated manifest selected by the current brain's `schema.active_pack` DB key, defaulting only when the key is absent/empty |
| `reload_schema_pack` | Write, remote only with explicit `--allow-write` | Re-read/validate the current or named installed pack and select it atomically for only the current brain; not an upstream cache-invalidation parity claim |
| `schema_stats` | Read, remote-capable subject to source authorization | Active selected-source page counts grouped by stored page type with a bounded deterministic result |
| `ontology_get` | Read, remote-capable | Validated pack manifest as a Qbrain pack-ontology declaration subset; not entity ontology |
| `ontology_dimensions` | Read, remote-capable | Validated bounded dimension strings declared by a pack; not observed entity/dimension statistics |

No other operation, behavior, or ledger row belongs to N20. Existing later-node code may remain in the integrated regression suite, but no N21+ plan, audit, implementation, test, ledger row, or evidence artifact can satisfy an N20 acceptance assertion.

## Normative shared contract

### Data-root, pack identity, and filesystem confinement

1. The pack library is process-data-root global at `%LOCALAPPDATA%\Qbrain\schema-packs\`, matching the historical Qbrain layout. Active selection is not global: canonical key `schema.active_pack` lives only in the selected brain's SQLite `config` table. Two brains may select different ids from the same library, and changing one brain must not change the other.
2. The default manifest is compiled into the binary. When no `default.json` exists, reads return the built-in default without creating the pack directory or any file. A valid installed `default.json` may override the built-in manifest; its origin is then `installed`. The six handlers must not call the current create-on-read `ensure_default_pack` path.
3. A pack id is valid only when it is 1..64 ASCII bytes, matches `^[A-Za-z0-9_-]+$`, is not a case-insensitive Win32 reserved device name (`CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9`), and canonicalizes to lowercase. Empty explicitly supplied ids, dots, separators, drive colons, alternate-data-stream syntax, absolute/UNC/volume paths, trailing dot/space forms, Unicode lookalikes, and overlength ids fail before filesystem access.
4. A named pack resolves only to the direct child `<canonical-id>.json` of the canonical pack root. No recursion, glob, environment expansion, current-working-directory lookup, or caller-supplied path is allowed. Before opening, require direct-parent equality/confinement and a regular non-symlink, non-reparse-point file. A missing or unsafe candidate fails without probing or reporting an outside path.
5. Only byte-exact lowercase `.json` direct-child names with valid canonical stems are candidates. Ignore unrelated non-JSON files. Inspect at most 4096 directory entries and return a structured `pack_limit_exceeded` error on the next entry. Return at most 256 distinct canonical pack ids including `default`; a 257th candidate or a case-colliding duplicate is an error, never a nondeterministic truncation.
6. A pack file is at most 1,048,576 bytes. Check its regular-file status and size before a bounded read, reject boundary-plus-one, and catch open/read/stat/directory exceptions at the module/handler boundary. Errors never contain an absolute path, username, environment value, or raw attacker-controlled id.
7. Read operations do not create directories/files, rewrite `default.json`, make backups, update timestamps intentionally, repair configuration, or persist a normalized id. Filesystem evidence compares names, bytes, sizes, and last-write timestamps before/after; access time is not an acceptance signal.

### Minimal manifest contract

1. Every loaded manifest is strict JSON with one object root and no trailing data. The supported top-level keys are `id`, `name`, optional `version`, `types`, `dimensions`, and optional `phases`; an unknown key or wrong type is `pack_invalid` rather than a raw fallback.
2. `id` is required, obeys the pack-id rule, and canonicalizes exactly to the requested filename/id. `name` is required valid UTF-8 of 1..256 bytes. Optional `version` is valid UTF-8 of 1..64 bytes.
3. `types` is a required array of 1..256 unique canonical identifiers. `dimensions` is a required array of 0..256 unique canonical identifiers. Each identifier is 1..64 ASCII bytes under the same positive identifier character/device-name rule. Optional `phases` is an array of 0..64 unique valid UTF-8 strings, each 1..64 bytes.
4. Duplicate array members after canonicalization, malformed UTF-8, overlength values, excessive members, scalar/object members in an identifier array, missing required keys, and an `id`/filename mismatch are structured `pack_invalid` failures. Invalid content is never returned as `raw`, converted to `{}`/`[]`, or silently treated as the built-in default.
5. Successful serialization is from the validated in-memory value, not raw file bytes. Repeated reads against unchanged DB/files serialize byte-identically and contain no comments, path metadata, or unknown fields.

### Request, MCP, errors, and side effects

1. `list_schema_packs` and `get_active_schema_pack` accept no fields. `reload_schema_pack`, `ontology_get`, and `ontology_dimensions` accept only optional string `id`. `schema_stats` accepts only optional string `source_id` and optional unsigned-integer `limit`.
2. Every registry JSON schema is `type=object` with `additionalProperties=false`, exact properties, and exact defaults/bounds. MCP pre-dispatch typed maps cover all six operations, including empty maps for the no-argument operations. Non-object arguments, unknown fields, null supplied values, booleans/arrays/objects/numbers in string positions, and signed/floating/string values in the MCP integer position fail before dispatch.
3. Local registry handlers independently reject unknown fields and validate string-form values. `schema_stats.limit` defaults to 100; a supplied value must be a complete unsigned base-10 ASCII decimal with no sign, whitespace, decimal, suffix, empty string, or overflow. Syntactically valid 0 clamps to 1 and values above 256 clamp to 256.
4. Exclude all six N20 operations from MCP ambient `QBRAIN_SOURCE` injection. Omitted `schema_stats.source_id` means canonical `default`; an explicitly empty source is invalid. Pack-only operations never consume a source id.
5. Every failure has `ok=false`, nonzero `exit_code`, and valid bounded JSON shaped as `{"error":{"code":"...","field":"...","message":"..."}}`. Codes distinguish invalid arguments/ids, missing or invalid/oversized packs, candidate limits, source errors, filesystem errors, and database errors. Messages do not echo raw input, manifest content, paths, config values, tokens, secrets, provider/model settings, or data from another source/brain.
6. Successful `text` is the same JSON value and ordering as `json`. All success responses are bounded, valid UTF-8 JSON. Filesystem/database exceptions are caught; no operation terminates the server process.
7. `list_schema_packs`, `get_active_schema_pack`, `schema_stats`, `ontology_get`, and `ontology_dimensions` remain `Scope::Read`, `local_only=false`, work while MCP writes are disabled, and are logically DB/filesystem read-only. `allow_write=true` does not broaden `schema_stats` source authorization.
8. `reload_schema_pack` remains `Scope::Write` and uses `local_only=true` in the current registry semantics so remote MCP without `--allow-write` is denied before pack access or mutation. An explicitly allowed remote call may select only a preinstalled valid pack for the already selected server brain; N20 adds no remote upload/install path.

## Normative operation contracts

### `list_schema_packs`

1. Validate the selected brain's active id without repairing it, discover/validate bounded installed candidates, merge the built-in default only when not overridden, and sort by canonical id bytewise ascending before serialization.
2. Return exactly `{"active_id":string,"packs":[...]}`. Each pack row is exactly `{"id":string,"origin":"builtin|installed","active":boolean}`. Exactly one row is active. No row contains `path`, a filename, file size/time, parse error text, or another data-root detail.
3. A missing pack directory is successful and lists the built-in default. An invalid configured active id, active id whose installed file is missing (except built-in default), malformed candidate, duplicate/collision, oversized candidate, or enumeration bound failure is a structured error; no silent repair/fallback is allowed.

### `get_active_schema_pack`

1. Accept no fields. Read the selected brain's active id (`default` only when its DB key is absent/empty), load and validate that exact pack, and do not alter DB/filesystem state.
2. Return exactly `{"id":string,"origin":"builtin|installed","pack":object}` where `pack` has only the validated minimal-manifest keys. Never return `raw`, `{}`, a partial manifest, path, config dump, or another brain's active selection.

### `reload_schema_pack`

1. Accept optional `id`; omission re-reads and validates the current active pack. An explicitly empty or invalid id is rejected. Canonical mixed-case input selects the canonical lowercase id.
2. Resolve, size-check, read, parse, and fully validate the candidate before any SQLite change. A missing, unsafe, malformed, or oversized pack leaves every DB/file/config snapshot unchanged.
3. After validation, atomically upsert only selected-brain SQLite row `config['schema.active_pack']`. Do not call a path that rewrites global `%LOCALAPPDATA%\Qbrain\config.json`, and do not read/modify model, provider, base URL, API key, reasoning, context, or compression settings. If the selected id is already active, perform no write and return `changed=false`.
4. Return exactly `{"id":canonical-id,"changed":boolean}`. The operation does not create/copy/backup a pack file and does not claim cache-cascade semantics.

### `schema_stats`

1. Resolve and authorize exactly one existing canonical source through the N2.5/N18/N19 read contract before statistics SQL. A local caller may read any registered source. A remote non-default source must be case-insensitively allowlisted in selected-brain `mcp.allowed_sources`; `allow_write=true` cannot bypass this. Reads never call `ensure_source`.
2. Count only selected-brain pages with bound `source_id` and `deleted_at IS NULL`. Group by the stored byte-exact `type`, order by `count DESC, type COLLATE BINARY ASC`, and apply `limit + 1` after source/active predicates and grouping to compute deterministic truncation. Other-source/deleted/decoy rows cannot consume a limit or change counts/order.
3. Require each returned stored type to be valid UTF-8 and at most 256 bytes. A damaged row outside that response contract produces a structured `database_error`, not invalid JSON or lossy cross-row merging.
4. Return exactly `{"source_id":string,"active_pack_id":string,"schema_version":integer,"total_active_pages":integer,"type_counts":[{"type":string,"count":integer}],"truncated":boolean}`. `schema_version` is read from live schema integrity, not hard-coded; implementation/evidence is gated on v12.

### `ontology_get`

1. Accept optional `id`; omission selects the current active id, while a supplied id selects a validated named pack without changing active configuration.
2. Return the same exact `{"id", "origin", "pack"}` shape and validated manifest semantics as `get_active_schema_pack`. This is a pack declaration lookup only, not an entity, date, confidence, provenance, or source-scoped ontology query.

### `ontology_dimensions`

1. Accept optional `id` under the same resolution/validation rules as `ontology_get` and never change active configuration.
2. Return exactly `{"id":string,"dimensions":[string...]}` in manifest order. The array is already bounded/unique by manifest validation. Invalid JSON or a wrong/missing `dimensions` field is an error, not silent `[]`; a valid explicitly empty dimensions array is successful.

## Deliverables

1. `include/qbrain/schema/packs.hpp` and `src/qbrain/schema/packs.cpp`:
   - one canonical pack-id validator and safe direct-child resolver;
   - an embedded default manifest and read-only discovery path that does not create the root;
   - bounded file/candidate enumeration, non-symlink/reparse confinement, strict JSON parsing, minimal manifest validation, deterministic metadata, and structured internal error information without path leakage;
   - selected-brain active-id read and a DB-only atomic selector that never rewrites global `config.json`;
   - a source-scoped schema-stat helper with bound SQL and deterministic ordering;
   - compatibility for existing non-N20 callers sufficient to compile and pass regression, without expanding N20 ledger scope.
2. `src/qbrain/ops/handlers.cpp`: replace exactly the six historical N20 handlers with strict allowed-field checks, pack/source/limit validation, declared response shapes, structured exception handling, honest subset descriptions, Read/Write metadata, and `reload_schema_pack` write gating.
3. `src/qbrain/mcp/server.cpp`: add exact typed-argument maps for the six operations, including no-argument empty maps; reject mismatches pre-dispatch; exclude all six from ambient-source injection. Do not change unrelated coercion behavior as an incidental N20 edit.
4. Add dedicated `tests/test_n20.cpp` and register it in `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1`. Keep `tests/test_n20_23.cpp` as integrated regression coverage, but do not use its historical non-empty checks as the dedicated N20 gate or weaken any existing test.
5. Add `scripts/n20-verify.ps1` and `docs/nodes/n20-evidence/VERIFY-REPORT.md` plus frozen build/runtime/MCP/snapshot/manifest artifacts. The verifier must ingest and fail closed on the approved-plan-bound `PRE-IMPLEMENTATION-GATE.json`; evidence records facts and never writes an audit verdict.
6. No schema migration, schema SQL, migration code, model configuration, production data, or pack installation API is a planned deliverable. If any becomes necessary, stop and re-plan before editing it.
7. After implementation and evidence, obtain a fresh node-specific Claude Code outcome audit against the approved N20 plan. Only its PASS permits the parent to mark this plan `done` and reconcile exactly the six N20 ledger rows.

## Tests and evidence

All commands run on native Windows 11 PowerShell with x64 MSVC and C++20. WSL and Docker are not part of the build, test, verifier, or runtime path.

1. Build and full regression:
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-cl.ps1`.
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1` and the produced `build\cl\qbrain_tests.exe` as required by the script contract; run the full test binary a second time against a fresh verifier root.
   - Record exact commands/exit codes, Windows edition/architecture, full `cl.exe` version, x64 target, `/std:c++20`, production/test binary SHA-256, exact registered count, and every result. The latest completed N19 evidence establishes the current 26-test baseline, including the retained combined `n20_23` regression. The new dedicated registration is additive, so the suite must be all PASS with at least 27 registered tests unless unrelated approved work raises the baseline further; the missing dedicated test at draft time is a planned deliverable, not a pre-approval claim.
2. Schema and gate matrix:
   - Verify the pre-implementation gate and its temporal/hash bindings before edits.
   - Open fresh and populated schema-v12 selected/decoy databases, run the six operations, close/reopen, and prove no schema SQL/index/version/data migration occurs.
   - Damage a required config/pages/schema table only in a disposable fixture and require a structured bounded database error, no process termination, no repair, and no path/config/secret leak.
3. Pack-root and built-in-default matrix:
   - Start with a nonexistent isolated pack root. `list_schema_packs`, `get_active_schema_pack`, `ontology_get`, and `ontology_dimensions` must return the built-in default while the root remains nonexistent.
   - Preseed valid custom packs and an optional valid `default.json`; prove origin, active flags, deterministic bytewise listing, exact response keys, byte-identical repeats, and no path/filename leakage locally or remotely.
   - Hash the full isolated filesystem tree before/after every read and prove names, bytes, sizes, and last-write timestamps are unchanged.
4. Id/path/filesystem-bound matrix:
   - Accept exact boundaries and mixed-case canonicalization. Reject explicit empty, `.`, `..`, separators, drive/ADS colon, absolute/UNC/volume forms, Win32 device names, trailing dot/space, Unicode, and 65-byte ids before any candidate open.
   - Reject symlink/reparse candidates and any resolver result outside the direct canonical root. Assert outside sentinel files are never opened, returned, changed, or named in errors.
   - Cover file size 1,048,576 and 1,048,577, 256/257 distinct candidates, 4096/4097 inspected entries, case collisions, invalid filename stems, directory entries posing as files, missing/unreadable files, and deterministic error envelopes with no mutation.
5. Manifest-validation matrix:
   - Cover the built-in manifest and a valid installed manifest, optional fields, empty valid dimensions, maximum array/value boundaries, and stable canonical serialization.
   - Reject malformed/trailing JSON, non-object root, unknown/wrong/missing keys, id/filename mismatch, invalid/malformed UTF-8, duplicate identifiers after canonicalization, oversized arrays/strings, and non-string members. `get_active_schema_pack`, `ontology_get`, `ontology_dimensions`, and `reload_schema_pack` must agree on acceptance/rejection.
   - Prove invalid content is never returned as raw data, `{}`, or silent `[]` and never changes active selection.
6. Selected/decoy brain and reload matrix:
   - Create two physical brains sharing the isolated pack library but with different active ids. Prove every active/ontology response follows the invoked brain and no decoy active id/config sentinel appears.
   - Successful reload changes exactly selected-brain `schema.active_pack`; decoy DB, every other selected-brain table/config key, pack files, and global `config.json` are unchanged. Repeating the same id returns `changed=false` with an unchanged logical snapshot.
   - Remote reload without allow-write is denied before filesystem/DB access; explicit allow-write can select a preinstalled valid pack; missing/invalid/oversized/unsafe packs and all malformed requests leave selected/decoy DB and filesystem snapshots unchanged.
7. `schema_stats` source matrix:
   - Seed selected and decoy brains with overlapping `default`/`team_a` sources, active/deleted pages, tied type counts, more than 256 types, and stronger out-of-scope counts.
   - For selected/default, selected/team, decoy/default, and decoy/team independently, compare `schema_version`, total active pages, every returned type/count, ordering, and truncation to direct bound SQL and `storage::check_schema_integrity`. Whole-object inequality is not a substitute for exact per-cell assertions.
   - Exercise omitted/default, mixed-case registered source, empty/malformed/reserved/overlength/unknown source, local non-default, remote default, remote denied, remote allowlisted, and remote `allow_write=true` denied cases. Source counts never change and no other source/brain can consume a group limit.
   - Exercise omitted, 0, 1, 100, 256, over-max, empty, sign, whitespace, decimal, suffix, and overflow limits. Invalid values fail before statistics SQL; valid 0/over-max values clamp exactly as declared.
8. Registry and real MCP matrix:
   - Inspect exactly six registrations and `tools/list` schemas: five Read/non-local-only operations, one Write/write-gated operation, `additionalProperties=false`, exact fields/types/defaults/bounds, and bounded honest descriptions.
   - Exercise real stdio or loopback-token `tools/call` with writes disabled for each read success/error and reload denial; then exercise only the explicit allow-write reload path. Cover non-object arguments, unknown fields, wrong JSON types, null, signed/floating limit, ambient `QBRAIN_SOURCE`, and structured MCP error signaling.
   - JSON and text must parse and match; no response contains a drive, UNC/volume path, username, production-root fragment, raw manifest, protected setting, source/brain sentinel outside the authorized result, or secret.
9. Side-effect and snapshot matrix:
   - Before/after every authorized read, empty read, malformed request, missing/corrupt pack, invalid/unknown/denied source, clamped stats call, and denied reload, hash a deterministic logical snapshot of schema and every row/column in all selected and decoy SQLite user tables plus the isolated filesystem tree. Every pair must match.
   - For the one successful changing reload, require an exact logical diff containing only selected-brain `config['schema.active_pack']`; no file, decoy row, other config row, model/provider/API-key/base-URL field, job, source, page, or schema value changes.
10. Evidence manifest:
    - Record the approved plan/audit/gate hashes; dependency audit hashes; relevant production/test/verifier/build/schema/third-party input hashes; binary hashes; exact commands and exit codes; suite count; pack/path/manifest/source/auth/reload/MCP markers; and every selected/decoy DB/filesystem before/after pair.
    - Record explicit facts that production `%LOCALAPPDATA%\Qbrain` was not touched, no live network/provider call occurred, no secret was persisted, no LLM/agent/application model/provider/base URL/API key/reasoning/context/compression setting changed, no commit/push occurred, no N30 artifact was created/read/used, and no N21+ artifact supplied N20-specific acceptance evidence.

## Acceptance assertions (falsifiable)

1. New N20 corrective work under this refreshed plan starts only after a fresh Claude Code plan-audit PASS on the exact revised draft, a metadata-only transition to `approved`, verified dependency hashes, and a successful audited-draft/approved-plan-bound pre-implementation gate reporting live schema v12 on an isolated brain. Historical implementation remains a frozen, noncompliant baseline; the gate does not retroactively approve or date it. Any premature or concurrent scoped edit returns the plan to `draft` without further implementation.
2. Fresh and populated v12 databases support all six operations and reopen unchanged with no schema/migration edit. Any discovered migration requirement blocks work until a revised plan and new plan-audit PASS.
3. With no pack directory/file, read operations return the bounded built-in default and create nothing. All five Read operations preserve complete selected/decoy DB and filesystem logical snapshots across success and failure.
4. Pack ids obey the exact Windows-safe 1..64 canonical rule, named resolution is confined to a direct non-symlink/non-reparse `<id>.json` child, and every traversal/device/ADS/absolute/UNC/Unicode/overlength case fails before outside access or disclosure.
5. Directory/file/manifest bounds are enforced exactly: at most 4096 inspected entries, 256 returned ids, 1,048,576 file bytes, declared array/string maxima, strict JSON/UTF-8/types/id matching, and structured no-mutation errors at each boundary-plus-one.
6. `list_schema_packs` is byte-stable in canonical id order, identifies one selected-brain active pack and each origin, and exposes no path/filename/stat metadata. Invalid active/candidate state fails instead of silently repairing or falling back.
7. `get_active_schema_pack`, `ontology_get`, and `ontology_dimensions` return only their exact validated shapes; omitted/named selection semantics are deterministic; invalid pack data is never emitted as raw/empty fallback; ledger descriptions explicitly disclaim per-entity/full upstream ontology parity.
8. `reload_schema_pack` is denied remotely without explicit allow-write before side effects, validates before mutation, changes only selected-brain `schema.active_pack` through a DB-only atomic write, leaves global `config.json`/pack files/decoy brain/protected settings unchanged, and is no-op/`changed=false` when already active.
9. `schema_stats` resolves/authorizes an existing source without creation, counts only active selected-source pages, orders `count DESC, type BINARY ASC`, limits after scope/grouping, and matches direct SQL/live-integrity results in all four selected/decoy-by-default/team cells.
10. Local registered-source stats need no allowlist; remote non-default stats require case-insensitive N2.5 authorization; `allow_write=true` cannot bypass it; omitted source is `default` despite ambient `QBRAIN_SOURCE`; invalid/unknown/denied cases run no stats query and leak no data.
11. Local limit parsing consumes the complete unsigned decimal, MCP requires an unsigned integer type, defaults only on omission, clamps valid 0/over-max to 1/256, and rejects empty/sign/whitespace/decimal/suffix/overflow before enumeration without mutation.
12. Registry and real MCP evidence show exact additional-properties-closed schemas, strict no-arg/id/source/limit typing, five Read operations usable with writes disabled, one Write operation correctly gated, structured nonzero errors, and byte-equivalent JSON/text.
13. Two physical brains with overlapping sources and stronger decoy data/config prove selected-brain isolation for active selection, pack/ontology reads, reload, and statistics; no decoy/other-source/path sentinel appears.
14. Native Windows x64 MSVC C++20 evidence records `/std:c++20`, compiler/OS details, exact production/test commands and exits, all registered tests passing at or above 27 including dedicated `n20`, real MCP markers, gate/manifest hashes, and complete DB/filesystem snapshot evidence.
15. Planning, implementation, verification, and auditing touch no production Qbrain root, make no live provider/network call, persist no secret, and change no protected model/provider/base-URL/API-key/reasoning/context/compression configuration. No commit or push occurs unless separately requested by the human user.
16. Only a fresh complete Claude Code outcome-audit PASS permits status `done` and reconciliation of exactly the six N20 ledger rows. N21+, N30, historical N20 artifacts, combined `n20_23` checks, or another node's audit/evidence cannot replace either N20 gate.

## Rollback

- Keep or make the six N20 operations unavailable if path confinement, manifest validation, selected-brain isolation, source authorization, deterministic output, or write gating cannot be maintained. Do not restore path-leaking/create-on-read/raw-JSON fallback behavior.
- Operationally, an absent selected-brain `schema.active_pack` row means built-in `default`. Do not weaken validation, rewrite global config, or silently select a different installed pack to recover from an invalid configured id.
- Revert the N20 pack-module, handler, MCP, dedicated-test, and verifier changes together. Preserve unrelated operation behavior and run the complete regression suite after integration.
- N20 plans no schema downgrade or stored-data migration. If a schema change becomes necessary, stop and re-plan; do not edit migration/schema files under this approval.
- Verification and reload fixtures use only disposable roots/brains. Restore only disposable fixture backups on failure; never modify or delete production `%LOCALAPPDATA%\Qbrain` data.

## Security notes

- Pack ids and files are untrusted local input that remote callers can ask the server to read. Validate id, confinement, entry/file count, reparse status, size, JSON, UTF-8, and manifest shape before returning content or writing active selection.
- Never return pack paths, raw malformed bytes, filesystem exception paths, global config, environment values, usernames, source allowlists, tokens, secrets, page bodies, provider responses, or protected model settings.
- `schema_stats` is source-sensitive. Canonical validation, registered-source existence, and remote allowlist authorization precede SQL; bind source and limits and exclude deleted/other-source/decoy rows before grouping/limiting.
- Read operations must not use `ensure_default_pack`, `ensure_source`, config repair, access logging, or another create-on-read path. `reload_schema_pack` is the sole N20 mutation and must be denied before handler entry unless explicitly write-enabled remotely.
- The selected-brain active-pack row is separate from `%LOCALAPPDATA%\Qbrain\config.json`. N20 must not route the update through current generic persistence that rewrites global file-plane model/provider settings, even if values would appear unchanged.
- Tests use dummy sentinels, unique temporary `LOCALAPPDATA`, temporary brain ids, and no live network/provider request. Evidence includes hashes and bounded fixtures, never secrets or production knowledge.

## Direct dependency evidence

At the pre-implementation gate, verify every row exactly. These are the only direct node dependencies; transitive prerequisites remain owned by those nodes.

| node | plan-audit evidence | outcome-audit evidence | contract consumed by N20 |
|------|---------------------|------------------------|--------------------------|
| N1 | `docs/nodes/N1-PLAN-AUDIT.md`; SHA-256 `9fd6df77ad905463f34e6873c2220849003679a64c869e5fb1eaffba470f95e6` | `docs/nodes/N1-HARD-AUDIT.md`; SHA-256 `93f112c13d01864aa701683e2a4dbb3726a763d90b7a113c07dc543af4d31141` | Read/write scope and MCP write-default-deny |
| N2 | `docs/nodes/N2-PLAN-AUDIT.md`; SHA-256 `c34fede88989a9847dd3cad0bf719b6476c28bbfb124cb094d4afbe24d90fb85` | `docs/nodes/N2-HARD-AUDIT.md`; SHA-256 `e9dc809dcdb73c0757708f81d53daf2fc89394c12cf953e86c0e9de5923a3413` | Active-page and soft-delete semantics for stats |
| N2.5 | `docs/nodes/N2.5-PLAN-AUDIT.md`; SHA-256 `bd0cf1b5f4dddb9af40168a89d1a87be84d5a4eb2f99872d3389880523617953` | `docs/nodes/N2.5-HARD-AUDIT.md`; SHA-256 `dd6e404ab7583af8c6cbecd86179baba3401a1d5ef10f559b2067229a208c8ff` | Windows-safe source identity and remote allowlist |
| N7 | `docs/nodes/N7-PLAN-AUDIT.md`; SHA-256 `929970318d8fb3043371f82a9208360db7e38e6dd058e37f0eef515534f26d39` | `docs/nodes/N7-HARD-AUDIT.md`; SHA-256 `307226705f0dc7495b0aa7aeebf88bd807c0216c19cab059cd23d01dd6835421` | Authenticated loopback MCP and write flag propagation |
| N8 | `docs/nodes/N8-PLAN-AUDIT.md`; SHA-256 `7f16263f786315420ed42a7c79350add553ad84b11ce4cd6dbc21b0fdc320570` | `docs/nodes/N8-HARD-AUDIT.md`; SHA-256 `7970e96af49bbc86f6e71785409a68b482f24e8b2f08a42c2993bbc93c14a8f9` | `%LOCALAPPDATA%` layout, Windows-safe brain ids, selected-brain isolation |
| N11 | `docs/nodes/N11-PLAN-AUDIT.md`; SHA-256 `e157d9f3b6dcbc276b782d960c237d50fed9d4ff5614473678813e27541844a7` | `docs/nodes/N11-HARD-AUDIT.md`; SHA-256 `bdefcf26d138b658d31df0b8525c46b776aa5e9086796bcd16696d8b783f2012` | Native Windows/MSVC build, tests, and doctor evidence |
| N15 | `docs/nodes/N15-PLAN-AUDIT.md`; SHA-256 `01e95a0cc55e4d0580562008a65de2ee941a13a8b37f4fd730389937d5abaef1` | `docs/nodes/N15-HARD-AUDIT.md`; SHA-256 `9f5f14ab7ed2cf4da50b597f8f861061948d9b65331091a017d677f7b4968c59` | Live schema v12 and current source-aware storage baseline |
| N18 | `docs/nodes/N18-PLAN-AUDIT.md`; SHA-256 `87db9821c255555ab6a42aab8d22cac945a5e0141aeeb3dd02e76a07e743af6d` | `docs/nodes/N18-HARD-AUDIT.md`; SHA-256 `f09971ecf44ab66129f33ee3b7dad91515aac39d6d330b725916983fcb408053` | Strict source resolution, local/MCP bounds, structured errors, read snapshots |
| N19 | `docs/nodes/N19-PLAN-AUDIT.md`; SHA-256 `e5c603efbfecb5603a0fd068dd2a0b39e7a75abac5fd116634adc397d9b7e470` | `docs/nodes/N19-HARD-AUDIT.md`; SHA-256 `d4ee4ad14e3768b5470865f092a783ba0d10b9e9155bfb17c4bd5ce594ad4f24` | Typed MCP/no ambient source, current selected/decoy and exact evidence bar |

## Parallelism notes

- Implementation starts only after plan-audit PASS, status `approved`, and the pre-implementation gates above.
- Disjoint post-approval slices may cover (a) pack/filesystem/manifest core, (b) handler/MCP/stats integration, and (c) dedicated tests/verifier evidence.
- `src/qbrain/ops/handlers.cpp`, `src/qbrain/mcp/server.cpp`, `tests/test_main.cpp`, `CMakeLists.txt`, and `scripts/build-tests-cl.ps1` are shared hot files. The parent assigns exclusive ownership or serializes edits, reviews the merged diff, and reruns the complete native suite.
- The parent owns dependency/gate verification, integration, production/test builds, real MCP smoke, evidence manifests, both Claude Code audits, status transitions, and ledger reconciliation. No subagent may author a PASS audit or mark N20 approved/done.
- N20 can coexist with independently approved work only when that work does not supply N20 evidence or weaken this plan. N21+ and N30 remain outside N20 regardless of files already present in the dirty worktree.

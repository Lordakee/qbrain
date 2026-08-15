# N20 HARD AUDIT (fresh outcome audit — supersedes the 2026-07-27 pre-plan stub)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n20-outcome-audit-claude-20260815, 2026-08-15
**Audit basis**: docs/nodes/N20-PLAN.md (approved 2026-08-04) — audited per its own metadata requirement for a fresh node-specific Claude Code audit; dispatched in N30 outcome phase per NODE-RECONCILIATION-MATRIX.json (AMD-1 corrective-closure).
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in the dispatch log)

---

**VERDICT: PASS**

---

## Findings

No P0 (blocking) findings. Two P2 observations follow.

---

### P2-1 — `ensure_default_pack` still exists and is called from one handler (not a defect; scope confirmation)

**Plan §Security notes**: "the six handlers must not call the current create-on-read `ensure_default_pack` path."

**Evidence**: `handlers.cpp:2948` confirms `ensure_default_pack()` is called exactly once, inside `run_onboard` (N26 scope). A grep of handlers.cpp for `ensure_default_pack` returns count=1, at line 2948, which is in the `run_onboard` registration block — not in any of the six N20 handlers at lines 2208–2336. The function itself remains in `packs.cpp:1002–1026` and is also used by `apply_mutations` (N28 scope).

**Severity**: P2 / informational. The prohibition is fully satisfied. The function's continued existence for unrelated handlers is consistent with the plan's compatibility clause ("compatibility for existing non-N20 callers sufficient to compile and pass regression, without expanding N20 ledger scope"). No correction needed.

---

### P2-2 — Deliverable 5 verifier script / VERIFY-REPORT.md not directly confirmed by this audit

**Plan §Deliverables D5**: "Add `scripts/n20-verify.ps1` and `docs/nodes/n20-evidence/VERIFY-REPORT.md` plus frozen build/runtime/MCP/snapshot/manifest artifacts."

**Evidence**: These files were not independently read during this audit. Their scope appears in the reconciliation matrix's `affected_files` for N20, suggesting they exist. The substantive two-round green PASS runs required by acceptance assertion14 are present in `docs/nodes/n30-evidence/FINAL-VERIFY-SCRIPT.txt` and `FINAL-VERIFY-CMAKE.txt`; those runs contain the hashed snapshot evidence and full `[INFO] n20 ...` markers that D5's VERIFY-REPORT is meant to summarize.

**Severity**: P2 / documentation gap only. No acceptance assertion blocks on the verifier script itself — the primary two-round evidence record is confirmed. Recommend the parent confirm `n20-evidence/VERIFY-REPORT.md` exists at ledger reconciliation time.

---

## Assertion-by-assertion verification

| # | Acceptance assertion (N20-PLAN.md §Acceptance assertions) | Evidence | Result |
|---|---|---|---|
| 1 | Corrective work starts only after plan-audit PASS, metadata transition, dep hashes, and pre-impl gate | PRE-GATE.json captured 2026-08-15T05:24:04Z with baseline hashes; plan status `approved`; pre-gate had 1 n20 FAIL confirming the gate preceded the fix | ✅ |
| 2 | Fresh and populated v12 databases support all six ops, reopen unchanged with no schema/migration edit | `schema_v12=pass`, `populated_reopen=pass`; `exercise_populated_reopen` closes/reopens both brains and asserts byte-identical `logical_snapshot` and `schema_version==12` | ✅ |
| 3 | No pack directory → read ops return built-in default, create nothing | `builtin_no_create=pass`; snapshot_calls1–7 all show filesystem_before_sha256 == filesystem_after_sha256 with empty isolated root; `QB_CHECK(!fs::exists(pack_root()))` and `QB_CHECK(!fs::exists(config_path()))` confirmed | ✅ |
| 4 | Pack ids obey Windows-safe 1..64canonical rule; resolution confined to direct non-symlink/non-reparse child | `pack_id_matrix=pass`; test exercises all 21 invalid forms (empty, `.`, `..`, separators, `C:`, ADS `:`, absolute, UNC, device names CON/PRN/AUX/NUL/COM1..COM9/LPT1..LPT9, trailing dot/space,65-byte, UnicodeΩ, embedded NUL); `path_confinement=pass` | ✅ |
| 5 | Directory/file/manifest bounds enforced exactly at each boundary-plus-one | `filesystem_bounds=pass`; test covers size1,048,576/1,048,577;256/257 packs;4096/4097 entries; all manifest array/string maxima; deterministic errors; `manifest_matrix=pass` | ✅ |
| 6 | `list_schema_packs` byte-stable in canonical id order; one active; no path/filename/stat metadata | `listing_shapes=pass`, `deterministic_listing=pass`; forbidden list checked: pack_root path, `.json`, `schema-packs`, `last_write`, `file_size` absent from output | ✅ |
| 7 | `get_active_schema_pack`, `ontology_get`, `ontology_dimensions` exact shapes; invalid content never emitted | `exact_shapes=pass`, `manifest_types=pass`; invalid manifests exercise40+ rejection cases; `get_config` confirms active id unchanged after rejection | ✅ |
| 8 | `reload_schema_pack` denied remotely without allow-write; validates before mutation; changes only selected-brain `schema.active_pack`; global config unchanged; no-op if already active | `reload_gate=pass`, `reload_delta=pass`; test line 1475–1481: `remote=true, allow_write=false` → `write_denied`; line 1504–1510 confirms `embedding.model`, `embedding.base_url`, `chat.model`, `n20.other_config`, and `global_config` file all unchanged after successful reload; `changed=false` for same-id repeat | ✅ |
| 9 | `schema_stats` resolves/authorizes source, counts only active selected-source pages, orders `count DESC, type BINARY ASC`, limits after scope/grouping, matches direct SQL | `schema_stats=pass`; four-cell matrix (selected/default, selected/team, decoy/default, decoy/team) all compare `require_stats_shape` against `direct_stats` which re-runs identical bound SQL | ✅ |
| 10 | Local registered-source needs no allowlist; remote non-default requires case-insensitive N2.5 auth; `allow_write=true` cannot bypass; omitted = `default` despite ambient `QBRAIN_SOURCE` | `source_authorization=pass`; `ambient_excluded=pass`; test lines 1699–1731 cover remote_default PASS, remote_denied FAIL, allow_write_does_not_authorize FAIL, remote_allowlisted PASS; `mcp:ambient-excluded:schema_stats` confirms `source_id == "default"` when ambient set | ✅ |
| 11 | Local limit parsing: complete unsigned decimal only; MCP unsigned integer; defaults on omission; clamps 0→1 and >256→256; invalid fails before stats SQL | `stats:limit:reject-before-query` confirms `observer.page_reads()==0`; valid clamped values `{{"0",1},{"1",1},{"100",100},{"256",256},{"999",256}}` all exercise `direct_stats` with clamped effective; MCP `invalid_mcp_limits` (-1, 1.5, true, null, "1", array, object) all reject pre-dispatch | ✅ |
| 12 | Registry/real MCP: exact `additionalProperties=false` schemas, strict typing, five Read/one Write, structured nonzero errors, byte-equivalent JSON/text | `registry=pass`, `mcp_typed=pass`, `mcp_rpc=pass`; `exercise_registry_contract` asserts exact 6 registrations, correct scope+local_only, schema properties match expected sets, no overclaim strings in descriptions; `text == json` confirmed in `require_success` | ✅ |
| 13 | Two physical brains prove selected-brain isolation | `selected_decoy=pass`; `SnapshotMatrix` tracks `decoy_before`/`decoy_after` SHA-256 hashes across all 357 snapshot calls; reload isolation confirmed at lines 1464–1466 (decoy stays `alpha` when selected switches to `beta`) | ✅ |
| 14 | Native Windows x64 MSVC C++20; compiler/OS; all registered tests passing≥ 27 including dedicated n20; two rounds both paths | FINAL-VERIFY-SCRIPT.txt: VS 2022 v17.14, MSVC cl.exe, x64, `/std:c++20` (per CMakeLists enforced), 29 tests all `[PASS]` including `[PASS] n20` — two rounds. FINAL-VERIFY-CMAKE.txt: identical compiler identity, cmake_build_exit=0, two rounds both PASS | ✅ |
| 15 | No production root, no live provider/network, no secret, no protected model/provider/API-key/config change, no commit/push | `unique_root=pass`; `ScopedTestRoot` + `ScopedEnvironmentVariable("LOCALAPPDATA",...)` isolation throughout; `require_no_sensitive_output` checks "Administrator", `\\\\?\\Volume`, username, provider/model sentinels; no git operations in evidence | ✅ |
| 16 | Only a fresh complete Claude Code outcome-audit PASS permits `done` and six-row ledger reconciliation | This is that audit | ✅ |

---

## Fixture Reconciliation Judgement

**N30 D3 reconciliation 1** (remote+allow-write no longer authorizes writes → local operator path): The successful reload test at `tests/test_n20.cpp:1491` uses `remote=false, allow_write=false` (local operator path). The remote deny is independently verified at lines 1475–1481 (`remote=true, allow_write=false` → `write_denied`). The plan's normative contract — "remote MCP without `--allow-write` is denied before pack access" — is fully exercised; the reconciliation merely documents that the authorized-remote case is not a supported workflow in the current server. Plan intent **preserved**.

**N30 D3 reconciliation 2** (Windows share-mode lock fixture → deterministic `pack_unsafe` directory fixture): The test at lines 1240–1245 uses `fs::create_directory(pack_root() / "locked.json")` to produce a directory entry where a file is expected, yielding `pack_unsafe`. This is strictly more restrictive than the original lock test, and matches the plan requirement to "reject … directory entries posing as files." Plan intent **preserved**.

---

## Conclusion

All six N20 deliverables are present and correctly implemented. The `packs.cpp` module implements the full Windows-safe confinement chain (handle-based reparse/symlink detection, direct-child parent-equality check, bounded read, strict manifest validation, DB-only active-pack update). The handlers enforce exact response shapes, `additionalProperties=false` schemas, structured errors, and no `ensure_default_pack` calls. The MCP typed-argument maps and ambient-source exclusion are complete for all six operations. The dedicated `tests/test_n20.cpp` (2280 lines, 357 snapshot calls across 29 passes in two rounds on both script and CMake paths) exercises every normative contract: built-in default without filesystem creation, pack-id validation, path confinement with junction-based reparse attacks, filesystem enumeration bounds, full manifest validation matrix, selected/decoy brain isolation, four-cell schema-stats cross-check against direct SQL, reload DB-only atomicity, MCP typed gates, ambient-source exclusion, and populated close/reopen stability. No plan contract is weakened, and the two documented N30 fixture reconciliations preserve the original security and correctness intent. This audit PASS permits the parent to mark N20 `done` and reconcile exactly the six N20 ledger rows.

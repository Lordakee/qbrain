# N31 HARD AUDIT (outcome)

**Auditor**: Claude Code (claude-opus-5, effort max), watchdog task qbrain-n31-hard-audit-claude-20260815, 2026-08-15
**Audit basis**: docs/nodes/N31-PLAN.md (approved round-3 PASS)
**Human authorization**: user instruction 2026-08-15 (quoted verbatim in the dispatch log)

---

---

## N31 Outcome Hard Audit

**Auditor**: Claude Code (claude-opus-5, effort max),2026-08-15T14:50UTC
**Audit basis**: docs/nodes/N31-PLAN.md (approved round-3PASS), N31-PLAN-AUDIT.md  
**Evidence read**: PRE-GATE.json, OPS-INVENTORY.json, MAPPING-CLOSURE.md, EQUIVALENCE.json, B-DECOMPOSITION-VERIFY.txt, A-INVENTORY-VERIFY.txt, FINAL-VERIFY-SCRIPT.txt (first 200 lines), FINAL-VERIFY-CMAKE.txt (final 40 lines confirm round 1 result + 2680-line file), B-REGISTRY-BEFORE/AFTER.json, B-SUMMARY-{SCRIPT-ROUND3/4, CMAKE-ROUND2/3}.txt
**Implementation read**: src/qbrain/ops/handlers.cpp (offset 500+), src/qbrain/ops/registry.cpp (full), include/qbrain/ops/registry.hpp (full), tests/test_n31.cpp (full), tests/test_main.cpp (head + grep), CMakeLists.txt (grep), scripts/build-tests-cl.ps1 (grep), scripts/gen-ops-inventory.ps1 (existence confirmed), docs/OPS-PARITY-LEDGER.md (full)

---

### VERDICT: PASS

---

### Acceptance Assertion Results

**AA1 — Four-way count consistency (N=108)**

PRE-GATE.json freezes `source_register_one_count: 108` and `ledger_upstream_implemented_rows: 104`. OPS-INVENTORY.json confirms `registry_ops: 108 = inventory_rows: 108 = frozen_registry_count: 108`, `ledger_upstream: 104`, `ledger_extension: 4`, `extensions_or_diff: 0`. `test_n31.cpp` constants `kN31FrozenRegistryCount=108`, `kN31LedgerUpstreamRows=104`, `kN31LedgerExtensionRows=4`. The `n31a_ledger_rows()` function in the test parses OPS-PARITY-LEDGER.md live and asserts both table sizes and per-op membership exactly match inventory rows. The ledger upstream table has 104 rows confirmed; the extensions table now has 4 rows (capture, list_brains, run_dream, list_job_messages). The runtime assertion `QB_CHECK(static_cast<int>(ops.size()) == kN31FrozenRegistryCount)` enforces the four-way parity at every test run. **PASS**

**AA2 — Mapping completeness (ops_without_tests == 0)**

OPS-INVENTORY.json: `ops_with_tests: 108`, `ops_without_tests: 0`. MAPPING-CLOSURE.md records30 zero-mapping gap ops before D3, 0 after. Every one of the 108 inventory rows has a non-empty `tests` array. The test asserts `!row["tests"].empty()` per row and verifies each test entry carries bare filename+case name with no path separators. D3 gap-op assertions execute actual registry calls on a temp-dir brain, confirming primary paths (e.g., `revert_version` reverts to a prior version, `takes_calibration` returns `promoted_from_facts:0`, `file_upload` returns `id>0`). **PASS**

**AA3 — Equivalence proof (sha256 byte-identical before/after)**

EQUIVALENCE.json reports `before canonical_sha256 == after canonical_sha256 == cb4f198774e68deb54b21e7d2c0ffd2d3ac94b1a3a308adb7e022b51320cf417`, covering 108 ops with name/scope/local_only/description/schema in registration order. The before-handlers.cpp sha256 `7ac527f70e7dd5d9dc946249f46b0c3dbe3407e6320924672a28f758b743a38` matches PRE-GATE.json `scoped_files` exactly. B-DECOMPOSITION-VERIFY.txt independently confirms both hashes.25 domain functions partition the 108-op sequence contiguously with registration order preserved. **PASS**

**AA4 — Duplicate-registration defense**

`registry.hpp` declares `bool add(Operation op)`. `registry.cpp` implements: `if (ops_.find(op.name) != ops_.end()) return false; ops_[op.name] = std::move(op); return true;` — first registration wins, duplicate silently discarded with `false` return. `test_n31_c_negatives` exercises the full AA4 path: local registry returns `true` for fresh add, `false` for imposter, original scope/description/handler unchanged; global registry rejects re-adding `chronicle_on_this_day`, count before==after. The runtime log line `[INFO] n31-c: duplicate-registration defense active (bool add)` appears in all recorded suite runs (script + cmake, multiple rounds). **PASS**

**AA5 — Qualifying domains negative tests**

Three domains covered with≥2 ops each, all driven through `handle_rpc_body` (MCP typed-map path):
- **jobs**: `replay_job` (unknown field `bogus_field`, wrong type `"one"` for job_id, illegal ref424242), `send_job_message` (integer sender, unknown field), `list_job_messages` (unknown field `zzz`, string job_id, job424242 → `not_found`) — 3 ops, ≥2 validations each
- **schema**: `schema_stats` (unknown field, wrong type for `limit`, ghost source → `source_not_allowed` MCP / `source_not_found` CLI), `ontology_get` (unknown field, integer id, unregistered pack → `pack_not_found`), `reload_schema_pack` (unknown field, integer id) — 3 ops
- **chronicle/misc**: `chronicle_on_this_day` (unknown field, integer date, impossible calendar date, ghost source), `chronicle_backfill` (unknown field, wrong type `dry_run`, invalid `since`, ghost source), `log_ingest` (unknown field, wrong type `keep_last`, ghost source), `add_timeline_entry` (unknown field, integer title, ghost source) — 4 ops

All errors return structured `invalid_argument` (or `not_found`/`pack_not_found`/`write_denied`/`source_not_allowed`/`source_not_found`) with `code` + `field` + `message` fields confirmed by test assertions. pages/search/files explicitly out of scope per test comment and plan. Handler-layer agreement (CLI semantics via direct registry call) verified for replay_job, chronicle_on_this_day, schema_stats. Round-3 plan-audit P2("非法 source/brain 引用在合格域返回结构化错误") was adopted and is fully implemented. **PASS**

**AA6 — Suite ≥32, both paths, two rounds**

Script path: B-SUMMARY-SCRIPT-ROUND3.txt + B-SUMMARY-SCRIPT-ROUND4.txt = 33/33 PASS both. FINAL-VERIFY-SCRIPT.txt (A-INVENTORY-VERIFY.txt) shows run 1 (33/33) and run 2 (33/33). CMake path: B-SUMMARY-CMAKE-ROUND2.txt + B-SUMMARY-CMAKE-ROUND3.txt = 33/33 PASS both. FINAL-VERIFY-CMAKE.txt confirms33/33 (`[PASS] n31_c_negatives` / `[PASS] n31_a_counts_mapping` at file end, `cmake_build_exit=0`). Registration count33 = 31 N30 prior tests + `n31_c_negatives` + `n31_a_counts_mapping`, as expected. Both paths, ≥2 rounds each. **PASS**

**AA7 — Inventory determinism (byte-identical regeneration)**

A-INVENTORY-VERIFY.txt: `DETERMINISM_OK sha256=fcb39061a0edea7c3072a16a1d8af1672e372c071632e0e24b8afd93555bdfdb` (two consecutive in-pipeline runs byte-identical). MAPPING-CLOSURE.md records three end-to-end independent generations all matching the same sha256. The test additionally asserts the inventory is name-sorted, so any nondeterministic serialization fails the suite directly. **PASS**

**AA8 — Zero manual counts, all references generated**

The ledger header reads: "**Ops inventory (generated, N31 2026-08-15)**: docs/nodes/n31-evidence/OPS-INVENTORY.json — 108 registered ops (104 upstream + 4 extensions incl. list_job_messages), op→test mapping complete (0 gaps), deterministic (regenerated after list_job_messages reconciliation; sha256 fa772160…b3268b); enforced by test n31_a_counts_mapping." The inventory `generated_by: "scripts/gen-ops-inventory.ps1"` is confirmed present. The test constants (108, 104, 4) are frozen pre-gate values, not hand-counted during the node. **PASS**

---

### Governance Note — Extension Table 3→4 Reconciliation

**Assessment: coherent and honestly recorded.**

At pre-gate: extensions=3 (capture, list_brains, run_dream), `extensions_or_diff`=1 (`list_job_messages` — listed as "registered op absent from both ledger tables; N17 helper op"). During N31, `list_job_messages` was moved from `extensions_or_diff` into the ledger extensions table. The full reconciliation chain is consistent across all artifacts:
- MAPPING-CLOSURE.md records both the initial state (ledger_extension:3, extensions_or_diff:1) and explains the resolution
- OPS-PARITY-LEDGER.md now has 4 extension rows, with the 4th row carrying the explicit explanation: "N17 helper (Qbrain extension; read-only job message inbox; reconciled into the ledger by N31 ops inventory — registry count108 = upstream 104 + extensions 3+ this helper)"
- OPS-INVENTORY.json: `ledger_extension:4`, `extensions_or_diff:0`, empty `extensions_or_diff` array
- `kN31LedgerExtensionRows=4` with inline comment "N31 merge: list_job_messages reconciled into extensions table"
- N (=108) is unchanged throughout;104+4=108, extensions_or_diff=0 means total accountability is complete

The plan text's "扩展表(3)" in the audit disposition was the pre-gate state. The final implementation is strictly more complete: all 108 ops are formally classified in the ledger with no `extensions_or_diff` residue. The deviation improves the plan's invariant rather than relaxing it.

---

### D5 Methodology Assessment

Sound. The approach uses static canonical source extraction (b_split_extract.py) covering name/scope/local_only/description literal/schema literal in verbatim source text order for all 108 `register_one` calls. The before/after sha256 comparison of this canonical JSON proves the registrations are byte-identical. The rationale for not using a separate runtime export probe — parallel subagent construction of tests/* made a clean pre-decomposition full-suite run impossible — is explicitly documented and credible. The runtime cross-check (33/33 green including test_n31_a asserting runtime count=108) provides behavioral confirmation. The pre-gate frozen handlers.cpp sha256 ties the "before" extraction to the auditable baseline. The methodology satisfies the plan's intent of proving "no op's name/scope/local_only/schema changed."

---

### Security Assessment

The N30 authorization model is intact. `registry.cpp` retains the central choke point: remote Write/Admin requires an authenticated capability; MCP Write is default-deny without `--allow-write`; `local_only && remote` is blocked unconditionally. The only change to `registry.cpp` is the `add()` function (lines 9-13: guard + bool return). The EQUIVALENCE.json canonical sha256 includes scope and local_only fields verbatim — before/after equality means no scope or local_only changed for any op. The N30 test suite (test_n30_b_auth_redaction, test_n30_c_routing_storage) remains green in all recorded runs. No scope=admin/local_only op was altered.

---

### Findings

**P0 (blocks done)**: None.

**P1**: None.

**P2-1** — Plan text contains stale extension-count snapshot.
*File*: `docs/nodes/N31-PLAN.md`, lines 10 and 27(audit disposition P0-1 and D2 descriptions).  
*Summary*: Plan text reads "扩展表(3)" in two places; the implemented and test-enforced value is 4. This is a frozen historical snapshot from pre-gate time, not updated when list_job_messages was reconciled during the node.  
*Impact*: None on correctness — the test constant, inventory, and ledger all use the correct value 4. The discrepancy is a documentation artifact.  
*Why non-blocking*: The reconciliation chain is fully documented across MAPPING-CLOSURE.md, the ledger, the inventory, and the test code comment. The audit task explicitly asked to verify this chain is coherent, which it is.

---

### Conclusion

All eight acceptance assertions pass cleanly. The N31 node delivered a generated, deterministic, runtime-enforced 108-op inventory with zero mapping gaps; a live four-way count assertion (registry runtime == inventory rows == PRE-GATE N == parsed ledger); a byte-identical equivalence proof covering all 108 ops across the 25-unit `register_builtin_ops` decomposition; a bool-returning duplicate-registration guard; and per-domain input-contract negative tests for jobs, schema, and chronicle/misc that cover unknown fields, wrong types, illegal references, and MCP/CLI layer agreement. Both build paths produced33/33 green in two rounds each. The extension table growth from 3 to 4 (list_job_messages reconciliation) is an improvement over the plan's pre-gate baseline — every registered op is now formally classified in the ledger with zero residual `extensions_or_diff` — and is coherently and honestly recorded across all evidence artifacts.

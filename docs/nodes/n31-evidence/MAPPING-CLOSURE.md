# N31 D1/D2/D3 — inventory generation and op-to-test mapping closure (subagent A)

Date: 2026-08-15. Owner: subagent A slice (inventory/mapping). Inputs and outputs are
tool-extracted; no prose-only counts.

## Frozen baseline (PRE-GATE.json)

- Source-side `register_one` count: 108
- Ledger upstream implemented rows: 104
- Ledger extension rows: 3 (capture, list_brains, run_dream)
- **Runtime registry count N (measured): 108** — confirmed by the env-gated runtime
  export (test_n31.cpp n31-a, `QBRAIN_N31_EXPORT_REGISTRY`), measured AFTER subagent
  B's D4 decomposition landed (108 unique op names in the split registration units;
  register count unchanged, equivalence per B's D5).

## D1 — generated inventory

- Generator: `scripts/gen-ops-inventory.ps1` (PowerShell-first, runs under
  `powershell -NoProfile -ExecutionPolicy Bypass`).
- Sources, cross-checked at generation time:
  1. Runtime registry export (name/scope/local_only/description/input schema) written
     by the n31-a test's env-gated hook (test binary only; no production code).
  2. Static op-to-test extraction from `tests/*.cpp`: calls to registry-proxy helpers
     (`call_op`, `call_remote`, `mcp_call`, plus auto-detected functions whose bodies
     call `global_registry().call(` or `handle_rpc_body(`), direct
     `global_registry().call(...)` calls, and MCP `tools/call` request literals
     (`"name":"<op>"`). Case name = enclosing function of the call site.
  3. `docs/OPS-PARITY-LEDGER.md` upstream/extension tables (row mapping; the generator
     fails if any ledger row cannot map to a registered op).
- Output: `docs/nodes/n31-evidence/OPS-INVENTORY.json` — 108 rows
  (`{name, scope, local_only, description_summary, has_input_schema, ledger, tests[]}`),
  `has_input_schema` = declared schema different from the empty default
  `{"type":"object","properties":{}}`.
- Counts (generated, not hand-typed): registry_ops 108 = inventory_rows 108 =
  runtime N; ledger_upstream 104; ledger_extension 3; extensions_or_diff 1;
  ops_with_tests 108; ops_without_tests 0.
- `extensions_or_diff` (registry ops with no ledger row, named + reason):
  - `list_job_messages` — "registered op absent from both ledger tables; N17 helper
    op, listed as an 'uncounted Qbrain helper' in the ledger N17 notes; requires an
    explicit ledger disposition before it can be counted."
- Path hygiene: test mappings carry bare file names and function names only; no local
  absolute paths, no drive letters, no credentials (validated). The string
  `%LOCALAPPDATA%\Qbrain\brains` appears inside two ops' registry `description` text
  (generic documented path template, part of the op contract), not as a machine-local
  path disclosure.

## D2 — four-way assertion (tests/test_n31.cpp `// --- n31-a: counts/mapping ---`)

`test_n31_a_counts_mapping` asserts: runtime `global_registry().list().size()` ==
inventory row count == frozen N (108) == PRE-GATE; per-row name/scope/local_only match
the runtime registry; rows unique and name-sorted; every ledger upstream (104) and
extension (3) row parsed live from the ledger markdown maps to an inventory row with
the matching ledger classification; `extensions_or_diff` == exactly the registry ops
without a ledger row, each with a non-empty reason; every row's `tests` array
non-empty with path-free file names. Any drift fails the suite.

## D3 — mapping closure

- **Gap ops before D3 (30)** — implemented ledger/registry ops with zero extracted
  primary-path test mappings (method above, run before the D3 assertions were added;
  subagent C's n31-c section does not touch any of these):
  add_link, add_tag, advisor, file_upload, find_orphans, get_calibration_profile,
  get_chunks, get_links, get_page, get_stats, get_tags, list_brain_skillpack,
  list_brains, list_pages, ontology_conflicts, ontology_propose, put_raw_data,
  remove_link, remove_tag, restore_page, revert_version, run_skillopt,
  schema_explain_type, schema_graph, schema_review_orphans, sources_list,
  takes_calibration, takes_scorecard, takes_search, whoami.
- **Gap ops after D3: 0.** The n31-a section calls each gap op locally (CLI semantics)
  with minimal valid arguments on a temp-dir brain with `LOCALAPPDATA` redirected
  (nothing writes `%LOCALAPPDATA%\Qbrain`), asserting the primary path: ok + structured
  payload checks (e.g. `revert_version` reverts to a snapshotted version,
  `restore_page` after `delete_page`, `takes_calibration` promotes 0 facts,
  `run_skillopt` reports mode `no-mutate`, `file_upload` returns id > 0). All 30 now
  map to `{file: "test_n31.cpp", case: "test_n31_a_counts_mapping"}`.
- Gap counts recorded: before 30 / after 0 (`ops_without_tests: 0` in the inventory).

## Determinism proof

- In-pipeline: `gen-ops-inventory.ps1 -VerifyDeterminism` builds the full output twice
  and compares SHA-256: `DETERMINISM_OK
  sha256=fcb39061a0edea7c3072a16a1d8af1672e372c071632e0e24b8afd93555bdfdb`.
- End-to-end: three independent generations, each from its own fresh runtime export
  (separate full-suite runs), wrote byte-identical files:
  `fcb39061a0edea7c3072a16a1d8af1672e372c071632e0e24b8afd93555bdfdb`
  (generation 1: explicit export from suite round 1; generation 2: fresh export from
  suite round 2, compared with `cmp`; generation 3: script auto-run mode producing
  its own export). The two runtime exports are themselves byte-identical.
- The n31-a test additionally asserts the inventory is name-sorted, so any
  nondeterministic serialization would fail the suite.

## Suite evidence

- Direct-MSVC path (`scripts/build-tests-cl.ps1` with `-TestSources tests/test_n31.cpp`):
  **33/33 PASS, 0 FAIL** (31 prior + n31_c_negatives + n31_a_counts_mapping), two
  rounds plus the generator's auto-mode suite run (also exit 0). See
  `A-INVENTORY-VERIFY.txt` for the captured tails.
- Note (mid-merge coordination): subagent C's duplicate-registration block originally
  did not compile while `Registry::add` still returned `void` (`if constexpr` outside a
  template fully checks the discarded branch). Subagent A moved C's dual-state dispatch
  into `n31c_duplicate_registration_defense` (template helper, assertions verbatim) so
  the file compiles before and after subagent B's bool-returning `add`; with B landed,
  the full AA4 branch is active ("duplicate-registration defense active (bool add)").

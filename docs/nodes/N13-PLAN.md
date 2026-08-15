# N13 Plan - Live Sync, Sources, Graph Traversal, and Job/Facts Operations

**Status**: done (retrospective plan + outcome audit PASS 2026-07-30)
**Plan audit**: PASS (`N13-PLAN-AUDIT.md`, Claude Code, 2026-07-29)
**Outcome audit**: PASS (`N13-HARD-AUDIT.md`, Claude Code, 2026-07-30)
**Depends on**: N1-N12 approved and done; N1 MCP/write-scope rules, N2.5 source-id validation, N3 search/link contracts, N6 job foundations, N10 facts, and N12 token-fenced jobs/MCP default-deny behavior
**Process note**: retrospective re-audit under the updated N1-N11 gate rules. The historical N13 plan and hard audit are evidence only and cannot satisfy either current gate.

## Goal

Re-verify and, where necessary, correct the existing N13 implementation against the current Windows-native, multi-source isolation, MCP security, falsifiable-evidence, and node-gate rules. N13 is one bounded wave covering live-sync, source lifecycle/status, graph traversal, retry/facts operations, and their CLI/MCP surfaces. It does not include N14-N16 work or any orchestration node.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| sync_brain | MCP write path plus CLI `sync`; source-scoped live-sync |
| sources_remove | empty-source removal and explicit force purge with FK-safe cleanup |
| sources_status | active page/link counts and last-updated timestamp by source |
| traverse_graph | source-scoped, depth-limited bidirectional BFS |
| retry_job | failed/cancelled/dead -> waiting with fence reset |
| forget_fact | soft-deactivate facts with optional predicate filter |
| resolve_slugs | source-scoped existence/type resolution |
| recall | conservative read-only search alias |

## Deliverables

1. `include/qbrain/service/live_sync.hpp` and `src/qbrain/service/live_sync.cpp`: one-shot and bounded watch cycles; canonical source propagation; state namespaced by canonical brain, source, and notes root; path/symlink confinement; deterministic counters and ingest logging.
2. `src/qbrain/ingest/import.cpp` (and the smallest compatible header/API change): allow live-sync to pass the validated source id into file import so imported pages, links, and state all use the requested source.
3. `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`: source status, empty-source removal, explicit force cleanup with transactionally consistent page/link/chunk/tag/source handling, and fact deactivation semantics.
4. `include/qbrain/graph/traverse.hpp`, `src/qbrain/graph/traverse.cpp`, and the operation handler: source-scoped bidirectional BFS with depth bounds and cycle termination.
5. `src/qbrain/jobs/minions.cpp`, `src/qbrain/ops/handlers.cpp`, and CLI routing: retry/facts/source/sync operations registered with correct scopes and `local_only` flags; no N12 behavior regression.
6. Focused N13 tests and verification evidence, preferably in `tests/test_n13.cpp` plus extensions to existing live-sync/minion/MCP tests, with `scripts/n13-verify.ps1` capturing native build/test/runtime output and hashes.
7. `docs/nodes/N13-PLAN-AUDIT.md`, `docs/nodes/N13-HARD-AUDIT.md`, `docs/nodes/n13-evidence/VERIFY-REPORT.md`, and the N13 ledger notes after both Claude Code gates pass.

## Tests

- Native Windows/MSVC C++20 build through `scripts/build-tests-cl.ps1`; run the complete registered suite and record the exact count and exit code.
- Live-sync unit matrix using temporary databases and directories: first import, idempotent second run, changed-file reimport, markdown/markdown-extension/text filtering, invalid/missing directory, source-id validation, symlink/outside-root rejection, and bounded `live_sync_watch(..., max_cycles=1)` completion.
- Source isolation matrix: the same notes root is synced into two brains and two source ids; each imports independently, stores rows under the requested canonical source, and does not reuse another brain/source state file. Verify source status counts and `last_updated` from database contents.
- Source lifecycle matrix: default/empty ids rejected; empty custom source removed; non-empty source without force is a no-op; explicit force removes only the target source and its dependent pages/chunks/tags/links atomically under foreign keys; failed cleanup leaves a full snapshot unchanged.
- Graph matrix: direct outgoing and incoming neighbors, depth 2 two-hop results, depth 0/negative behavior, cyclic graph termination, no result beyond requested depth, and source isolation (identical slugs in two sources cannot leak).
- Jobs/facts matrix: retry only failed/cancelled/dead rows, clears lock/error fields, preserves payload and rejects waiting/active/paused/completed/unknown rows; forget all or one predicate, idempotence, unrelated entities remain active, and `list_facts` excludes inactive rows.
- Operation matrix: `sources_status`, `resolve_slugs`, and `recall` return structured read results; `sync_brain`, `sources_remove`, `retry_job`, and `forget_fact` are denied through a remote registry context without allow-write, leave a full database snapshot unchanged, and succeed through the explicit allow-write path where the requested mutation is valid.
- CLI smoke matrix: `sync <dir> --once`, `sync <dir> --watch --once`, `--source`, `--interval`, and graph output complete with stable exit codes and no infinite watch in the bounded case.

## Acceptance assertions (falsifiable)

1. A one-shot sync of a temporary notes root imports every supported note exactly once on the first run, reports zero imports and nonzero skips on an unchanged second run, and reimports a file after its content/signature changes.
2. `sync --watch --once` and the service watch API execute exactly one cycle and terminate; an invalid or missing root returns an error without importing unrelated files.
3. A valid canonical source id is persisted on imported pages/links, an invalid or reserved Windows source id fails before mutation, and outside-root files or symlink targets are never imported.
4. Sync state is isolated by canonical brain id, canonical source id, and canonical root: the same root can be imported independently into two brains/sources, and a state file from one scope cannot cause a false skip in another.
5. `sources_status` reports the target source's active page count, link count, and latest active-page update; it does not count another source or deleted pages.
6. Removing the default/empty source or a non-empty source without explicit force fails without changing a full database snapshot; an empty custom source can be removed.
7. Explicit force removal is opt-in, target-scoped, foreign-key safe, and atomic: it deletes the target source's dependent pages/chunks/tags/links and source row, preserves every other source, and leaves no orphaned dependent rows; an injected failure leaves the pre-call snapshot.
8. Graph traversal returns both directions up to the requested depth, terminates on cycles, returns no node beyond that depth, and never crosses source boundaries.
9. Retry changes only failed/cancelled/dead jobs to waiting, clears stale fence/error fields, preserves payload, and rejects every other status or unknown id without mutation.
10. Forget-fact deactivates all matching active facts or only the requested predicate, is idempotent, leaves unrelated facts active, and the active-facts read path excludes deactivated rows.
11. The four N13 write operations (`sync_brain`, `sources_remove`, `retry_job`, `forget_fact`) are registered `local_only=true`; remote calls without explicit allow-write are denied before handler mutation and full snapshots are byte-identical, while explicit allow-write exercises the valid path.
12. Read operations (`sources_status`, `traverse_graph`, `resolve_slugs`, `recall`) return stable structured output, preserve source scope, and do not write the database.
13. Native MSVC evidence records Windows/x64, compiler version, `/std:c++20`, exact build and verification commands, exact test count, runtime markers, full-snapshot hashes, and all dependency audit references; no model/provider/baseURL/key/reasoning/context/compression configuration changes.

## Rollback

- Disable live-sync by omitting the `sync` command or `--watch`; existing pages and jobs remain usable.
- Do not run force source removal unless the explicit destructive request is intended; take a database backup before destructive cleanup.
- No schema bump is planned. Revert the implementation slice or restore the database backup if a force-cleanup rehearsal fails.
- Keep MCP remote writes disabled unless the explicit allow-write switch/token is present.

## Security notes

- Validate and canonicalize source ids with the existing Windows-safe `Brain::ensure_source` contract, and fail closed instead of silently falling back to `default`.
- Resolve the sync root and each file with Windows filesystem canonicalization; reject symlink/path escapes and never use a caller-controlled source id as a filesystem path.
- Namespace or store sync state under the brain/source scope to prevent cross-brain/source false skips and cross-tenant data exposure.
- Register every N13 mutating MCP operation as `local_only=true`; the registry must deny remote calls before touching SQLite when allow-write is absent.
- Force source cleanup must use a transaction and explicit target predicates; no secrets or provider/model settings are added to code, tests, logs, or evidence.
- Tests use temporary paths/databases and do not touch production `%LOCALAPPDATA%\\Qbrain` data except isolated verification artifacts.

## Parallelism notes (after plan approval only)

- Live-sync/import/source lifecycle slice.
- Graph traversal and source-scoping slice.
- Jobs/facts/operation registry and focused tests slice.
- The parent agent owns merge review, native build/full suite, evidence manifest, ledger, and both Claude Code gates. No slice may mark N13 done or write a PASS audit.

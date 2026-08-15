# N15 Plan - Source-scoped Ingest Provenance and Chronicle Reads

**Status**: done (plan + outcome audit PASS 2026-08-04)
**Depends on**: N1-N13 approved and done; specifically N1 write provenance/MCP default-deny, N2 page/version/link contracts, N2.5 canonical source ids and remote source authorization, N5 import hooks, N7 authenticated loopback MCP, N8 brain isolation, N11 Windows quality evidence, and N13 source-scoped live-sync
**Plan audit**: PASS (`N15-PLAN-AUDIT.md`, Claude Code, 2026-07-30)
**Outcome audit**: PASS (`N15-HARD-AUDIT.md`, Claude Code, 2026-08-04)
**Process note**: This is a retrospective re-plan under the current node rules. The 2026-07-26 `N15-HARD-AUDIT.md` is historical evidence only: N15 has no node-specific plan-audit artifact, and that outcome document did not audit an approved current plan. It cannot satisfy either current gate. No N14/N16/N18 implementation and no N30 orchestration work belong to this node.

## Goal

Re-verify and, where necessary, correct the existing N15 implementation as one bounded capability wave:

1. Source-scoped `link_source` provenance counts.
2. A bounded, source-attributed ingest event log used by explicit calls, file import, and live-sync.
3. Deterministic UTC page-activity reads for a day or inclusive lower-bound timestamp.
4. A clearly documented Qbrain thin timeline entry implemented as a `type=timeline` page while preserving the N1 write-path and MCP security contracts.

The node must not claim the upstream page-attached timeline/event model, week/narrative/kind projections, or full Life Chronicle parity. It must not change an LLM provider, model, base URL, API key, reasoning setting, context size, or compression setting, and it introduces no new third-party dependency.

## Ledger rows to reconcile after outcome PASS

The ledger already contains historical N15 claims. They remain evidence candidates, not a current gate result, until the outcome hard audit passes. Only these rows may be reconciled by N15:

| op | intended bounded evidence |
|----|---------------------------|
| list_link_sources | histogram of open `link_source` provenance values within one canonical source |
| log_ingest | validated, source-attributed, retention-bounded ingest event write |
| get_ingest_log | newest-first ingest events from only the requested/authorized source |
| chronicle_day | active pages created or updated during one strict UTC calendar day, source-scoped |
| chronicle_since | active pages created or updated at/after one strict UTC timestamp, source-scoped |
| add_timeline_entry | Qbrain thin `type=timeline` page write; not an upstream page-attached timeline row |

No other operation may be newly attributed to N15. `chronicle_on_this_day`, `chronicle_last_seen`, `chronicle_backfill`, `get_timeline`, and later chronicle/ontology work retain their own node ownership and audits.

## Scope and exclusions

In scope: the N15 methods in `include/qbrain/core/brain.hpp` and `src/qbrain/core/brain.cpp`, the v7 `ingest_log` successor migration in `src/qbrain/storage/migrate.cpp`, N15 registrations in `src/qbrain/ops/handlers.cpp`, source propagation from `src/qbrain/ingest/import.cpp` and `src/qbrain/service/live_sync.cpp`, focused tests, native Windows verification evidence, and the final N15 ledger reconciliation after both gates pass.

Out of scope: a new timeline/event table, rewriting later N17-N28 capabilities, full upstream PostgreSQL/JSONB chronology, week or narrative rendering, event-kind projections, a schema downgrade tool, cross-brain aggregation, arbitrary remote filesystem access, any model/provider configuration change, and any N30 plan or implementation.

## Deliverables

1. Source-scoped link provenance:
   - `list_link_sources` accepts one canonical `source_id` (default `default`) and groups `links.link_source` only inside that source.
   - `link_source` remains an open provenance string distinct from the source registry id; N15 does not introduce a provenance allowlist.
   - Counts are deterministic (`count DESC`, then provenance ascending) and never include links from another source or another brain.
2. Source-attributed ingest schema and migration:
   - Add the next schema migration, v12 at plan-draft time, to replace the v7 `ingest_log` with an equivalent table containing canonical `source_id TEXT NOT NULL` and a foreign key to `sources(id)` with target-scoped cleanup semantics.
   - Preserve `id`, `event_type`, `path`, `detail_json`, and `created_at`; backfill pre-v12 rows to `default` because their original source cannot be inferred honestly.
   - Add an index supporting `(source_id, created_at DESC, id DESC)` reads.
   - The migration is transactional and idempotent for fresh databases and populated v11 databases. A failure before the v12 marker leaves the old table, rows, indexes, and version marker unchanged. Downgrade and pre-v12 write compatibility are explicitly unsupported.
3. Bounded ingest writes and reads:
   - `log_ingest` validates/canonicalizes `source_id` before mutation, stores it on every row, and prunes retention inside the same source only. Insert plus prune is one transaction so one source cannot delete or expose another source's history.
   - Missing `keep_last` means 100; values below 1 clamp to 1; values above 1000 clamp to 1000. Non-numeric or overflow MCP input fails before mutation.
   - `event_type` is at most 64 bytes, `path` at most 4096 bytes, and `detail_json` at most 65536 bytes and syntactically valid JSON. Boundary-plus-one or malformed input fails without inserting or pruning.
   - `get_ingest_log` defaults to 20 rows and clamps to 1..50, returns newest first with `id` as the deterministic tie-breaker, and returns only the requested source.
   - `import_path` and `live_sync_once` pass the N13-validated canonical source into every success/error summary they log. An invalid source fails before any page or ingest-log mutation.
4. Strict, source-scoped chronicle reads:
   - `chronicle_day` accepts exactly a real UTC date in `YYYY-MM-DD` form. `chronicle_since` accepts `YYYY-MM-DD` or `YYYY-MM-DD[T ]HH:MM:SSZ`; date-only means `00:00:00Z`. Invalid syntax, impossible dates/times, offsets, trailing data, and overflow fail closed.
   - Both operations filter `pages.source_id`, exclude `deleted_at IS NOT NULL`, and return a page once when either `created_at` or `updated_at` matches the inclusive interval.
   - Results order by effective activity timestamp descending and then page id descending. Missing limit means 100 and the effective limit is clamped to 1..200.
   - Read results include enough source/date metadata to prove which canonical scope and normalized UTC boundary were applied.
5. Thin timeline write with inherited write-path guarantees:
   - Keep the ledger's explicit Qbrain subset: one `add_timeline_entry` call creates or updates a `type=timeline` page, rather than pretending to append an upstream page-attached timeline row.
   - Require non-empty title or body. A caller-supplied slug follows normal `put_page` overwrite/version behavior; automatically generated slugs are unique even for two identical entries created in the same clock second.
   - Canonicalize and validate `source_id`, enforce the N2.5 remote source allowlist, stamp accurate local/MCP provenance, replace chunks, and enqueue embedding exactly once through the existing N1 path. Remote writes do not auto-extract links.
6. Operation security and registration:
   - `log_ingest` and `add_timeline_entry` remain `Scope::Write` and are guarded with `local_only=true` in the current registry semantics so a remote call without explicit allow-write is rejected before handler mutation; an explicitly allowed valid call is tested separately.
   - `list_link_sources`, `get_ingest_log`, `chronicle_day`, and `chronicle_since` remain `Scope::Read` and do not require allow-write.
   - Every N15 operation validates one canonical source. A remote non-default source must be present in `mcp.allowed_sources`; an unauthorized or invalid source is rejected rather than silently falling back to `default` or returning another source's data.
7. Focused verification artifacts:
   - Add focused N15 coverage, preferably `tests/test_n15.cpp`, and register it in CMake, `scripts/build-tests-cl.ps1`, and `tests/test_main.cpp` without weakening the full suite.
   - Add `scripts/n15-verify.ps1` and `docs/nodes/n15-evidence/VERIFY-REPORT.md` to record the native compiler/build/test/runtime evidence. Runtime evidence is not an audit verdict.
   - After implementation and verification, obtain a real Claude Code outcome hard audit against the approved plan before changing this plan to `done` or reconciling the ledger.

## Tests

All tests run on native Windows PowerShell/MSVC C++20 without WSL or Docker.

1. Build and regression suite:
   - Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-tests-cl.ps1` and then `build\cl\qbrain_tests.exe`.
   - Record exact exit codes and the exact registered test count. Every test must pass and the count must be at least the N13 baseline of 21 plus the registered N15 test.
2. Migration matrix:
   - Fresh open reaches schema v12 or newer with the source/index/FK contract present.
   - Build a populated v11 fixture with two existing ingest rows, migrate it, and prove ids, text, timestamps, and row count are unchanged while both legacy rows receive source `default`.
   - Run migration a second time and prove it is a no-op.
   - Inject a deterministic failure during the v12 rebuild and compare schema SQL, row hashes, indexes, and `schema_version` before/after; all remain identical.
   - Force-remove a custom source and prove its ingest rows are cleaned up without affecting another source, with `PRAGMA foreign_key_check` empty.
3. Source and retention matrix:
   - Create default and `team_a` links with overlapping provenance values; each source's `list_link_sources` count excludes the other.
   - Write more than the requested retention to `team_a` and default; prove pruning is newest-per-source and cannot delete the other source's rows.
   - Verify newest-first deterministic ordering, default/1/50/over-max limits, `keep_last` 0/1/1000/over-max behavior, and non-numeric/overflow no-mutation behavior.
   - Reject invalid/reserved/mixed-case source inputs according to the N2.5 canonicalization contract; `Team_A` resolves to stored `team_a` and no duplicate source is created.
   - Exercise event/path/detail exact size boundaries and boundary-plus-one, plus malformed JSON; every rejected case leaves a full database snapshot unchanged.
4. Import/live-sync attribution matrix:
   - Import and one-shot sync fixtures into `team_a`; every emitted ingest row is attributed to `team_a`, includes bounded counter-only JSON, and no equivalent default-source row appears.
   - Repeat with another brain using the same source/root and prove physical brain isolation.
   - Invalid source and missing-path cases follow the declared error contract without cross-source log writes.
5. Chronicle matrix:
   - Seed deterministic created/updated timestamps immediately before, at, and after a UTC day/since boundary in two sources; verify inclusive boundaries, no duplicates, deterministic tie ordering, source isolation, and limit clamping.
   - Soft-delete a matching page and prove it is absent while an active matching page remains.
   - Accept a leap-day fixture for a leap year; reject `2025-02-29`, month/day/time overflow, timezone offsets, partial strings, and trailing garbage without process failure or database writes.
6. Timeline and MCP matrix:
   - A valid local call creates a gettable page with `type=timeline`, requested canonical source, correct provenance, chunks, and one embed job; two auto-slug calls with identical content in the same second produce two distinct pages.
   - Empty payload and invalid source fail with a byte-identical full database snapshot.
   - Remote `log_ingest` and `add_timeline_entry` without allow-write are denied before mutation and preserve a full snapshot. Explicit allow-write plus an authorized source succeeds; a non-allowlisted source fails unchanged.
   - Remote read calls for an authorized source return only that source; a non-allowlisted source is denied. All four read operations leave the full database snapshot unchanged.
7. Evidence manifest:
   - Record Windows edition, x64 process/target, full `cl.exe` version, `/std:c++20`, exact commands, exact test count, runtime markers, migration/schema hashes, and before/after database snapshot hashes.
   - Record hashes of all N15 deliverables and references to PASS plan/outcome audits for N1-N13. Assert that no model/provider/baseURL/key/reasoning/context/compression configuration changed.

## Acceptance assertions (falsifiable)

1. A populated v11 database migrates transactionally to v12 or newer with every legacy ingest row preserved and backfilled to `default`; a second migration is a no-op and an injected mid-migration failure leaves schema, data, indexes, and version marker byte-equivalent to the pre-run snapshot.
2. Every new ingest row carries a valid canonical source id, import/live-sync use the requested N13 source, and the same source/root in another brain cannot share or expose ingest events.
3. Retention is applied atomically per source with the exact 1..1000 boundary contract; pruning one source never changes another source's rows.
4. `get_ingest_log` returns at most 50 rows in deterministic newest-first order from only the requested source, and invalid/oversized event input or malformed JSON performs neither insert nor prune.
5. `list_link_sources` reports exact provenance counts for the requested canonical source and never includes another source or brain, while leaving open provenance strings unchanged.
6. `chronicle_day` accepts only a real `YYYY-MM-DD` UTC date and returns each active matching page once; day-start/day-end fixtures, leap day, soft deletion, and source isolation all match the declared boundary.
7. `chronicle_since` normalizes only the declared UTC forms, uses an inclusive lower bound, rejects invalid/offset/trailing input, returns deterministic bounded output, and never writes the database.
8. A valid `add_timeline_entry` produces a `type=timeline` page with correct source/provenance/chunks/embed enqueue, and two identical auto-slug calls in one second cannot overwrite one another.
9. Remote `log_ingest` and `add_timeline_entry` without explicit allow-write are denied before handler mutation with identical full database snapshots; the valid allow-write path succeeds only for an authorized canonical source.
10. Remote N15 reads cannot select a non-allowlisted source and authorized reads return no row from another source; all N15 read calls preserve the database snapshot.
11. The ledger and user-facing descriptions explicitly call `add_timeline_entry` a thin timeline-page subset and `chronicle_day/since` page-activity reads; no page-attached event, week/narrative/kind, or full upstream Chronicle parity is claimed.
12. Native Windows x64 MSVC evidence records `/std:c++20`, exact commands and counts, all registered tests passing at or above the N13 baseline plus N15, N15 runtime/hash markers, and PASS dependencies through N13 before the outcome audit.

## Rollback

- Keep N15 operations unregistered or disabled if implementation verification fails; existing page, search, and N13 sync capabilities remain usable.
- Do not downgrade a migrated database. Back up the database before the v12 migration and restore that backup if rehearsal or deployment fails.
- Stop automatic import/live-sync logging if provenance logging causes an operational issue; do not weaken source validation or MCP write denial as a workaround.
- Keep remote writes disabled by omitting allow-write. Reverting handler/API changes does not authorize use of a pre-v12 writer against a migrated database.

## Security notes

- `log_ingest` and `add_timeline_entry` are mutations and remain MCP default-deny. Denial must happen before SQLite or filesystem side effects.
- Source ids use the existing N2.5 Windows-safe canonicalization. Remote source authorization applies to both N15 writes and sensitive provenance/chronicle reads so one source cannot enumerate another source's paths, summaries, pages, or link provenances.
- Ingest paths and detail JSON are untrusted and potentially sensitive. Use bound SQL parameters, strict size/JSON validation, source-scoped reads, and counter-only automatic summaries; never execute logged text or include tokens, provider responses, page bodies, or environment secrets in automatic log details.
- Tests use temporary brains, files, and loopback-free local calls only. They never mutate production `%LOCALAPPDATA%\Qbrain` data.
- The migration uses an explicit transaction and foreign-key verification. No destructive cleanup uses an unscoped predicate.
- No secrets are committed, and no LLM configuration setting is read or changed for N15 verification.

## Parallelism notes (after plan approval only)

- Migration/core source-scoping and retention slice.
- Chronicle validation/query slice.
- Handler/timeline/MCP security slice.
- Focused tests and Windows evidence script slice.
- The parent agent owns conflict resolution, the full native build/test run, evidence review, ledger reconciliation, and both Claude Code gates. No slice may mark N15 approved/done or write a PASS audit.

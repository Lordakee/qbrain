# VERDICT: PLAN_REVISE

**Auditor**: Claude Code — master-plan hard audit (N0 gate)
**Date**: 2026-07-25
**Target**: `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` v1.0.0-draft
**Corroborating sources read**: `docs/01-ANALYSIS.md`, `README.md`, `docs/reviews/CLAUDE_HARD_AUDIT_PASS.md`, `docs/reviews/CLAUDE_MCP_HARD_AUDIT.md`, live tree (`src/`, `include/`, `schema/`), upstream mirror `gbrain-upstream/` (gbrain 0.42.65.0, MIT)
**Method**: plan claims were not taken on trust — each factual assertion in §1.2, §5 and §8 was checked against Qbrain source and against upstream source where the mirror contains it.

---

## Summary

The plan is structurally sound and the sequencing instinct is right: `N1 → N2 → N3` is the correct spine, the "capability parity, not bit-level port" framing is the only honest one available, and §1.3 (诚实边界) is the strongest part of the document — it pre-empts the failure mode that kills projects like this.

It does not pass as written. Four P0 defects:

1. **The storage bootstrap is broken in exactly the deployment mode the README advertises**, and the plan builds eleven nodes of migrations on top of it (`src/qbrain/core/brain.cpp:22-56`). Not a plan-text problem — a foundation problem the plan does not know about.
2. **N1's "MCP 默认允许 write（与 gbrain 一致）" rests on a false premise.** gbrain has no `capture` MCP operation at all, and its default-write `put_page` is safe only because of remote-caller mitigations Qbrain lacks and the plan never mentions. N3 then adds the backlink boost that turns the gap into a search-poisoning primitive.
3. **"所有功能" has no falsifiable test.** §1.3 makes the ops registry the mechanism and §4 makes D1–D25 the gate, but no node owns the ledger that maps upstream's 132 op names onto {implemented | planned-N*k* | out-of-scope+reason}. Current surface: **9 ops** (`src/qbrain/ops/handlers.cpp`) vs **132** upstream. A 14× expansion is asserted with zero per-node commitment.
4. **The evidence base is a 53-file partial mirror**, and the plan makes confident behavioral claims about domains whose source is absent from it (D10–D16, D19–D22). N6 is sized 大 against files that were never read.

P0-1 and P0-2 are engineering blockers before N1 opens. P0-3 and P0-4 are cheap document fixes, but they must land in N0 — a ledger written after the code is a ledger written to match the code, and an unrecorded provenance gap silently propagates into every N5+ audit.

Fix these four and the plan is approvable. Nothing in the architecture (§2), the risk register (§6), or the node list (§3) needs to be rethought.

---

## Answers to plan §7 questions (1-5)

### Q1 — 节点划分是否合理？是否应合并/拆分？

Mostly reasonable. Keep 11 nodes as the skeleton; three defects:

- **Source axis is 5 nodes too late.** `01-ANALYSIS.md:35-39` states the rule the plan then breaks: *"查询路由必须同时尊重两轴，否则会静默串数据."* Source isolation (D18) sits in N8, after N3 builds the ranking pipeline and N6 builds jobs/dream. Both write and read paths would be built against a weak source axis and revisited. Worse, it is already leaking: `put_page` takes `source_id` straight from caller params with no validation and no remote allow-list (`src/qbrain/ops/handlers.cpp:96`), so an MCP caller can write into any source. Split N8: the **source axis** (schema + op-level `sourceId` plumbing + remote scope enforcement) moves to a new **N2.5** immediately after N2; **multi-brain mount** — genuinely additive — stays at N8.
- **N4 conflates two unrelated deliverables.** "多轮 think" is a synthesis-loop change gated on N3's retrieval quality. "provider recipes / 多 provider 网关" (D24) is a config+HTTP concern gated on nothing, and is a *prerequisite* for N6's dream phases and N10. Split: **N4a = AI gateway** (small, unblocks N6/N10), **N4b = think 多轮 + save 策略**. Also fix the internal inconsistency: §1.2 maps D24 to "N4 扩展", §3 puts it inside N4.
- **N9 has a non-dependency.** "依赖 N7 或 N1" — 或 is not a dependency. `list_skills`/`get_skill` need the ops registry only, i.e. N1. Pin to N1 and mark N9 schedulable in parallel from that point; it is the cheapest visible-parity win in the plan.

No merges recommended. Resist the temptation to merge N1 into N2 — N1 is the node that sets the write-path trust contract, and it deserves its own gate.

### Q2 — 「能力对齐而非位级复刻」是否可接受为「所有功能」的操作定义？

**Yes, conditionally.** It is the only defensible definition: `gbrain-upstream/src/core/operations.ts` is 5,679 lines carrying 132 distinct op names, against a 3,384-LOC C++ codebase exposing 9. Demanding 1:1 would guarantee the §6 failure mode ("范围过大导致永不完工").

The condition is **P0-3**: capability parity is only auditable if the capability set is enumerated. Without a ledger, "D1–D25 均达可用" is a judgment call made 11 times by whoever is in the room, and the final N11 audit has nothing to fail against. With the ledger, approve the definition as written — including §1.3's explicit permission to defer, which should be *used* and recorded, not treated as an escape hatch.

### Q3 — N1 MCP 默认允许 write 是否与安全姿态冲突？

**Yes, as written.** Three independent problems — detail in **Security** below.

The parity claim is factually wrong on `capture`: it does not exist as a gbrain operation. It is in gbrain's `CLI_ONLY` set (`gbrain-upstream/src/cli.ts:58`) and appears nowhere among the 132 op names. "与 gbrain 一致" for capture means **not exposing it over MCP** — the opposite of what N1 proposes.

`put_page` default-write *is* genuine parity (`scope: 'write'`, not `localOnly`, dispatched with `remote: true` in `gbrain-upstream/src/mcp/server.ts`). But upstream pairs it with four mitigations Qbrain does not have. Qbrain's `put_page` runs link extraction unconditionally for remote callers (`handlers.cpp:107-108`), and N3 adds the backlink boost. That combination is upstream's *risk* without upstream's *controls*.

**Recommendation**: keep default-deny for this release. If default-allow is a hard product requirement, it ships in the same node as the mitigation set — never before.

### Q4 — 是否有必须提前的节点？

Yes, three moves; none needs to go later.

| Move | From | To | Why |
|------|------|----|-----|
| Source axis (D18) | N8 | **N2.5** | 静默串数据 is a read-path correctness bug; N3/N6 must not be built on a weak axis. Already leaking at `handlers.cpp:96`. |
| AI gateway (D24) | N4 | **N4a**, before N6 | N6 dream phases need chat under a worker; N10 needs it too. |
| Skills (D19) | N9 (after N7) | any time after N1 | Only needs the ops registry. Cheapest parity win; good filler while N5/N6 grind. |

**N10 should shrink rather than move.** `01-ANALYSIS.md:76` calls tree-sitter "可移植；后期可选" — on MSVC the C core builds cleanly but each grammar is a separate vendored C source. That is real work, not a wasm drop-in. Declare 1–2 languages up front.

### Q5 — VERDICT

**PLAN_REVISE.** 4× P0, 7× P1, 6× P2 below. No P0 is architectural; all four are addressable inside one revision cycle.

---

## P0 changes (must fix before any node starts)

### P0-1 — Schema bootstrap silently degrades in the flagship deployment mode; every node's migrations inherit it

**Evidence**: `src/qbrain/core/brain.cpp:22-56`, `src/qbrain/storage/migrate.cpp:31-66`, `schema/001_init.sql`.

`open_at()` locates the schema through CWD-relative candidates (`schema/001_init.sql`, `../`, `../../`, `./`) plus `QBRAIN_SCHEMA`. On miss it executes an **inline fallback DDL and never calls `apply_migrations()`**. Three consequences, compounding:

1. **The fallback is not the canonical schema.** It omits all 7 indexes (`idx_chunks_page`, `idx_jobs_claim`, `idx_links_from`, `idx_links_to`, `idx_pages_source`, `idx_pages_type`, `idx_pages_updated`) and all 3 foreign keys — including `content_chunks.page_id → pages(id) ON DELETE CASCADE`.
2. **Migrations are skipped, then permanently locked out.** The fallback stamps `schema_version = 1`. A later run with a resolvable CWD sees `ver == 1`, skips the v1 bootstrap, and applies only v2. The missing v1 indexes and FKs are **never repaired on any future run.**
3. **The trigger is the primary install path.** `README.md:27-30` documents `claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve`. MCP clients set CWD to the user's project directory, not the Qbrain repo. The advertised entry point is precisely the one that produces the degraded schema — and `doctor` cannot see it, because `get_health` reports version and row counts only (`handlers.cpp:52-77`), so it prints `OK schema v2` over a database missing half its DDL.

Why this is P0 rather than a bug filed against N1: the plan's own global gate (§4, "破坏性变更有迁移") is unsatisfiable while two divergent schema lineages exist. N2 adds a versions table and `purge`; on a fallback-created brain, purge relying on `ON DELETE CASCADE` orphans chunks instead of deleting them. Each node makes the divergence more expensive.

**Required fix, in N0 or a dedicated N0.5:**
- Compile the canonical schema in as a string constant generated from `schema/001_init.sql` at build time. One source of truth, no filesystem lookup, no drift possible.
- Route **both** paths through `apply_migrations()`. Delete the divergent inline DDL.
- Add a schema-integrity check to `doctor`: assert the expected table/index/FK set, report `DEGRADED` on mismatch. Detection now; `--remediate` may wait for N6 (D16).
- Add a regression test that opens a brain from an unrelated CWD and asserts the full index and FK set.
- Decide and document the repair path for any brain already created through the fallback.

### P0-2 — N1's write-default flip is justified by a false parity claim and drops upstream's compensating controls

**Evidence**: plan §5 vs `gbrain-upstream/src/cli.ts:58`, `gbrain-upstream/src/core/operations.ts:842,850,1019,1040-1070,1150-1165`, `gbrain-upstream/src/mcp/server.ts`, `src/qbrain/ops/handlers.cpp:91-116`.

Plan §5 states: 默认允许 `put_page`/`capture`（与 gbrain 一致）.

- **`capture` is not a gbrain operation.** CLI-only (`cli.ts:58`); absent from all 132 op names. Parity means *not* exposing it over MCP.
- **`put_page` default-write is real parity, but conditional.** Upstream ships four mitigations for `ctx.remote !== false` that Qbrain has none of:
  - auto-link and auto-timeline **skipped for remote callers**, with the reason stated in-source: the bare-slug regex matches inside code fences and prompt-injected content, an untrusted page can plant arbitrary outbound links, *"Combined with the backlink boost in hybridSearch, attacker-placed targets would surface higher in search"* (`operations.ts:1042-1049`);
  - frontmatter values **overridden** for MCP callers (`operations.ts:850`);
  - provenance tagged `mcp:put_page` vs `put_page` (`operations.ts:1019`);
  - stdio callers pinned to `takesHoldersAllowList: ['world']` so private content stays invisible (`mcp/server.ts`).
- **Qbrain would import the risk without the controls.** `put_page` calls `extract_links` + `replace_extracted_links` unconditionally, `ctx.remote` unchecked (`handlers.cpp:107-108`). N3 then adds the backlink boost. Upstream documented this exact attack chain and closed it; the plan proposes opening it.
- **It is a regression against the current shipped posture**, which `CLAUDE_MCP_HARD_AUDIT.md` recorded as *"safer than gbrain open MCP writes"*.

**Required fix:** rewrite §5's MCP-write row as an explicit decision with the mitigation set as a precondition. Either keep `--allow-write` default-deny (recommended — see Security), or ship default-allow together with: remote link-extraction skip, remote frontmatter override, `mcp:` provenance tagging, remote `source_id` allow-list, and a `capture` decision recorded as an intentional non-parity extension. Remove the incorrect 与 gbrain 一致 justification either way.

### P0-3 — No node owns the ops parity ledger, so "所有功能" is unfalsifiable

**Evidence**: plan §1.1 (契约优先), §1.3 (预留/分批落地), §4 (final gate) vs `src/qbrain/ops/handlers.cpp` (9 ops) and `gbrain-upstream/src/core/operations.ts` (132 names).

The plan names the mechanism and the gate but never creates the artifact connecting them. Consequences: no node has an op-surface commitment; "D1–D25 均达可用" is re-litigated at every gate; the N11 全项目硬审 the user explicitly asked for has nothing objective to fail against.

**Required fix:** create `docs/OPS-PARITY-LEDGER.md` in N0. One row per upstream op name (all 132 — the list is mechanically extractable), columns: `upstream_op | domain (D*) | qbrain status {implemented | planned-N*k* | out-of-scope} | rationale if out-of-scope | qbrain op name if renamed`. Rules: every node plan must state which rows it moves to `implemented`; every node audit verifies those rows and no others silently regressed; `out-of-scope` requires a written reason (this is §1.3 being *used*, which is legitimate — unrecorded omission is not). Add a per-domain 验收断言 column to §1.2 so each D-row carries one falsifiable sentence.

### P0-4 — The upstream evidence base is partial, and the plan asserts behavior for absent files

**Evidence**: `gbrain-upstream/` contains 53 files (shallow, gitignored). `src/core/minions/index.ts` is a 9-line barrel — `queue.ts`, `worker.ts`, `backoff.ts`, `types.ts` are **absent**. `src/core/ingestion/index.ts` is a 33-line barrel; the daemon and all source implementations are **absent**. Also missing: `mcp/dispatch.ts`, `mcp/tool-defs.ts`, `search/rerank.ts`, `search/autocut.ts`, `search/adaptive-return.ts`, and everything under dream/cycle, skills, facts, code-intel.

The plan states upstream behavior for D10–D16 and D19–D22 anyway, and sizes N5/N6/N9/N10 against it. **N6 is sized 大 on the basis of files nobody has read.** Those four nodes therefore have unverifiable acceptance criteria, and their audits would inherit an invisible unknown.

Vendoring itself is clean — `gbrain-upstream/` is gitignored (`.gitignore:31`), upstream is MIT, and `LICENSE` is present. This finding is about **evidence provenance, not licensing.**

**Required fix (documentation, cheap):** add a §1.4 "证据来源与置信度" recording which domains are source-verified vs doc-derived vs unverified, and gate N5 on completing the mirror for `minions/`, `ingestion/`, `search/rerank|autocut|adaptive-return`, `mcp/dispatch|tool-defs` — or on explicitly re-deriving those requirements from gbrain's public docs and labelling them as such. Do not re-read everything now; just stop the unknown from being invisible.

---

## P1 changes (should fix)

**P1-1 — Node graph corrections.** Implement the three moves from Q4 (source axis → N2.5; AI gateway → N4a before N6; N9 pinned to N1). Replace the §3 "依赖" column with a real DAG; "N7 或 N1" is not a dependency expression.

**P1-2 — N4 must be split.** Gateway (D24) and think-multi-round are independent and have different downstream consumers. As one node, N6 and N10 are blocked on think work they do not need.

**P1-3 — N7 has no threat model.** It is the only node in the plan that opens a network listener, and §4's global gate covers only "无密钥进入仓库". Add N7-specific acceptance criteria: loopback-only default bind; token required with no anonymous mode even in dev; constant-time comparison; **token never in argv** (Windows command lines are readable by other processes via WMI/`Get-CimInstance Win32_Process`) — env or DPAPI-protected store only; request size and rate limits; admin panel read-only and off by default. Promote to a conditional gate in §4: "any node exposing a network surface must state bind address, auth mode, and default-off posture."

**P1-4 — No anti-regression or performance floor.** N3 boosts, N2.5 source filters and N10 facts all pile onto the read path with nothing measuring it. §6 flags the vector-scale risk and defers to "N3 引入 sqlite-vec 或外置索引", but N3's §3 row does not mention a vector index at all, and today's search full-scans (`CLAUDE_HARD_AUDIT_PASS.md`: doctor warns above 5,000 embedded chunks — against upstream's ~146k pages). Fix: a fixed corpus fixture + recorded search latency baseline in N1, re-measured at every subsequent gate; and either move ANN into N3's explicit deliverables or state v1's supported corpus ceiling and enforce it in `doctor`.

**P1-5 — A superseded audit is still authoritative, in a process built on audits.** `CLAUDE_MCP_HARD_AUDIT.md` M2 records *"Content-Length framing — PASS"* and lists *"NDJSON fallback framing"* as a remaining gap. The code does the opposite: NDJSON is primary and Content-Length header lines are skipped (`src/qbrain/mcp/jsonrpc.cpp:8-11`, which cites the MCP SDK's actual stdio behavior). The current code is **correct** and plan §8 describes it correctly — but a stale PASS gate left unmarked will be cited later. Fix: add a status header (`SUPERSEDED BY <doc> ON <date>`) to superseded review docs. Cheap insurance on the audit chain the user is deliberately building.

**P1-6 — "写后自动 embed" has no failure-mode spec.** N1 says "异步或同步" — that 或 *is* the design decision. Synchronous means every MCP `put_page` inherits remote-API latency and provider downtime becomes write failure. Asynchronous without N6's worker means a thread inside `serve`. Decide in N1: writes enqueue (the `jobs` table already exists in `schema/001_init.sql`), drained by `embed --all` until N6 lands a worker. Specify: no-API-key must not fail the write; partial-batch failure semantics; re-embed on model change (`content_chunks.model` exists — use it); and idempotency, which `CLAUDE_HARD_AUDIT_PASS.md` already lists as an open capture-hash item.

**P1-7 — No abort criterion, and §6 contains a self-review loophole.** §3 says PASS → next, and nothing else. Undefined: what happens on a second FAIL, what "partial PASS" means, and who breaks a tie. §6's mitigation "Claude Code 审核调用不稳定 → 架构师代审并标注来源" lets the author approve their own work in a process whose entire value is external review. Fix: FAIL → one revision cycle → second FAIL escalates to **scope cut, not self-approval**; and any self-audit carries a `SELF-AUDITED — NOT INDEPENDENT` header and cannot close a node containing security or schema changes.

---

## P2 optional

- **Decide the alias policy once.** Upstream ships paired names: `get`/`get_page`, `list`/`list_pages`, `put`/`put_page`, `search`/`query`, `graph`/`traverse_graph`, `health`/`get_health`, `stats`/`get_stats`, `tags`/`get_tags`, `skills`/`list_skills`, `skill`/`get_skill`. Mirroring them helps agents whose prompts hardcode upstream names but roughly doubles the MCP tool list, and every client pays those tokens on every request. Recommend canonical-only + a documented alias map in the ledger.
- **README overstates parity.** "与 gbrain 同用法" (`README.md:23`) while the write default differs. After P0-2 resolves, add a short "Delta vs gbrain" section instead of implying identity.
- **`docs/nodes/` does not exist** though §3 rule 1 depends on it. Create it with an `N{k}-PLAN.md` template (goal / ledger rows moved / tests / acceptance assertions / rollback) so the auditor can mechanically diff plan → result.
- **Estimates 小/中/大 carry no unit.** A per-node test-count or ledger-row target makes §6's 永不完工 risk measurable instead of a feeling.
- **Keep Qbrain's finer-grained `think` gate and document it as intentional.** Upstream marks `think` `scope: 'write'` because it can save; Qbrain registers it read-scope with the save side-effect separately gated (`handlers.cpp:259-275`). That is better. Record it in the ledger as an intentional improvement so a future audit does not "fix" it toward upstream.
- **`purge_deleted_pages` is `localOnly` upstream** (`operations.ts:1468`) — one of only 8 such ops. When N2 implements 软删/purge, inherit that flag rather than rediscovering it.

---

## Node graph risk assessment

Critical path is `N1 → N2 (+N2.5) → N3`. Everything else is a leaf or near-leaf. Data-model decisions in N2 are the most expensive to reverse — migration cost grows with corpus size — and ranking changes in N3 are the ones users feel and the ones that regress silently.

| Node | Risk | Blast radius | Specific failure mode | Mitigation to add |
|------|------|--------------|----------------------|-------------------|
| N1 | **High** (P0-1, P0-2) | Every subsequent write | Ships on divergent schema; opens unmitigated remote write | P0-1 + P0-2 before opening |
| N2 | High | Corpus-wide, irreversible | Version/link contract wrong → later migration touches every page | Freeze contract in the plan, review before coding |
| N2.5 (new) | Medium | Read + write paths | Retrofitting the source axis after N3/N6 means revisiting both | Land before N3 |
| N3 | Medium-High | User-visible quality | Boosts regress ranking with no baseline to detect it (P1-4); backlink boost weaponizes P0-2 | Fixture + latency/quality baseline; P0-2 first |
| N4a | Low | Isolated | — | — |
| N4b | Medium | think only | Multi-round cost/latency unbounded | Per-call round and token caps |
| N5 | Medium | Ingest only | Watcher/live-sync loops or duplicate ingest; requirements unverified (P0-4) | Content-hash idempotency; complete the mirror first |
| N6 | **Highest** | Corpus-wide, unattended | First node that mutates the corpus with no human in the loop — a bad dream extraction writes wrong edges at scale. Also sized 大 against unread source (P0-4), and the most likely node to be quietly abandoned | `--dry-run`; per-phase write caps; every dream write tagged via `link_source`/provenance so it is **bulk-revertible**; state explicitly that N6 may be descoped rather than letting the project die here |
| N7 | High (security) | Network-exposed | Only listener in the plan; no threat model (P1-3) | P1-3 criteria |
| N8 | Low once N2.5 lands | Additive | — | — |
| N9 | Low | Additive | — | Pull forward as filler |
| N10 | Medium-High | Additive but large | tree-sitter grammar vendoring on MSVC is underestimated | Declare 1–2 languages up front |
| N11 | Medium | Release gate | Becomes a rubber stamp without the ledger (P0-3) | Ledger is the pass/fail instrument |

Two structural observations. **N6 is where this plan is most likely to fail** — largest, most dependencies, least-verified requirements, and the only node doing unsupervised corpus mutation. Decide now whether N6 is a v1 requirement or a v1.1 stretch; deferring it deliberately is a fine answer, discovering it in month three is not. **N7 is the only node that changes the security surface from "local process" to "network service"** — it deserves its own gate criteria, not the shared §4 checklist.

---

## Security (MCP default write)

Current shipped posture (verified in source): stdio MCP sets `ctx.remote = true` (`src/qbrain/mcp/server.cpp:127`); the registry denies `local_only` ops unless `--allow-write` or `QBRAIN_MCP_ALLOW_WRITE=1` (`src/qbrain/ops/registry.cpp:40-47`); `put_page` and `capture` are the two write ops, both `local_only` (`handlers.cpp:115,295`); `think --save` is separately gated (`handlers.cpp:259-275`). This is coherent and it is stricter than upstream.

Upstream posture, verified: `put_page` is `scope: 'write'`, **not** `localOnly`, dispatched with `remote: true` — writable over stdio MCP with no kill switch. Only 8 of 132 ops are `localOnly` (`purge_deleted_pages`, `sync_brain`, `file_list`, `file_upload`, `file_url`, `get_recent_transcripts`, `code_traversal_cache_clear`, `chronicle_backfill`). `capture` is not an operation at all.

So the plan's premise is half right, and the half that is right is the dangerous half. Upstream's default-write is not carelessness — it is a deliberate trade *paid for* with per-caller mitigations (P0-2). Qbrain currently has none of them. The specific chain, using upstream's own words: a remote caller writes a page containing `see meetings/board-q1` inside a code fence → Qbrain's unconditional link extraction (`handlers.cpp:107-108`) creates the edge → N3's backlink boost ranks the attacker's target higher for every future query. Upstream skips remote auto-link precisely to break this chain. N1 as written opens it; N3 arms it.

Aggravating factors: `source_id` is caller-controlled and unvalidated (`handlers.cpp:96`), so a remote write can target any source before D18 exists; and `QBRAIN_MCP_ALLOW_WRITE=1` (`commands.cpp:310-311`) means an inherited environment variable can silently enable writes — worth an audit-log line at minimum.

**Recommendation, in order of preference:**

1. **Keep default-deny.** Ship `--allow-write` as-is, document it as an intentional deviation, and say why in README. Cost is one flag in one install command — the README already shows both forms. This is the right call: Qbrain's threat model is a single-user Windows machine where the MCP client is an LLM agent processing untrusted content, and the plan's own §1.1 claims "Windows 一等", not "upstream-identical".
2. If default-allow is a hard requirement, it ships **in the same node** as: remote link-extraction skip, remote frontmatter override, `mcp:` provenance tagging, remote `source_id` allow-list, and an explicit decision on `capture` recorded as a non-parity extension.
3. Not acceptable: flipping the default in N1 and deferring mitigations to a later node. That leaves a known-exploitable window open across N2–N3, and N3 is the node that makes it matter.

Either way, N1 must add: an audit-log entry whenever a remote write executes, and `doctor` surfacing whether write is currently enabled and by which mechanism (flag vs env).

---

## Feasibility of "all gbrain features" definition

**Feasible as scoped; not feasible as literally worded — and the plan already says so, which is its best quality.**

The numbers: 132 upstream op names in 5,679 lines of `operations.ts`, plus engine (2,245), CLI (2,529), hybrid search (2,117) — against Qbrain's 3,384 LOC total and 9 ops. Upstream reports ~146k pages, 24k people, 66 crons in production (`01-ANALYSIS.md:24`). Qbrain's `doctor` warns above 5,000 embedded chunks. The gap is roughly 14× in op surface and ~30× in operating corpus.

§1.3's reformulation — "所有功能" = D1–D25 each reaching 可用 — is the right move, and it converts an impossible requirement into a tractable one. Three things must be true for it to hold up:

1. **The ledger must exist (P0-3).** Without enumeration, "可用" drifts to mean "something in this domain works", and the final audit cannot fail.
2. **可用 must be defined per domain, not globally.** One falsifiable assertion per D-row (e.g. D3: "a query with both lexical and semantic-only matches returns both, RRF-fused, with recency and backlink boosts observable in scored output"). Without this, §4's checkbox is a vibe.
3. **The plan must budget for the corpus gap.** P1-4. A feature set that is 可用 at 3,000 pages and unusable at 100,000 has not reached parity with a system that runs at 146,000, no matter how many D-rows are checked. Either commit to ANN in N3 or publish v1's ceiling and enforce it.

Domains where 可用 will be hardest to defend honestly: **D19 Skills** (43+ upstream — is 5 可用? the ledger must say), **D20 code-intel** (tree-sitter grammar vendoring, see N10), **D23 evaluation** (LongMemEval needs a harness and a labelled set, not just a script), **D15 Dream** (upstream's phases are unread — P0-4), and **D25 self-upgrade** (a self-updating Windows binary is its own security surface; consider declaring it out of scope with a reason rather than half-building it).

Verdict on the definition: **approve, conditional on P0-3 and P1-4.** It is honest, it is tractable, and §1.3 is written by someone who has seen this failure mode before.

---

## Pass criteria for N0 complete

N0 passes when all of the following are true and verifiable in the tree. Re-audit is a diff against this list.

**P0 remediation**

1. `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` bumped past v1.0.0-draft with a changelog naming each P0/P1 as adopted or rejected-with-reason. (Rejection is acceptable — silent omission is not.)
2. Schema bootstrap fixed (**P0-1**): canonical schema compiled in from `schema/001_init.sql`, divergent inline DDL deleted, both paths through `apply_migrations()`, `doctor` asserts the expected table/index/FK set and reports `DEGRADED` on mismatch, regression test opens a brain from an unrelated CWD and asserts the full index + FK set, and the repair path for fallback-created brains is documented. This is code, not plan text — N0 does not pass on a promise here.
3. §5 rewritten (**P0-2**): the incorrect 与 gbrain 一致 justification removed; `capture`-over-MCP recorded as a non-parity extension with a decision; either default-deny retained or the full mitigation set listed as N1 deliverables in the same node.
4. `docs/OPS-PARITY-LEDGER.md` exists (**P0-3**) with all 132 upstream op names classified, the 9 current ops marked `implemented`, and every `out-of-scope` carrying a reason. §3 amended so each node plan declares which rows it moves.
5. §1.4 evidence-provenance section added (**P0-4**), and N5 gated on completing the mirror for `minions/`, `ingestion/`, `search/rerank|autocut|adaptive-return`, `mcp/dispatch|tool-defs` — or on re-deriving those requirements from public docs, labelled as such.

**P1 remediation (or explicit written rejection)**

6. §3 carries a real DAG; N2.5 (source axis) inserted before N3; N4 split into N4a/N4b; N9 pinned to N1.
7. N7 threat model recorded, and §4 extended with the network-surface conditional gate.
8. Baseline fixture + search-latency baseline defined, with either ANN in N3's deliverables or a published v1 corpus ceiling enforced in `doctor`.
9. Superseded review docs marked `SUPERSEDED BY … ON …` — starting with `CLAUDE_MCP_HARD_AUDIT.md` M2's framing claim.
10. N1's auto-embed failure modes specified: enqueue-vs-sync decided, no-key behavior, partial-batch semantics, re-embed on model change, idempotency.
11. Node abort criterion defined, and §6's 架构师代审 path constrained by the `SELF-AUDITED — NOT INDEPENDENT` rule.

**Hygiene**

12. `docs/nodes/` created with an `N{k}-PLAN.md` template.
13. §1.2 gains a per-domain 验收断言 column.
14. `docs/nodes/N1-PLAN.md` written against the revised master plan, declaring its ledger rows, tests, acceptance assertions, and rollback.

**Standing gates unchanged and re-verified at N0**: no WSL/Docker dependency; no secrets in the repo; `qbrain_tests` green (currently 6/6); `doctor` clean on a freshly initialized brain — now including the new schema-integrity assertion.

Items 2, 4 and 14 are the ones that actually change outcomes: the first stops eleven nodes from being built on a broken foundation, the second makes the user's "所有功能" requirement signable, the third is the artifact the next audit reads.

---

## Conclusion

**PLAN_REVISE.** Good plan, and §1.3 shows the author understands the real risk. Four P0s: one live foundation bug in the advertised install path, one security regression built on a misread of upstream, one missing artifact that makes the headline requirement unfalsifiable, and one evidence-provenance gap that would quietly compromise four later audits. None requires rethinking the architecture. Remediate, re-audit against the 14 criteria above, then open N1.

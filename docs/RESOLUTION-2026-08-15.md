# Qbrain 整体复核三方决议 — 最终决议 (2026-08-15)

**程序**: Iron Law Article 5 三方决议（独立提案 → 匿名投票 → 获胜提案讨论定稿）
**参与方**: Claude Code (claude-opus-5/max) · Codex (gpt-5.6-sol/max) · OpenCode (grok-4.6/max)
**主持人**: ZCode 主会话（机械主持，不注入技术结论）
**授权**: 用户 2026-08-15 指令（逐字见 watchdog 分发记录与本决议附录前的 brief）

## 投票记录

| 参与方 | 投票 | 说明 |
|---|---|---|
| OpenCode | Proposal B | 格式合规 v20 |
| Codex | Proposal C | 有效（C 非己方） |
| Claude | ~~C~~ → B | 首投误投己方被判无效，控制器执法后在 A/B 中重投 B |

**结果**: Proposal B（2 票）胜出。无平票。

## 定稿采纳的修正案（Stage 3，均获 3/3 支持）

在获胜提案 B 的基础上，以下修正案并入最终方案：

- **AMD-1**（CAND-1，Codex 措辞）: N30 开始任何重叠实现前、以及 N30 标记 done 前，控制器必须对 N20/N21/N23 记录处置决定：或按其当前 approved 计划完成并获得新的 Claude Code outcome-audit PASS，或以新计划正式取代并获 Claude Code plan-audit PASS。记录须指明受影响文件、ledger 行、依赖与所选处置。
- **AMD-2**（CAND-2，Codex 措辞）: 建立分层证据矩阵：Tier 1 = 当前计划、哈希绑定、Claude Code PASS 的证据，可满足节点门；Tier 2 = 回溯性证据，仅作历史背景，且仅在控制器明确等价性决定后可用于文档；Tier 3 = stub/矛盾/过期/计划不匹配证据，必须纠正性关闭。对 N21、N23、N29 记录明确处置；不得静默接受亦不得反复重审有效的当前证据。
- **AMD-3**（CAND-3，Codex 措辞）: 所有触及存储 schema/迁移、身份、授权、registry 策略或共享热文件的变更必须串行化。N35 的 adapter/迁移变更及其全套测试须合并且全绿后，N36 方可编辑共享授权代码；仅当 approved 计划包含证明不相交的文件所有权矩阵且父代理记录该决定时才允许并行。
- **AMD-4**（CAND-4）: 93 个修改 + 94 个未跟踪文件在任何新节点开始前，必须获得哈希清单、分类、密钥检查与干净构建证据。
- **AMD-5**（CAND-5，Codex 措辞）: docs/08 修订须覆盖 N1-N29 与 N30-N37（或正式取代的节点 ID），含当前状态、审计哈希、权威生成的操作/测试清单与全部显式 deferral；完成文档与 ops 台账从同一来源同步。
- **AMD-6**（CAND-6）: 一切文档中的测试计数必须由当前注册的可执行套件与记录的原生运行生成；散文不得携带未验证的历史计数。
- **AMD-7**（CAND-7，Claude 措辞）: N30 治理调和必须以当前标准（哈希绑定、断言表、快照矩阵）对照各自的 approved 计划审计 N24-N28；任何当前审计为 <10 行 stub 的节点，在 N30 标记 done 前必须获得全新的全标准审计或明确的 deferral 决定。
- **AMD-8**（CAND-8）: 严格节点身份/门绑定：任何节点的人工制品不得满足另一节点的 outcome 门；每个节点只针对其自己的当前 approved 计划审计。
- **AMD-9**（CAND-9）: 脏树不得盲目提交。验证 CMake 与直连 MSVC 双路径干净构建、分类完整变更集、仅提交被接受且有证据的切片。
- **AMD-10**（CAND-10）: 中央 scope 执行、远程路径披露、精确 HTTP 路由、安全端口解析、CMake store.cpp 链接、schema 完整性、崩溃原子 pack 写入、构建脚本可复现性等发现为 N30 强制验收门，非可选加固。
- **AMD-11**（CAND-11）: 修订后的计划必须为 AST 代码智能、多模态摄取/搜索、有界 minion 扇出、存储后端边界、认证远程访问、打包/CI、最终项目审计定义可证伪验收标准或显式 deferral。未定义的未来工作不得与 project-complete 声明共存。

## 少数意见（记录，未采纳为独立条款；实质多被上述修正覆盖）

- Codex NEW-1: 纠正节点须捕获已审计的实施前时间基线（计划/审计哈希、范围文件清单、交付物缺席证明、二进制/构建身份、隔离数据根证明；既有未跟踪文件不得计为实施交付物）。实质大部分被 AMD-4/AMD-9 覆盖；其“时间基线”要求并入 N30 计划的实施前门。
- Claude NEW-1: 台账中每个 implemented 操作须映射到至少一个行使主路径的注册测试；N37 终审须验证该映射由工具提取而非散文断言。实质并入 AMD-6/AMD-11 的 N31/N37 验收。
- Claude NEW-2: N30 须产出 N1-N29 哈希绑定调和矩阵。实质并入 AMD-1/AMD-2/AMD-5。

## 获胜提案全文（作为最终方案基座）

# 1. Current-State Audit

**Evidence basis.** I read the immutable brief and inspected `D:\Projects\Qbrain` read-only. No file was modified, created, deleted, or renamed. No build, test execution, commit, push, or model-configuration change was performed in this proposal stage. The codebase-memory graph reports 4,905 nodes and 15,673 edges for project `Qbrain`; that index is inspection data, not implementation evidence.

**Repository and provenance.** The repository is on `main` at commit `5ced8ccb511672536d0f9767a2bc1777baf561ab`, and `origin/main` points to the same commit. The working tree is not release-grade: 93 tracked files are modified, 94 files are untracked, and the tracked diff is approximately +12,872/−2,480 lines. The changes span production C++, headers, tests, PowerShell scripts, node plans, audits, and evidence. The current state is therefore a large uncommitted changeset whose provenance, completeness, build status, and acceptance cannot be treated as one verified release.

**Product and documentation drift.**

- `docs/08-MASTER-PLAN-GBRAIN-PARITY.md` still describes version 1.3.0, dated 2026-07-28, as COMPLETE through N1-N11, with 104 operations and 18/18 tests. Its DAG stops at N11.
- The repository contains later N12-N29 plans, implementation, tests, and evidence. `tests/test_main.cpp` currently registers 29 tests, and later evidence files such as `docs/nodes/n22-evidence/TEST-BUILD-OUTPUT.txt` report an expected registered count of 29.
- `docs/09-PROJECT-COMPLETION.md` still reports historical 18/18 test results and older wave counts. Those statements are not a current acceptance record.
- `docs/OPS-PARITY-LEDGER.md` reports 104 implemented operations but also acknowledges that many are heuristic stubs rather than full gbrain/Postgres/LLM/tree-sitter parity. The count, implementation depth, and evidence need to be reconciled rather than repeated as a blanket completion claim.
- Existing binaries under `build\cl` and historical evidence logs were not executed during this review. Their timestamps and text are not fresh runtime proof.

**Node-gate integrity is contradictory.**

- `docs/nodes/N20-PLAN.md` is `approved` with outcome audit `pending`, while `docs/nodes/N20-HARD-AUDIT.md` is an older retrospective PASS that does not audit the current revised plan.
- `docs/nodes/N21-PLAN.md` is `draft` with both audits pending, while older N21 artifacts and retrospective claims report PASS.
- `docs/nodes/N23-PLAN.md` is `approved` with outcome audit pending, while `N23-PLAN-AUDIT.md` describes a different draft state and `N23-HARD-AUDIT.md` is an older retrospective audit.
- `docs/nodes/N2-PLAN.md` says `done` while declaring its outcome audit BLOCKED; `N2-HARD-AUDIT.md` says PASS.
- `docs/nodes/PLAN-AUDIT-BATCH-RESULT.json` contains stale results that no longer describe the current node plans.

No node may be considered complete while its status, plan audit, outcome audit, implementation evidence, and current plan hash disagree.

**Spot-audit of code risks.**

| Priority | Evidence | Consequence |
|---|---|---|
| P0 | `include/qbrain/ops/registry.hpp` defines Read/Write/Admin scopes, but `src/qbrain/ops/registry.cpp` enforces only a `local_only`/remote condition and does not centrally enforce `scope`. Several Write registrations in `src/qbrain/ops/handlers.cpp` use `local_only=false`. | Remote Write/Admin operations can bypass the intended default-deny policy when `--allow-write` is absent. |
| P0 | `src/qbrain/ops/handlers.cpp` exposes health and file information; `get_health` passes `true` to `health_report_json`, `file_list` returns stored paths, and `file_url` produces `file:///` URLs. | Remote callers can learn local database or filesystem paths. |
| P1 | `src/qbrain/mcp/http_server.cpp` uses substring tests such as `req.find("POST /ingest")` and broad `req.find("POST")` checks. | Paths such as `/ingestx` or malformed request prefixes can be misclassified. |
| P1 | `src/qbrain/cli/commands.cpp` uses uncaught `std::stoi` for `--port`. | Invalid or out-of-range port input can terminate the command instead of returning a controlled error. |
| P1 | `CMakeLists.txt` does not define a files library or include `src/qbrain/files/store.cpp`, while `scripts/build-cl.ps1` compiles and links `store.cpp`. | Direct-script builds and documented CMake builds can describe different products and can fail independently. |
| P1 | `src/qbrain/storage/migrate.cpp` checks only an older subset of tables and indexes. Newer tables such as `page_versions`, `facts`, `takes`, `file_index`, and `raw_data` are omitted. | A damaged database can report schema OK while newer operations fail. |
| P1 | `src/qbrain/schema/packs.cpp` backs up a pack and then truncates/writes the live file directly. | A partial write can leave the active schema pack corrupted without automatic restoration. |
| P1 | `scripts/build-cl.ps1` and `scripts/build-tests-cl.ps1` hardcode the repository, Visual Studio, and temporary paths; the test-source parameter is ignored and stale objects can remain in the link set. | Builds are not reproducible or reliably clean. |
| P2 | `register_builtin_ops` in `src/qbrain/ops/handlers.cpp` is approximately 2,152 lines with very high measured complexity. | Security policy, schemas, and registration behavior are difficult to audit and easy to drift. |

The project has substantial capability work, but the current evidence supports “feature-rich, partially verified development tree,” not “completed and release-proven project.”

# 2. Problems and Required Fixes

The following fixes must precede new feature expansion. They are proposed work only; they are not claimed as completed here.

**P0: Establish provenance and reconcile the node ledger.**

1. Freeze the current tree and generate a complete manifest of every tracked modification and untracked file, including path, size, hash, and category.
2. Classify each item as accepted product work, required test/evidence, documentation correction, duplicate/stale artifact, or rejected work. Do not delete or overwrite user work merely to make the tree appear clean.
3. Verify secrets are absent from the entire proposed changeset.
4. Decide the disposition of N20, N21, and N23 before touching overlapping files. Each must either complete its own current plan loop with a fresh Claude Code audit or be explicitly superseded by a controller decision and a new audited plan. Historical PASS artifacts cannot silently approve a different plan.
5. Split the accepted work into coherent commits only after native builds and tests establish what is actually viable.

**P0: Restore the governance contract.**

- Reconcile every node from N1 through N29 into a single matrix containing current plan hash, status, plan-audit hash/verdict, implementation evidence, outcome-audit hash/verdict, and ledger rows.
- Retire or label stale retrospective audits instead of allowing them to satisfy current plans.
- Update `docs/08-MASTER-PLAN-GBRAIN-PARITY.md`, `docs/09-PROJECT-COMPLETION.md`, and `docs/OPS-PARITY-LEDGER.md` from one authoritative inventory.
- A plan may be `done` only after its own current plan audit is PASS, implementation evidence exists, native tests pass, and its own current outcome hard audit is PASS.

**P0: Close remote authorization and disclosure gaps.**

- Make operation authorization derive from one central policy using operation scope, local-only status, request locality, authenticated identity, brain, and source scope.
- Deny every Write/Admin operation remotely unless the explicitly documented capability is present. `--allow-write` must not become a substitute for identity, tenant, brain, or source authorization.
- Add a complete negative matrix for every Write/Admin operation, including `takes_calibration`, `file_upload`, `submit_agent`, `put_raw_data`, and `schema_apply_mutations`.
- Make health and file responses local-only or redact absolute paths, database paths, and `file:///` URLs for remote callers.

**P1: Make build and request handling deterministic.**

- Add `src/qbrain/files/store.cpp` to the CMake target graph and verify both CMake and direct-MSVC builds from clean output directories.
- Derive paths from `$PSScriptRoot`, discover MSVC robustly, use the system temporary directory, honor `-TestSources`, and link only objects generated by the current invocation.
- Replace HTTP substring routing with an exact request-line parser for method and path. Add negative tests for `/ingestx`, malformed methods, duplicate headers, invalid content lengths, and unsupported paths.
- Parse and range-check CLI numeric options without uncaught exceptions.

**P1: Repair storage and mutation integrity.**

- Expand schema health checks to all required tables, indexes, columns, constraints, and FTS objects for the current schema version.
- Add corruption fixtures that remove each required object and verify `doctor` fails closed.
- Make schema-pack mutation write a bounded temporary file in the same directory, flush and close it, atomically replace the target, and preserve the original on every failure path.

**P2: Reduce maintainability and evidence drift.**

- Split operation registration by domain and produce one inventory consumed by the registry, tests, and ledger.
- Classify recurring non-failing `live_sync` warnings and ensure invalid sources/directories are either rejected intentionally or represented as explicit test fixtures.
- Replace prose test counts with generated counts from the registered suite and recorded command output.

# 3. Revised Overall Development Plan (Phase/Node DAG)

The revised plan is a security-and-evidence-first sequence:

```text
N30: baseline reconciliation, security, build, storage closure
  |
N31: registry/MCP contract closure and registration decomposition
  |
  +--> N32: AST code intelligence
  +--> N33: real multimodal ingestion/search
  +--> N34: bounded parent-child minion hierarchy
  +--> N35: storage adapter and optional backend boundary
  +--> N36: authenticated remote/multi-tenant profile
              |
              +--> N37: packaging, CI, complete audit, and release
```

N32, N33, and N34 are the cleanest parallel seam after N31 because they can be isolated by module ownership. N35 and N36 may proceed in parallel only when their schema, identity, and authorization contracts do not overlap; migrations and shared authorization code must be serialized. N37 is last.

The unresolved historical N20/N21/N23 work is a preflight input to N30, not an excuse to run overlapping implementations concurrently.

Every node follows the same ordered loop:

1. The controller writes `N{k}-PLAN.md` with status `draft`, goals, ledger rows, falsifiable acceptance, tests, rollback, security, and dependencies.
2. Claude Code audits that exact plan. P0/P1 failures block approval.
3. The controller decides which audit recommendations to adopt, records the decision, updates the plan, and changes it to `approved` only after a PASS.
4. Implementation begins only after approval, with parallel subagents limited to disjoint, explicitly owned slices.
5. Native Windows builds and tests run before outcome review.
6. Claude Code performs the current node’s hard audit against the approved plan. Only PASS permits `done`, ledger reconciliation, and the authorized push.
7. A node’s audit cannot be replaced by another node’s audit, a retrospective artifact, or a prose evidence claim.

# 4. Next-Phase Node Definitions

## N30 — Baseline Reconciliation and Security/Build Closure

**Goal:** Establish one trustworthy baseline, close the immediately exploitable authorization and disclosure defects, and make both supported build paths reproducible.

**Falsifiable acceptance:** A hashed worktree manifest records a disposition for every changed/untracked file; all node statuses and documents agree; every Write/Admin operation is denied remotely without explicit authorization; `/ingestx` is not routed as `/ingest`; malformed ports return controlled errors; remote health/file responses contain no local paths; schema checks detect removal of every required current table/index/column; failed pack mutation leaves the target hash unchanged; clean CMake and direct-MSVC builds produce working production and test binaries from identical source manifests.

**Tests:** Full registered suite; MCP authorization matrix; HTTP socket negative tests; CLI argument tests; corrupted-schema fixtures; atomic-write failure tests; clean-build and script smoke tests.

**Rollback:** Isolate documentation and source fixes in reversible commits; take no destructive migration; restore the original pack from its verified backup if a replacement fails; revert only the N30 commit set if the baseline cannot be made coherent.

**Security:** Central default-deny policy, path redaction, source/brain scope checks, no secrets in evidence, bounded request bodies, and explicit proof that temporary test roots do not touch `%LOCALAPPDATA%\Qbrain`.

## N31 — Registry/MCP Contract Closure and Decomposition

**Goal:** Make the operation registry authoritative, auditable, and domain-partitioned without changing operation names accidentally.

**Falsifiable acceptance:** A generated or validated inventory has one row per public operation; registry count, inventory count, tests, and ledger agree; every operation declares scope and locality; unknown fields, wrong types, duplicate registrations, invalid source/brain references, and unauthorized remote calls fail consistently; splitting registration produces no intentional behavior regression.

**Tests:** Golden inventory comparison, schema fuzz/negative tests, duplicate-name tests, source/brain isolation tests, remote Read/Write/Admin matrix, and operation-level regression tests.

**Rollback:** Keep the existing registration path available until the new inventory passes equivalence tests; revert domain splits as whole commits if inventory or behavior diverges.

**Security:** No operation may inherit authorization from a caller default. Admin operations require an explicit capability stronger than ordinary read access, and operation schemas must reject ambiguous or extra input.

## N32 — AST Code Intelligence

**Goal:** Replace regex-only code intelligence with a bounded, Windows-compatible parser implementation, initially for C++ and TypeScript, while retaining a clearly labeled fallback.

**Falsifiable acceptance:** Definitions, references, calls, flow, and blast results are correct on committed C++/TypeScript fixtures; malformed files return bounded diagnostics; results are deterministic; resource limits prevent hangs or unbounded memory growth; fallback results are explicitly marked heuristic and never reported as AST certainty.

**Tests:** Golden AST fixtures, malformed and partial-source tests, deterministic repeated runs, timeout/memory-bound tests, path-isolation tests, and parity comparisons between parser and fallback modes.

**Rollback:** Keep the existing scanner behind an explicit fallback flag and revert parser integration without changing stored source data.

**Security:** Restrict parsing to authorized workspace roots, treat source as untrusted input, cap file size and recursion, and do not execute parsed content.

## N33 — Real Multimodal Ingestion and Search

**Goal:** Replace filename-only image heuristics with actual metadata/decode support and an optional embedding-provider contract.

**Falsifiable acceptance:** Supported MIME types are identified from content, dimensions and metadata are recorded within limits, unsupported or oversized files fail predictably, a deterministic mock provider proves embedding/search behavior, and missing credentials fail open without blocking ordinary text search.

**Tests:** PNG/JPEG and malformed-file fixtures, MIME spoofing, size/timeouts, deterministic mock embeddings, provider failure, index rebuild, and disclosure-boundary tests.

**Rollback:** Retain the existing text-only path and disable the provider integration through configuration without invalidating stored text data.

**Security:** Enforce MIME, byte, pixel, timeout, and concurrency limits; never expose raw local paths or provider credentials; isolate provider failures from the core database.

## N34 — Bounded Parent-Child Minion Hierarchy

**Goal:** Implement real parent-child orchestration with bounded fan-out, aggregation, cancellation, retries, and transactional state transitions.

**Falsifiable acceptance:** Parent and child jobs have explicit states and ownership; fan-out, queue depth, runtime, and payload sizes are bounded; cancellation propagates; retries are idempotent; partial failure produces a deterministic aggregate result; concurrent workers cannot double-complete a child.

**Tests:** Race and stress tests, cancellation tests, retry/idempotence tests, rollback tests, resource-limit tests, crash/reopen recovery, and aggregate-result determinism.

**Rollback:** Preserve the current single-level job path behind a feature gate; do not rewrite historical job state in place until migration and recovery tests pass.

**Security:** Enforce per-brain and per-source ownership, prevent recursive fan-out abuse, cap prompt/result sizes, and ensure child jobs cannot escalate operation scope.

## N35 — Storage Adapter and Backend Boundary

**Goal:** Define a stable storage contract while retaining SQLite as the Windows default; add a PostgreSQL-class backend only if product scope and operational requirements justify it.

**Falsifiable acceptance:** The adapter contract covers migrations, transactions, locking, indexes, FTS/vector behavior, and error semantics; SQLite passes the contract suite; any optional backend passes the same migration and integration suite before being listed as implemented. No ledger row claims backend parity from an interface stub alone.

**Tests:** Migration/idempotence/rollback tests, transaction isolation, concurrent job claims, index/FTS behavior, backup/restore, and backend integration tests with explicit environment gating.

**Rollback:** Keep SQLite as the default and make any new backend opt-in; revert adapter wiring without changing the on-disk SQLite schema.

**Security:** Use parameterized queries, environment-provided credentials, least-privilege database roles, bounded connection pools, and no secrets in logs or repository files.

## N36 — Authenticated Remote and Multi-Tenant Profile

**Goal:** Make a deliberate product decision about authenticated remote access. Implement OAuth/token-scoped tenancy if it is in scope; otherwise document it as a deferral and keep remote writes disabled.

**Falsifiable acceptance:** If implemented, tokens are validated, scopes map to operations, and tenant, brain, source, file, and job isolation is proven with cross-tenant negative tests. If deferred, the master plan and ledger explicitly mark it out of scope, remote Write/Admin remain denied, and no “multi-tenant” capability is claimed.

**Tests:** Token expiry/revocation, scope escalation, tenant crossover, brain/source crossover, replay, malformed token, loopback/TLS boundary, and secret-redaction tests.

**Rollback:** Preserve local stdio operation and loopback read-only behavior; disable remote authenticated mode without affecting local data.

**Security:** `--allow-write` is never treated as identity; require secure token handling, TLS or an explicitly bounded loopback-only mode, secret rotation, audit logging without token material, and strict tenant filters.

## N37 — Packaging, CI, and Final Project Audit

**Goal:** Produce a reproducible Windows release and close the project against the revised master plan.

**Falsifiable acceptance:** A clean Windows 11 MSVC C++20 checkout builds through CMake and direct PowerShell paths; package contents and version are deterministic; data-root behavior is documented and tested; the complete registered suite passes repeatedly; smoke tests cover CLI, stdio MCP, HTTP MCP, storage recovery, authorization, and path redaction; the master plan, node plans, audits, ledger, and completion document agree; the final Claude Code project-level hard audit returns PASS.

**Tests:** Clean checkout build, repeated build/hash comparison, install/package smoke tests, fresh temporary data-root tests, full unit suite, MCP negative matrix, and release artifact validation.

**Rollback:** Keep the previous known-good package and commit available; publish a new release only after the final audit; do not replace the default branch with an unaudited artifact.

**Security:** Verify package contents for secrets, default-deny remote writes, least-privilege runtime behavior, reproducible dependency inventory, and documented incident rollback.

# 5. Completion Bar and Exit Criteria

The project is complete only when all of the following are evidenced, not merely stated:

- The large dirty changeset has an explicit disposition and is represented by coherent, reviewable commits in a clean worktree.
- The revised master plan includes every active node and every declared deferral, including the exact scope of tree-sitter/AST, multimodal, OAuth/multi-tenant, backend, and minion work.
- Every active node has its own current plan, a Claude Code plan-audit PASS, implementation evidence, native tests, and a Claude Code outcome hard-audit PASS.
- No plan says `done` while an audit is pending, BLOCKED, stale, retrospective-only, or bound to a different plan hash.
- The operation registry, generated inventory, tests, and `docs/OPS-PARITY-LEDGER.md` agree exactly. “Implemented” rows are limited to behavior proven by tests; heuristic or deferred capabilities are labeled honestly.
- Clean Windows 11 MSVC C++20 production and test builds pass through both CMake and the direct PowerShell path without stale objects or machine-specific absolute paths.
- The registered suite passes repeatedly. The current 29-test registration baseline is reconciled from `tests/test_main.cpp`; later tests may increase the count, but no prose count may replace the executable result.
- The MCP matrix proves remote default-deny for every Write/Admin operation, source/brain isolation, malformed-input rejection, path redaction, transaction rollback, and no-secret behavior.
- Schema health detects all required current objects, schema-pack mutation is crash-atomic, and storage recovery is demonstrated on fresh and corrupted temporary databases.
- Every known gbrain capability gap is either implemented and tested or explicitly deferred with rationale, owner, and no misleading ledger claim.
- A final Claude Code hard audit against the revised project plan passes before the repository is declared complete or pushed.

# 6. Risks and Tradeoffs

- **Dirty-tree provenance risk:** Splitting or rejecting work can lose context or duplicate effort. Mitigation is a hashed manifest, explicit disposition, and reversible commits before cleanup.
- **Schedule versus hardening:** N30 and N31 delay new feature work, but continuing on contradictory gates would multiply unsupported claims and make later audits unreliable.
- **Capability versus depth:** Keeping 104 operation names is useful only when each operation’s actual behavior is clear. A smaller set of deeply tested operations is preferable to a larger set of misleading parity claims.
- **Parallel development conflicts:** N32-N34 can be parallelized, but shared schema, registry, authorization, and test-registration files create race risk. Ownership manifests and serialized integration are required.
- **External dependency risk:** Tree-sitter, embedding providers, OAuth, and PostgreSQL add Windows packaging, credentials, and operational complexity. Each must remain optional until a complete acceptance matrix exists.
- **Backward compatibility risk:** Expanding schema checks or changing authorization can expose previously tolerated states. Preserve migration backups, provide controlled error responses, and test existing local workflows before tightening behavior.
- **Test-evidence drift:** The suite count has already changed from 18 to 26 to 29 across evidence waves. Generate counts and hashes from the current executable so documentation cannot silently become stale.
- **Security/usability tradeoff:** Remote convenience must remain subordinate to tenant, brain, source, and path isolation. Local stdio can remain ergonomic while remote interfaces stay conservative by default.
- **Windows-only toolchain risk:** Hardcoded Visual Studio and temporary paths make another Windows host behave differently. Discovery, clean output roots, and repeated clean-build verification are necessary.

# 7. Recommendation

Adopt N30 as the immediate next node and treat it as a blocking baseline rather than a feature wave. Do not mark the current repository complete and do not push the current uncommitted tree. First freeze and classify the changeset, reconcile N20/N21/N23 and all historical audit artifacts, then obtain the required Claude Code plan-audit PASS for N30.

After N30 passes its own outcome audit, proceed to N31. Once the registry and authorization contracts are stable, run N32, N33, and N34 in parallel where ownership is genuinely disjoint; schedule N35 and N36 only with explicit backend and identity decisions; finish with N37 and the project-level hard audit. Keep every deferral explicit, preserve the Windows-native C++20/MSVC boundary, and require evidence-backed ledger claims throughout.

## 生效

本决议为最终三方决议产物。控制器据此修订 docs/08-MASTER-PLAN-GBRAIN-PARITY.md 至 v2.0.0，并按节点环（PLAN → Claude 计划审计 → 实现 → Claude 硬审计 → 推送）从 N30 开始执行。

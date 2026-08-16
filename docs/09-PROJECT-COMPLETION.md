# Qbrain Project Completion Status

**Date**: 2026-07-28 (Phase 1 closeout; current status governed by master plan v2.0.0)
**Platform**: Windows 11 native C++20  

## Completion definition (agreed)

1. **Master plan D1–D25 “usable” gates** — achieved through N11 quality closeout PASS.
2. **Capability parity with gbrain**, not 1:1 100+ ops — ledger marks intentional `out-of-scope-v1`.
3. **Node loop**: PLAN → implement → hard audit → next. Claude Code is authoritative when reachable.

## What ships

| Area | Status |
|------|--------|
| Pages CRUD / versions / tags / links | done |
| Hybrid search + RRF + rerank fail-open | done (N12) |
| Think + AI chat/embed gateways | done |
| MCP stdio NDJSON + HTTP loopback token | done |
| Jobs minions claim/complete/retry | done (N12–N13) |
| Multi-phase dream | done (N12) |
| Live-sync notes dir (mtime state) | done (N13) |
| Sources add/list/remove/status | done (N13) |
| Skills list/get | done (N9) |
| Facts / trajectory / forget | done |
| Multi-brain dirs | done |
| Unit tests | 31/31 PASS (N30 two-round, both build paths; generated 2026-08-15) |

## Explicitly deferred / bounded parity

Full tree-sitter parity, full multimodal search, OAuth remote multi-tenant MCP, PGLite/Postgres engine, and full parent-child minion fan-out remain bounded or heuristic unless a later audited node tightens them. Ledger rows marked implemented may still be usable/stub-level where noted.

## Process

- N12 Claude hard audit: **PASS**
- N13 Claude hard audit: **PASS** (`docs/nodes/N13-HARD-AUDIT.md`, re-audit 2026-07-26)
- Residual P1/P2 from N13: sources_remove runtime smoke, forget_fact HTTP round-trip (non-blocking)
- N11 plan audit: **PASS** (`docs/nodes/N11-PLAN-AUDIT.md`); N11 hard audit: **PASS** (`docs/nodes/N11-HARD-AUDIT.md`).

## Binary

`D:\Projects\Qbrain\build\cl\qbrain.exe`  
MCP: `qbrain serve --allow-write`

## Wave N14–N16 (2026-07-26)

| Node | Focus | Audit |
|------|-------|-------|
| N14 | pause/resume/progress, status snapshot, doctor remediate | PASS |
| N15 | ingest_log v7, chronicle, link sources, timeline | PASS |
| N16 | code_def/refs/callers (regex) | PASS |

- Registered MCP ops: **59**
- Unit tests: **10/10** PASS
- Schema: **v7**
- Binary: `build\cl\qbrain.exe`


## Wave N26–N28 (2026-07-27)

| Node | Focus | Audit |
|------|-------|-------|
| N26 | agent/advisor/onboard/skillopt | PASS |
| N27 | raw_data v11, transcripts, salience, image stub | PASS |
| N28 | schema_apply_mutations | PASS |

**Ledger**: all previously out-of-scope upstream ops marked implemented at usable/stub level.
Unit tests: **15/15** PASS. Schema **v11**.

## Wave N1-N11 Retrofit (2026-07-28)

| Node | Focus | Audit |
|------|-------|-------|
| N1-N10 | v1 parity retrofit through facts/trajectory | PASS |
| N11 | doctor/tests/docs/ledger closeout | PASS |

- Registered MCP/CLI ops in ledger: **104**
- Unit tests: **18/18** PASS via `scripts\build-tests-cl.ps1`
- Build binary: `build\cl\qbrain.exe`

## Phase 2 closeout (2026-08-16)

Phase 2（2026-08-15 三方决议序列，master plan v2.0.0）收官。治理规则：分层证据
（Tier-1/2/3）、节点环（draft PLAN → Claude Code plan-audit PASS → approved →
实现 → 原生构建+测试 → Claude Code outcome hard-audit PASS → done）。逐节点收官
状态（逐文件核对，索引见 `docs/nodes/n37-evidence/GOVERNANCE-INDEX.md`）：

| Node | Focus | Plan audit | Outcome hard audit |
|------|-------|-----------|--------------------|
| N30 | 基线调和、安全闭合（中央默认拒绝/路径脱敏）、双路径干净构建、N20/N21/N23/N24-N28 处置 | PASS | PASS |
| N31 | registry/MCP 契约闭合、注册分解、清单/台账/测试四方一致（108 ops，104 上游 + 4 扩展） | PASS (round 3) | PASS |
| N32 | AST 代码智能（astlite C++/TS 有界解析器，显式 heuristic fallback） | PASS (round 2) | PASS |
| N33 | 真实多模态摄取/搜索（MIME/元数据/解码；凭据缺失 fail-open） | PASS (round 2) | PASS |
| N34 | 有界父子 minion 层级（扇出/聚合/取消/事务状态；schema v13） | PASS | PASS |
| N35 | 存储适配器与后端边界（存储契约套件；SQLite 唯一后端，PG/vector 契约显式 deferral） | PASS (round 2) | PASS |
| N36 | 认证远程/多租户画像（token 范围 read/write/admin，constant-time 校验；TLS/OAuth/多租户显式 deferral） | PASS (round 2) | PASS |
| N37 | 打包（zip+MANIFEST sha256 可复现）、CI 骨架、版本 2.0.0、数据根文档+测试、五类冒烟、文档终调、secrets 终扫 | PASS (round 2) | **pending**（节点收尾门 + 项目级终审） |

- **Suite**: 注册 39（38 + `n37_packaging`；版本常量 + 数据根解析/敌意 id 拒绝
  断言）。39/39 已在独立私有树副本（`%TEMP%\n37b-verify`，N37 子代理 B）经
  `scripts\build-tests-cl.ps1` 全新构建验证全绿（2026-08-16，含
  `[PASS] n37_packaging`）；正式双路径两轮 FINAL-VERIFY 证据 =
  `docs/nodes/n37-evidence/FINAL-VERIFY-*`（N37-AA/父代理收官门生成）。
- **数据根文档**: `docs/03-BUILD-WINDOWS.md` §数据根（`%LOCALAPPDATA%\Qbrain\
  brains\<id>\brain.db` 结构与 brain id 拒绝语义）。
- **安全**: 全仓 secrets 终扫（模式集与逐命中评估见
  `docs/nodes/n37-evidence/SECRETS-SCAN.txt`）。
- **Phase-1 历史保留**: 上文各历史波次（N14-N16、N26-N28、N1-N11 retrofit 等）
  记录原样保留；Phase-1 证据的分层定性（Tier-2 回溯/Tier-3 stub）以
  `docs/nodes/n30-evidence/NODE-RECONCILIATION-MATRIX.json` 为准。

# Qbrain ↔ gbrain 全功能对齐 — 整体规划（Master Plan）

**版本**: 2.0.0
**日期**: 2026-08-15
**状态**: **REVISED — Phase 2 COMPLETE (2026-08-16)**（依据 2026-08-15 三方决议，见 `docs/RESOLUTION-2026-08-15.md`；N30-N37 全部完成，收官记录见 `docs/09-PROJECT-COMPLETION.md` 与 `docs/nodes/n37-evidence/GOVERNANCE-INDEX.md`）
**目标**: 纯 Windows 11 原生 C++ 下，Qbrain 达到 gbrain **能力对等**（非位级移植），且**每一项声明都有可验证证据**。

---

## Changelog

| Ver | Change |
|-----|--------|
| 1.0.0-draft | Initial plan |
| 1.1.0 | Adopt N0 audit P0s: embedded schema, ops ledger, write-default-deny retained, evidence §1.4, node DAG (N2.5, N4a/N4b), abort rule, N6 stretch option |
| 1.3.0 | Wave 1 closeout: N1-N11 audits PASS; ledger 104 ops（历史状态，后经 N12-N29 波次扩展） |
| 2.0.0 | 三方决议采纳：Phase 2（N30-N37）安全与证据优先序列；分层证据政策；串行化规则；生成式计数；完整修正案 AMD-1..AMD-11 见决议文档 |

---

## 0. 当前基线（2026-08-15 决议确认）

- 代码：`main` @ `5ced8cc`；17 模块、104 台账 ops 全 implemented、schema v12、测试注册基线 29（N22 冻结双跑 29/29）。
- **已知治理债（Phase 2 必须先清偿）**：
  1. 工作树存在 93 修改 + 94 未跟踪文件（+12872/−2480 行）未提交变更集，出处/完整性/构建状态未证。
  2. 节点门矛盾：N20-PLAN approved 但 outcome audit 为 plan 前 8 天的 5 行 stub；N21-PLAN draft 但历史声称 PASS；N23-PLAN approved 但 outcome pending；N2-PLAN done 与 BLOCKED 声明并存；PLAN-AUDIT-BATCH-RESULT.json 过期。
  3. N24-N28 审计为 2-10 行 stub，低于 N16-N22 已确立的当前标准。
  4. 文档计数漂移（18/18 vs 实际 29）。
- **已知代码缺陷（Phase 2 N30 强制门）**：
  - P0: registry 未中央执行 scope（远程 Write/Admin 可绕过默认拒绝）；get_health/file_list/file_url 向远程暴露本地路径。
  - P1: HTTP 子串路由（`/ingestx` 误判）；`--port` 未捕获 stoi；CMake 缺 `files/store.cpp`；migrate.cpp schema 检查缺新表；pack 写入非原子；构建脚本硬编码路径/陈旧对象。

## 1. 分层证据政策（AMD-2，全局硬规则）

| Tier | 定义 | 效力 |
|------|------|------|
| 1 | 当前计划哈希绑定 + Claude Code 当前 PASS | 可满足节点门 |
| 2 | 回溯性证据（历史 PASS、旧审计） | 仅历史背景；经控制器显式等价性决定后方可支撑文档 |
| 3 | stub / 矛盾 / 过期 / 计划不匹配 | 必须纠正性关闭 |

节点状态唯一合法组合：`done` ⇔ 自身当前计划 plan-audit PASS + 实现证据 + 原生测试 + 自身当前 outcome hard-audit PASS（AMD-8：任何节点的制品不得满足另一节点的门）。

## 2. Phase 1（历史，冻结）

D1-D25 能力域与 N0-N29 波次记录见 git 历史（v1.3.0 及 `docs/09`、`docs/OPS-PARITY-LEDGER.md`）。Phase 1 的实现是 Phase 2 的调和对象而非完成的最终态；104 ops 的深度注记（usable/heuristic stub）保留。

## 3. Phase 2 节点 DAG（决议采纳）

```
N30: 基线调和 + 安全/构建/存储闭合（含 N20/N21/N23 处置、N24-N28 stub 审计纠正）
  |
N31: registry/MCP 契约闭合与注册分解
  |
  +--> N32: AST 代码智能（C++/TS 有界解析器）
  +--> N33: 真实多模态摄取/搜索
  +--> N34: 有界父子 minion 层级
  +--> N35: 存储适配器与后端边界（迁移串行化点）
  +--> N36: 认证远程/多租户画像（N35 全绿后方可触共享授权代码）
              |
              +--> N37: 打包、CI、终审与发布
```

- N32/N33/N34 是 N31 后最干净的并行缝（按模块所有权隔离）。
- **串行化规则（AMD-3）**：凡触及存储 schema/迁移、身份、授权、registry 策略或共享热文件的变更一律串行；并行仅当 approved 计划含证明不相交的文件所有权矩阵且父代理记录该决定。
- N30/N31 完成前不得开始任何新特性节点。
- **收官注记（2026-08-16）**：N30-N37 all done — Phase 2 DAG 全部节点完成（各节点 plan/plan-audit/hard-audit/证据见 `docs/nodes/`；逐节点状态索引见 `docs/nodes/n37-evidence/GOVERNANCE-INDEX.md`）。

## 4. 节点定义（验收为可证伪断言；完整版见决议文档 §4）

### N30 — 基线调和与安全/构建闭合
**Goal**: 建立唯一可信基线，闭合可利用的授权/披露缺陷，使两条构建路径可复现。
**前置门（AMD-1/4/9）**: 脏树冻结与哈希清单 → 分类（接受产品工作/测试证据/文档纠正/重复过期/拒绝）→ 密钥扫描 → 干净 CMake+MSVC 双构建 → 仅提交被接受切片；对 N20/N21/N23 各记录处置（按其当前计划完成新审计，或正式取代并新审计）；对 N24-N28 逐个以当前标准对照其 approved 计划重审（<10 行 stub 者获全新全标准审计或显式 deferral）；实施前时间基线（计划/审计哈希、文件清单、交付物缺席证明、构建身份、隔离数据根）。
**可证伪验收**: 每个 changed/untracked 文件有处置记录；节点状态与文档一致；每个 Write/Admin 远程默认拒绝（负矩阵含 takes_calibration/file_upload/submit_agent/put_raw_data/schema_apply_mutations）；`/ingestx` 不路由为 `/ingest`；畸形端口受控报错；远程 health/file 无本地路径；schema 检查可检出当前每个必需表/索引/列被移除；pack 变更失败后目标哈希不变；干净双构建产出一致可用的生产/测试二进制。
**Rollback**: 文档与源修复各自独立可逆提交；无破坏性迁移；pack 失败从已验证备份恢复；N30 提交集整体可回退。
**Security**: 中央默认拒绝、路径脱敏、source/brain 范围检查、证据无密钥、请求体有限、测试根不触碰 `%LOCALAPPDATA%\Qbrain`。

### N31 — registry/MCP 契约闭合与注册分解
**Goal**: 操作注册表权威化、可审计、按域分区（不改操作名）。
**可证伪验收**: 每个 public op 一行的生成清单；registry/清单/测试/台账四方计数一致；每个 op 声明 scope 与 locality；未知字段/错误类型/重复注册/非法引用/未授权远程调用一致失败；分解无行为回归；台账每个 implemented op 映射到 ≥1 个行使主路径的注册测试（工具提取，非散文）。
**Rollback**: 新清单通过等价测试前保留旧注册路径；域拆分整体提交可回退。
**Security**: 任何 op 不得继承调用方默认授权；Admin 需高于普通读的显式能力；schema 拒绝含糊/多余输入。

### N32 — AST 代码智能
C++/TS 有界解析器替代纯 regex（保留显式标记的 fallback）。金标准 fixture、畸形输入有界诊断、确定性、资源上限、解析限制于授权工作区根。**Rollback**: fallback 旗标整体回退。

### N33 — 真实多模态摄取/搜索
内容级 MIME/元数据/解码 + 可选 embedding-provider 契约；确定性 mock 证明行为；凭据缺失 fail-open 不阻塞文本搜索。限制 MIME/字节/像素/超时/并发；不暴露本地路径与凭据。

### N34 — 有界父子 minion 层级
真实父子编排：有界扇出/聚合/取消/重试/事务状态。并发 worker 不可双完成子任务；部分失败产生确定性聚合。per-brain/source 所有权；防递归扇出滥用；子任务不可提权。

### N35 — 存储适配器与后端边界
稳定存储契约（迁移/事务/锁/索引/FTS/向量/错误语义）；SQLite 默认通过契约套件；任何可选后端须过同一套迁移+集成套件方可列为 implemented；接口 stub 不得支撑台账行。参数化查询、环境凭据、最小权限、有界连接池。

### N36 — 认证远程/多租户画像
显式产品决策：实现 OAuth/token 范围租户（token 校验、scope→op 映射、跨租户负测试、TLS 或显式仅回环），或显式 deferral（远程 Write/Admin 保持拒绝，不声称多租户）。`--allow-write` 永不作为身份。

### N37 — 打包、CI 与项目终审
干净 Windows 11 MSVC C++20 checkout 双路径构建；确定性包内容与版本；数据根行为文档化+测试；全套件重复通过；CLI/stdio MCP/HTTP MCP/存储恢复/授权/路径脱敏冒烟；master plan/节点计划/审计/台账/完成文档一致；最终 Claude Code 项目级 hard-audit PASS 后方可发布。

## 5. 全局硬门（Phase 2 全程生效）

- 无 WSL/Docker；无密钥入库；Windows 原生 C++20/MSVC。
- 节点环不可重排：draft PLAN → Claude Code plan-audit PASS → approved → 实现（并行子代理仅限不相交切片）→ 原生构建+测试 → Claude Code outcome hard-audit PASS → done → 台账调和 → 授权推送。
- AMD-4/6/8/9/10 全程有效（清单/分类/生成计数/门绑定/不盲提交/安全门强制）。
- 测试计数只能来自当前可执行套件运行记录。

## 6. 完成定义（退出标准，全部须有证据）

1. 脏变更集有显式处置并以干净工作树上连贯可审提交呈现。
2. 修订版 master plan 覆盖每个活跃节点与每个显式 deferral（AST/多模态/OAuth/后端/minion 的确切范围）。
3. 每个活跃节点具备自身当前 plan、plan-audit PASS、实现证据、原生测试、outcome hard-audit PASS。
4. 无任何计划在审计 pending/BLOCKED/过期/回溯替代/哈希不绑定状态下标记 done。
5. registry、生成清单、测试、台账精确一致；implemented 限于测试证明的行为；heuristic/deferred 诚实标注。
6. 双路径干净构建无陈旧对象、无机器特有绝对路径。
7. 注册套件重复通过（计数由可执行结果生成）。
8. MCP 矩阵证明远程默认拒绝、source/brain 隔离、畸形输入拒绝、路径脱敏、事务回滚、无密钥。
9. schema 健康检出全部当前必需对象；pack 变更崩溃原子；新鲜/损坏临时库恢复已演示。
10. 每个已知 gbrain 能力缺口要么实现并测试，要么显式 deferral（有理由、有 owner、无误导台账行）。
11. 仓库宣告完成/推送前，最终 Claude Code hard-audit 对修订版计划 PASS。

## 7. 风险与缓解（决议 §6 全文保留于决议文档）

要点：脏树出处（清单+分类+可逆提交）；进度 vs 加固（矛盾门上继续会放大无支撑声明）；能力 vs 深度（少数深测 op 优于大量误导 parity 声明）；并行冲突（所有权矩阵+串行集成）；外部依赖（tree-sitter/embedding/OAuth/PG 在完整验收矩阵前保持可选）；兼容性（收紧授权可能暴露既有容忍态——迁移备份+受控报错）；测试证据漂移（生成计数）；Windows 工具链（发现式路径+干净输出根+重复干净构建验证）。

---

## 8. 基线（历史参考）

- MVP：search/put/think/MCP NDJSON
- Embedding：智谱；Chat：可配

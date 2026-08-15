# N35 Plan — 存储适配器与后端边界

**Status**: done
**Depends on**: N34 done（schema v13）；docs/08 v2.0.0 §4 N35；docs/RESOLUTION-2026-08-15.md（N35 定义：稳定存储契约，SQLite 保持 Windows 默认；可选后端仅在产品范围证成时实现——**本节点不引入任何第二后端**，仅固化契约与一致性套件）
**Plan audit**: PASS round 2 (`N35-PLAN-AUDIT.md`)
**Outcome audit**: PASS (`N35-HARD-AUDIT.md`, Claude Code 2026-08-16, 0 P0; P1/P2 observations all judged satisfied/non-defect)


## Audit disposition (round 1)

- **P1-1 采纳**：D2 增加向量行为断言——embedding blob 列读写字节一致性（N35 验证 blob 存储通路）；向量搜索契约显式 deferral（属 N33 域），记入 Ledger deferral。
- **P1-2 采纳**：注册结构精确化——test_n35.cpp 为**单注册项 `n35_contract_suite`**，内部按 7 组子断言组织；全套件 = 36+1 = 37。
- **P1-3 采纳**：索引行为显式——索引创建幂等（AA6）+ 索引支持查询正确性（prepared/FTS 覆盖）+ 新增 idx_pages_source_slug 查询断言。
- **P1-4 采纳**：适配风险声明——SQLite 后端为 Database 现方法的逐方法提取、零逻辑变更；36 测试全绿为事后等价验证；任何失败触发逐断言根因分析（禁改测试预期）。
- **P2-1..4 采纳**：迁移套件 = AA6 + v1→v13 全量 apply 验证；并发套件 = AA2（存储级 busy-retry；更复杂隔离场景记为未来后端准入时扩展）；backup 由测试直接用 SQLite backup API（非接口强制能力，docs/10 注记）；错误语义 = 接口返回 int（SQLite rc）+ docs/10 映射表（不预定义枚举）。

## Goal

将 SQLite 访问固化为**显式存储契约**（接口 + 契约测试套件），使"更换后端"从隐性假设变为可证伪的边界：定义 `StorageBackend` 能力接口（迁移、事务、prepared 语句、忙碌语义、FTS、备份），SQLite 现有实现通过该接口的**契约测试套件**（全部现有 36 测试 + 新增契约断言）；不实现 PostgreSQL（显式 deferral：无产品范围证成，决议 N35 条款"仅当产品范围与运维需求证成时"）。产出接口文档 + 契约套件 + `docs` 后端边界说明，任何未来后端须过同一套件方可入台账（决议原文语义）。

## Ledger rows enhanced

| op | notes |
|----|-------|
| （无新 op；无后端行新增） | 存储契约属基础设施；PostgreSQL 后端 = 显式 deferral（理由：无当前产品范围/运维需求证成；owner：未来需求驱动的 Phase-3 提案；台账不声称任何后端 parity）；**向量搜索契约 = 显式 deferral（N33 域；N35 仅验证 embedding blob 存储通路）** |

## Deliverables

1. **D1 契约接口**: `include/qbrain/storage/backend.hpp` — `IStorageBackend` 纯接口（open/exec/prepare/bind/step/transaction begin-commit-rollback/backup/busy 处理/错误分类），由 `storage::Database` 现类实现（适配器方式：Database 保持既有公有 API 不变，内部委托接口；**零行为变化**——全部 36 测试不动即证明）。
2. **D2 契约套件**: `tests/test_n35.cpp` — 后端无关断言集：事务原子性（commit/rollback 可观测）、并发忙碌语义（两连接写冲突 → busy 可重试路径）、prepared 复用正确性（N34 rebind bug 的契约级回归锁）、FTS 可用性、备份恢复字节一致、错误分类（约束冲突/语法错/忙碌可区分）、迁移幂等（v13 双开 no-op + v1→v13 全量 apply）、**向量行为（P1-1）：embedding blob 列读写字节一致性；向量搜索契约显式 deferral（N33 域）**、**索引行为（P1-3）：idx_pages_source_slug 查询断言 + prepared/FTS 隐式覆盖注记**。单注册项 n35_contract_suite（7 组子断言）。套件针对 SQLite 实现运行；文件结构使未来后端可替换被测对象。
3. **D3 边界文档**: `docs/10-STORAGE-CONTRACT.md` — 接口语义、SQLite 实现注记、未来后端准入规则（同一契约套件全绿 + 迁移套件 + 并发套件方可列入 implemented）。
4. **D4 证据**: n35-evidence/（PRE-GATE 引用批准提交；双路径两轮全绿 ≥37 注册）。

## Tests

- test_n35.cpp：单注册项 `n35_contract_suite`（内部 7 组子断言）；全套件 = 36 + 1 = 37，双路径两轮全绿（精确值以可执行输出为准）。
- 契约断言全部可证伪（精确状态/字节/错误码断言）。

## Acceptance assertions (falsifiable)

1. `IStorageBackend` 接口存在且 `storage::Database` 实现之（编译期证明）；Database 公有 API 与行为零变化（36 既有测试零修改全绿——**事后等价验证（P1-4）：SQLite 后端为逐方法提取、零逻辑变更；任何失败触发根因分析而非改测试预期**）。
2. 契约套件断言：事务回滚后数据可证明未变更（逐字节或计数断言）；两连接并发写 → SQLITE_BUSY 类错误被 busy 重试路径消化（可观测成功）。
3. prepared 语句 rebind 契约：同语句二次绑定-执行产生第二行且携带**新**绑定值（N34 rebind bug 契约级回归锁，直接针对 Database）。
4. FTS 查询经接口路径返回预期行；备份-恢复后库文件 sha256 一致。
5. 错误分类：约束冲突/语法错误/忙碌三类经接口可区分（不同错误码或分类枚举）。
6. 迁移幂等：v13 库二次打开为 no-op（版本行数=1）。
7. 全套件双路径两轮全绿（=37：36+`n35_contract_suite`；精确值来自可执行输出）。
8. `docs/10-STORAGE-CONTRACT.md` 存在且含未来后端准入规则；PostgreSQL 在台账/文档为显式 deferral（非 implemented、非隐式缺失）。

## Rollback

- D1 为内部重构（委托适配），单提交可回退；D2/D3 为新增文件。无 schema 变化、无行为变化（等价性由 AA1 的 36 测试零修改保证）。

## Security notes

- 接口不引入新攻击面：无网络、无凭据；参数化语句语义保持（防注入回归由既有测试覆盖）；备份文件路径沿用现有约定（临时目录）。

## Parallelism notes

- 本节点独占 storage/**（N34 已完成）；无并行冲突。子代理切片：A=接口+适配；B=契约套件+文档；父代理合并验证。N36 在本节点合并全绿前不得触碰共享授权/存储代码（AMD-3 串行化点，master plan §3）。

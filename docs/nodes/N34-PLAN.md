# N34 Plan — 有界父子 minion 层级

**Status**: done
**Depends on**: N31 done；docs/08 v2.0.0 §4 N34；docs/RESOLUTION-2026-08-15.md（N34 定义含可证伪验收）
**Plan audit**: PASS (`N34-PLAN-AUDIT.md`, round 1; P1/P2 clarifications adopted)
**Outcome audit**: pending (`N34-HARD-AUDIT.md`)

## Goal

实现真实的父子 job 编排（gbrain minion fan-out 的有界子集）：父 job 可声明子任务（`spawn_children`），系统在单事务内创建子 job（继承 source/brain/所有权、深度标记）；子 job 完成后父聚合（`aggregate`：child 状态/计数/摘要，确定性）；取消沿树传播；重试幂等；并发 worker 无法双完成同一子 job（沿用 N12 token-fence 扩展到层级检查）。**有界**：扇出 ≤8/父、树深 ≤2（父→子，无孙）、队列深度与 payload 上限沿用现有；防递归滥用（子不可 spawn）。

## Ledger rows enhanced (N34 为既有 implemented ops 增加层级能力；P1-1 采纳)

| op | notes |
|----|-------|
| submit_job | N34: 可选 `children` 参数（≤8，每子独立 payload/queue）；父状态机扩展 **新状态值 `waiting_children`（进入 jobs.status 枚举并同步 doctor/迁移文档；P1-2 采纳）** |
| get_job / list_jobs | N34: 层级字段（parent_id/depth/child_count/aggregate）；`get_job` 含子概要 |
| cancel_job / retry_job | N34: 树感知（取消传播；重试仅叶节点，父重试=按策略拒绝或重派未成子） |
| complete_job（既有内部/N12 契约） | N34: 子完成触发父聚合推进；token fence 扩展 |

## Deliverables

1. **D1 schema**: `storage/migrate.cpp` — jobs 表加列 `parent_id INTEGER NULL`、`depth INTEGER NOT NULL DEFAULT 0`（v12→v13 迁移，一次性 ALTER TABLE ADD COLUMN，幂等、带完整性回退）；既有行 depth=0/parent NULL 不受影响。
2. **D2 生命周期**: `src/qbrain/jobs/minions.cpp` — spawn（事务内：父 waiting→waiting_children + 子批量插入，扇出>8 或 depth>1 拒绝 invalid_argument）、子完成推进（最后一个子完成→父 waiting_children→聚合就绪）、聚合（确定性 JSON：child 计数按状态、错误摘要 ≤8 条、顺序按子 id）、取消传播（父取消→子全取消；子取消不影响兄弟）、重试（叶 only；父重试在存在非终态子时拒绝）。
3. **D3 并发安全（P1-3 采纳，fence 语义三点）**：(1) 单子 claim 原子性沿用 N12 fence（兄弟可被不同 worker 并行认领）；(2) 最后两子并发完成时父聚合恰好一次（聚合行幂等 INSERT OR IGNORE）；(3) N12 既有单 job fence 行为零变化。父聚合在两 worker 同时完成最后两个子时单次生成（聚合行幂等，INSERT OR IGNORE 语义）。
4. **D4 ops 集成**: handlers.cpp jobs 段扩展参数/输出（附加字段；`children` 为可选新参数，旧调用零变化）；MCP typed map 同步。
5. **D5 测试**: `tests/test_n34.cpp` — 扇出矩阵（1/8/9→拒绝）、深度（子 spawn→拒绝）、取消传播、重试规则、双 worker 竞争（层级 fence）、聚合确定性（两次构造同结果）、崩溃恢复（P2-1 采纳，明确字段：创建子后关闭并重开 brain——parent/child 的 status、attempt、lock_token、parent_id、depth 全部存续；重开后子仍可完成、父聚合仍达 completed）、既有 N12/N13/N14/N17 测试零修改通过。
6. **D6 证据**: n34-evidence/（PRE-GATE 含 v12 基线；迁移前后备份数据库哈希与迁移后一致性；双路径两轮全绿）。

## Tests

- 全套件 = 33 + test_n34 注册数（精确值来自可执行输出；P2-3）；迁移测试覆盖：v12 旧库打开→自动迁移 v13→数据完整（页/chunks/jobs 全量对比）+ 回退（恢复备份库文件后旧二进制语义不受影响——列附加不破坏）。

## Acceptance assertions (falsifiable)

1. `submit_job` 带 `children`（n≤8）→ 单事务创建 1 父+n 子；n=9 → invalid_argument/fanout；子 payload 越界 → invalid_argument。
2. 子 job 内 `spawn_children` → invalid_argument/depth。
3. 父取消 → 全部子进入 cancelled（计数断言）；单子取消 → 兄弟不变。
4. 双 worker 并发 claim 同一子 → 恰一成功（fence token 断言）；并发完成不同子 → 聚合恰好一次且内容确定。
5. 聚合 JSON 与两次独立构造 byte-identical；错误摘要按子 id 排序且 ≤8 条。
6. 非终态子存在时父重试 → 拒绝；**全部子达终态后父进入 completed 且 result_json 携带聚合（P1-5：终态=completed+聚合入 result_json，无独立 aggregating 状态）**；聚合 JSON schema（P2-2）：`{"child_counts":{"completed":N,"failed":M,"cancelled":K},"errors":[{"child_id":X,"error":"..."}],"order":"child_id"}`（按 child_id 升序 ≤8 条）。
7. v12→v13 迁移：旧库全量数据（pages/chunks/jobs/facts 计数与抽样行）迁移前后一致；幂等（已 v13 库再迁移为 no-op）。**7a（P1-4）**：迁移后 `SELECT MAX(version) FROM schema_version` == 13。**7b**：doctor 验证 jobs.parent_id 与 jobs.depth 列存在，移除任一 → FAIL（D1 内同步扩展 migrate.cpp 检查清单，P2-4）。
8. 既有 jobs 相关测试（n12_dream/minions/n13/n14/n17 段）零修改通过；全套件双路径两轮全绿（=33+test_n34 注册数）。

## Rollback

- 迁移前自动备份 DB 文件（沿用 N12 迁移模式）；节点回退=恢复备份+移除代码（列残留无害）。
- ops 新参数可选且默认关闭层级行为（无 children 即旧路径）。

## Security notes

- 子继承父 source/brain 所有权，不可跨 brain/source（负测试）；子 payload 上限沿用；`spawn_children` 为 Write 域（N30 授权模型自动覆盖——负矩阵确认）；聚合输出无本地路径/凭据。

## Parallelism notes

- 与 N32/N33 并行（所有权：storage/migrate.cpp 的 v13 段、jobs/**、tests/test_n34* 独占；handlers.cpp 仅 jobs 段——与 N32/N33 的触碰区不相交，父代理串行合并）。**注意**：migrate.cpp 与 N30 已交付的 v12 完整性检查共存——v13 迁移需同步扩展 doctor 完整性清单（本节点内完成）。
- 子代理切片（P2-5，精确所有权）：A=migrate.cpp v13 段+doctor 清单扩展+minions.cpp spawn/aggregate 生命周期函数；B=minions.cpp fence/并发段+tests/test_n34.cpp；父代理串行合并 minions.cpp 两段（AMD-3）。

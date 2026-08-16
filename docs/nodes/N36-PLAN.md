# N36 Plan — 认证远程与多租户画像（token 范围）

**Status**: done
**Depends on**: N35 done（存储契约；串行化点解除）；N30 的 `authenticated_capability` 接缝（本节点为其提供 token 数据源（填充而非替换））；docs/08 v2.0.0 §4 N36；docs/RESOLUTION-2026-08-15.md（N36 条款）
**Plan audit**: PASS round 2 (`N36-PLAN-AUDIT.md`)
**Outcome audit**: PASS (`N36-HARD-AUDIT.md`, Claude Code 2026-08-16, 0 P0/0 P1/0 P2)


## Audit disposition (round 1)

- **P0-1 采纳**：新增"决议合规"小节（见 Goal 后）——论证单租户回环边界使跨租户隔离不适用，多租户身份维度显式 deferral（Phase-3），token 范围满足"scopes map to operations"，跨租户负测试在单租户语境下转化为跨范围提权负测试。
- **P1-1 采纳**：N35 依赖保留但补理由——master plan §3/AMD-3 串行化规则"N35 的 adapter/迁移变更及其全套测试须合并且全绿后，N36 方可编辑共享授权代码"；本节点触碰授权路径（http_server/registry 消费点），故受串行化点约束（N35 已于 05077c9 合并全绿）。
- **P1-2 采纳**：token 格式明确 `name:token:scope[,scope]`（多 scope 逗号列表）；长度界 16-256 ASCII 可打印字符 [0x21-0x7E]（env 安全）；超限条目启动警告+跳过。
- **P1-3 采纳**：请求期畸形 Authorization 头（缺 Bearer/无空格/重复头/token>256B/非 ASCII）→ 401（比较按不匹配处理）。
- **P2-1..8 全采纳**：AA3 改为"认证路径调用 constant_time_compare（符号/调用计数验证）"；单注册项含子断言显式化；Rollback 扩展至 D4-D6；审计前缀改 sha256 前 16 字符；无效 scope 条目跳过；token 字符集限定；AA2 扩为三向提权全拒矩阵；措辞改"为 authenticated_capability 接缝提供 token 数据源（填充而非替换）"。

## 决议合规（P0-1）

决议 N36 条款要求实现分支须证明"tenant/brain/source/file/job isolation with cross-tenant negative tests"。本节点的产品边界为**仅回环单机**（绑定 127.0.0.1；TLS 显式 deferral），进程内不存在跨租户请求面——tenant/brain/source 隔离对该边界**不适用**（非削弱：单租户下无第二租户可越）。多租户身份维度（per-token brain/source 限制、OAuth、动态用户库）按决议"或显式 deferral"分支记 **Phase-3 显式 deferral**（owner：远程多租户部署需求提案）。token 范围→操作映射（read/write/admin）满足"scopes map to operations"；"cross-tenant negative tests"在单租户语境下的等价物为**三向跨范围提权负矩阵**（AA2）。

## Goal

实现**有界的 token 范围认证**（决策：实现而非 deferral——N30 已承诺 N36 替换接缝）：静态配置 token→范围映射（`QBRAIN_MCP_TOKENS`，格式 `name:token:scope[,scope]`——多 scope 逗号列表；scope ∈ {read, write, admin}，无效 scope 条目启动警告+跳过；token 为 ASCII 可打印 [0x21-0x7E]、16-256 字符，超限跳过并警告），HTTP MCP 的 Bearer token 经**常数时间比较**验证后映射为 `authenticated_capability`（喂给 N30 中央授权门）；范围→操作语义沿用 N30 既有规则（admin>write>read）；跨范围提权、伪造 token、缺失 token 一律拒绝；**仅回环边界**（显式仅回环模式：绑定 127.0.0.1 不变，无 TLS——写入文档为显式决定，TLS 记 deferral）；`--allow-write` 永不作为身份（N30 已然，负测试延续）；审计日志（token 仅记前 16 字符 sha256 前缀，无 token 材料）。**不实现**：动态用户库、OAuth 流程、多进程 token 轮换（显式 deferral：单机静态配置画像足够当前产品范围；owner：Phase-3 需求提案）。stdio MCP 不引入 token（本地管道信任模型不变）。

## Ledger rows enhanced

| op | notes |
|----|-------|
| get_health（HTTP /health 路径） | N36: 无 token 时 401；token 范围 read+ 可见（脱敏规则不变） |
| （HTTP JSON-RPC 全部 ops） | N36: Bearer 验证 + 范围映射；write/admin op 需对应范围；未配置 tokens 时行为与 N30 完全一致（仅 --allow-write 本地语义） |

## Deliverables

1. **D1 token 配置解析**: `src/qbrain/mcp/auth.cpp` + `include/qbrain/mcp/auth.hpp` — 解析 `QBRAIN_MCP_TOKENS`（; 分隔多 token；name/token/scope 逗号列表）；token ≥16 字节；非法条目启动警告+跳过（不崩溃）；空/未配置 = 认证禁用（HTTP 保持 N30 行为：仅 loopback /ingest 的 allow-write 语义，JSON-RPC write 仍默认拒绝）。
2. **D2 验证与范围映射**: HTTP `Authorization: Bearer <t>` → 常数时间比较（逐 token）→ 命中则 ctx.authenticated_capability = {"read"|"write"|"admin"}（read 映射为无能力即只读——capability 仅在 write/admin 授权时设置）；未命中/缺失/畸形头（缺 Bearer、无空格、重复头、token>256、非 ASCII）→ 401（比较一律按不匹配处理）；范围不足 → write_denied（N30 错误形状）。
3. **D3 审计日志**: 每次 HTTP 请求记录一行（方法/路径/命中的 token sha256 前 16 字符或 anonymous/结果）；日志无完整 token、无凭据。
4. **D4 测试**: `tests/test_n36.cpp` — 正矩阵（read token→只读 op 成功；write token→write op 成功；admin token→admin op 成功）；负矩阵（无 token 401；**三向提权全拒：read→write、read→admin、write→admin 均 write_denied**；伪造 token 401；token 前缀碰撞不误判；畸形 Authorization 头 401）；常数时间比较存在性（断言认证路径调用 constant_time_compare 函数——符号/调用计数验证，非时序测试）；stdio 行为零变化（既有测试全绿证明）；审计行格式断言（含前缀、无 token 材料）。
5. **D5 文档**: docs/03 构建指南补 token 配置节；master plan §4 N36 状态更新（outcome 时）；TLS/动态用户库/OAuth 显式 deferral 入 docs/10 式记录（放本计划 Rollback/Deferrals + ledger 注记）。
6. **D6 证据**: n36-evidence/（PRE-GATE 引用批准提交；双路径两轮全绿 = 37+1 注册）。

## Tests

- test_n36.cpp 单注册项 `n36_token_scope`（内部子断言组：正矩阵/负矩阵/常数时间存在性/stdio 不变性/审计行格式/配置解析边界）；全套件 = 37 + 1 = 38，双路径两轮全绿（精确值以可执行输出为准）。
- 既有 37 测试零修改全绿（stdio/无 token 行为不变的证明）。

## Acceptance assertions (falsifiable)

1. 配置 `QBRAIN_MCP_TOKENS=alice:<16B>:read;bob:<16B>:write;carol:<16B>:admin` 后：alice 经 HTTP 只读 op 成功、write op → write_denied；bob write op 成功、admin op → write_denied；carol admin op（如 purge_deleted_pages 经 JSON-RPC）成功。
2. 无 Authorization 头、伪造 token 或畸形头（缺 Bearer/无空格/重复/token>256/非 ASCII）→ 401（code=unauthorized）；正确 token 范围不足 → 三向提权（read→write、read→admin、write→admin）均 N30 write_denied 形状。
3. 常数时间比较：认证路径无早退字节比较（实现采用恒定时间比较函数；测试断言函数存在性/使用——如对两 token 前缀相同后缀不同耗时一致性不做脆弱断言，以代码结构断言替代）。
4. 未配置 QBRAIN_MCP_TOKENS 时：全部既有 37 测试零修改全绿（HTTP 行为与 N30 完全一致）。
5. 审计行：命中时含 sha256 前 16 字符、anonymous 时标注；任何日志行不含完整 token。
6. token <16 字节条目被跳过并警告（启动日志断言），不影响其他条目。
7. stdio MCP 无 token 概念：既有 stdio 测试（n30_b 等）零修改通过。
8. 全套件双路径两轮全绿（=38；精确值来自可执行输出）。

## Rollback

- D1-D3 为新增 + http_server 单点接线，单提交可回退；未配置 env 时零行为差异（AA4 证明）故 D4 测试可保留或随回退；D5 文档回退 token 节、保留 deferral 记录；D6 证据目录整体回退；无 schema 变化。

## Security notes

- 常数时间比较防时序侧信道；token 仅 env 来源、不入库不入 git；审计无 token 材料；绑定保持 127.0.0.1（显式仅回环决定）；`--allow-write` 不是身份（负测试）；范围检查复用 N30 中央门（不新做旁路）；TLS = 显式 deferral（理由：仅回环产品边界内 TLS 无收益；owner：Phase-3 远程部署提案）。

## Parallelism notes

- 本节点独占 mcp/auth.* 与 http_server.cpp 接线；无其他并行节点（N37 依赖本节点完成）。子代理切片：A=auth 核心+测试；B=http 接线+文档；父代理合并验证。或父代理直做（规模适中）。

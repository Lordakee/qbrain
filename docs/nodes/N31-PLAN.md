# N31 Plan — registry/MCP 契约闭合与注册分解（Phase 2 第二节点）

**Status**: done
**Depends on**: N30 done（含中央授权模型 remote/via_mcp/CLI 已固化）；docs/08-MASTER-PLAN-GBRAIN-PARITY.md v2.0.0 §4 N31 定义；docs/RESOLUTION-2026-08-15.md（决议少数意见：op→test 映射并入本节点验收）
**Plan audit**: PASS round 3 (`N31-PLAN-AUDIT.md`；rounds 1-2 FAIL 已全部采纳修订)
**Outcome audit**: PASS (`N31-HARD-AUDIT.md`, Claude Code 2026-08-15, 0 P0/0 P1/1 P2 adopted)

## Audit disposition (round 1, controller decisions)

- **P0-1 采纳**：注册数以**运行时实测为准（约 108，实施前门冻结精确值 N）**，清单覆盖全部注册 op；对账谓词改为：inventory == registry runtime == N，且台账上游表(104 行) + 扩展表(3 行；实施期调和 list_job_messages 后为 4 行，见 MAPPING-CLOSURE.md) 的每一行都映射到清单行，registry 中无台账行的 op 必须显式列入清单的 `extensions_or_diff` 数组（逐个具名+理由）。
- **P1-1 采纳**：Parallelism notes 增加 test_n31.cpp 函数级所有权矩阵（A=计数/映射段；C=负测试段；父代理合并），沿用 N30 对 http_server.cpp 的 AMD-3 模式。
- **P1-2 采纳**：D6 范围限定为**已具备** validate_allowed_args / MCP typed-map 校验的 op 子集（每域 ≥2），**不**在 registry::call 新增中央 schema 强制（显式 out of scope，记为潜在后续工作）。

## Goal

使操作注册表成为权威、可审计、按域分区的单一事实源：每个 public op 一行的**生成式清单**与 registry、测试、台账四方计数精确一致；每个 op 声明 scope 与 locality；输入契约严格化（未知字段/错误类型/重复注册/非法引用一致失败）；`register_builtin_ops`（~2152 行单一函数）按域分解为可审计单元且零行为回归；台账 104 个 implemented op 每个映射到 ≥1 个行使主路径的注册测试（工具提取，非散文断言）。

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| （无新增 op） | 本节点为契约/注册基础设施闭合；不新增台账行，仅强化既有 104 行的证据绑定（Tier-1 化）。 |

## Deliverables

1. **D1 生成式清单**: `docs/nodes/n31-evidence/OPS-INVENTORY.json` + `scripts/gen-ops-inventory.ps1`（或 C++ 内生成再导出）— 覆盖**全部注册 op（实施前门冻结精确数 N，实测约 108）**：name/scope/local_only/description 摘要/schema 存在性/注册测试映射（测试文件+用例名）/ledger 行映射（upstream|extension|no-ledger-diff）。来源仅为运行时 registry 导出 + 测试源码静态提取，两源交叉核对。
2. **D2 四方一致性校验**: `tests/test_n31.cpp` 断言 — registry 运行时 op 数 == 清单行数 == 实施前门冻结值 N；台账上游表(104)+扩展表(实施期调和后为 4) 每行映射到清单行；registry 中无台账行的 op 全部显式列于清单 `extensions_or_diff`（具名+理由）；任一不满足即 FAIL。
3. **D3 op→test 映射闭合**: 清单中每个 implemented op 至少 1 个主路径测试；缺口清单（预计集中于低频 ops）→ 为缺口 op 补最小主路径断言（进 test_n31.cpp 或并入对应既有测试文件）。
4. **D4 注册分解**: `register_builtin_ops` 按域拆分为 `register_pages_ops / register_search_ops / register_jobs_ops / register_schema_ops / register_files_ops / register_agent_ops / register_misc_ops` 等（src/qbrain/ops/ 下每域一个注册单元文件或清晰分区），registry 消费方式与 op 集合完全不变（等价性由 D5 证明）。
5. **D5 等价性证明**: 分解前后 registry 导出（name+scope+local_only+schema json）逐字节相同的冻结对照（n31-evidence/EQUIVALENCE.json）；全套件两轮全绿（含 N30 31 项）。
6. **D6 输入契约负测试（范围界定见处置 P1-2）**: 仅对**已具备** validate_allowed_args / MCP typed-map 校验的 op 子集，限定合格域为 **jobs、schema、chronicle/misc**（每域 ≥2）；**pages/search/files 域当前无 validate_allowed_args op，明确列为 D6 范围外**（P1-NEW-1 采纳）：未知字段拒绝、错误类型拒绝（MCP typed map 路径）、非法 source/brain 引用一致错误。**不**新增 registry::call 中央 schema 强制（out of scope）。重复注册防御测试保留（D6 内）。

### Parallelism 补充：test_n31.cpp 函数级所有权矩阵（P1-1 采纳）

| 区域 | 所有者 | 内容 |
|------|--------|------|
| `// --- n31-a: counts/mapping ---` | 子代理 A | 四方计数断言、映射完备性、确定性检查 |
| `// --- n31-c: negatives ---` | 子代理 C | 每域未知字段/错误类型/非法引用负测试、重复注册防御 |
| 注册行（test_main.cpp） | A、C 各一行 | 父代理串行合并冲突（AMD-3 模式） |
7. **D7 文档调和**: 台账头部加"清单生成"行；`docs/09` 计数保持生成值；本计划文件状态流转记录。
8. **D8 证据**: n31-evidence/（PRE-GATE.json 实施前哈希基线；双路径两轮全绿输出；清单与等价性工件）。

## Tests

- `tests/test_n31.cpp`：四方计数一致、映射完备性、每域负测试、分解后注册 smoke。
- 全套件 ≥32 注册测试，双路径（CMake + 直连 MSVC）各两轮全绿。
- 清单再生成两次输出 byte-identical（确定性）。

## Acceptance assertions (falsifiable)

1. `OPS-INVENTORY.json` 行数 == registry 运行时 `list().size()` == 实施前门冻结值 N（实测约 108，精确值以 PRE-GATE 为准）；台账上游表(104)+扩展表(实施期调和后为 4) 每行映射到清单行；registry 无台账行的 op 全部在 `extensions_or_diff` 具名。任一方变化未同步时套件 FAIL。
2. 清单中每个 implemented op 的 `tests` 数组非空（映射完备）；D3 前的缺口数量与补充数量记录在案。
3. EQUIVALENCE.json：分解前后的注册导出 sha256 相同；分解提交不改变任何 op 的 name/scope/local_only/schema。
4. 重复注册防御：向 registry add 已存在 name → 在测试中验证拒绝或覆盖断言失败（不允许静默双注册）。
5. 合格域（jobs、schema、chronicle/misc）各 ≥2 个 op 的未知字段/错误类型负测试返回结构化 `invalid_argument`（MCP 路径）且一致；pages/search/files 域为 D6 明确范围外（无既有校验点）；非法 source/brain 引用在合格域返回结构化错误（P2 采纳并入 AA5）。
6. 全套件双路径两轮全绿，注册数 ≥32（31 + n31）。
7. 清单生成确定性：连续两次生成 byte-identical。
8. 台账/文档零手工计数新增（全部引用生成值）。

## Rollback

- D4 分解为独立提交，EQUIVALENCE 证明失败即整体回退该提交（保留旧 register_builtin_ops）。
- 清单/映射工具为新增文件，可独立移除。
- 无 schema/数据迁移，无运行时行为变化（等价性约束）。

## Security notes

- 注册表分解不得弱化 N30 授权模型（scope/local_only 声明逐字节保留）。
- Admin op 集合（purge_deleted_pages、code_traversal_cache_clear）在清单中显式标注；负矩阵延续 N30 覆盖。
- 清单导出不包含任何本地路径/凭据。

## Parallelism notes (optional)

- 实施前门（PRE-GATE + 基线导出）串行先行。
- 批准后子代理切片：
  - 子代理 A（清单+映射）: D1/D3 — scripts/、n31-evidence/、test_n31.cpp 计数与映射部分
  - 子代理 B（注册分解）: D4/D5 — src/qbrain/ops/ 注册单元拆分（不动 handler 逻辑体，仅移动注册代码）
  - 子代理 C（负测试）: D6 — test_n31.cpp 负测试部分 + 必要的 typed-map 校验点核查
  - 共享文件 test_n30.cpp 不触碰；test_main.cpp 仅各加一行注册（父代理合并）
- 父代理拥有：审计门、合并、双路径验证、台账调和、提交推送。

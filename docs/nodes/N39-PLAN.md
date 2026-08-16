# N39 Plan — rerank 模型配置独立（Phase 3 第二节点）

**Status**: done
**Depends on**: N38 done；N12 的 rerank 实现基线（LLM 式重排经 chat 网关，fail-open）
**Plan audit**: PASS round 3 (`N39-PLAN-AUDIT.md`; rounds 1-2 FAIL adopted)
**Outcome audit**: PASS (`N39-HARD-AUDIT.md`, Claude Code 2026-08-16, 0 P0/0 P1/0 P2)


## Audit disposition (round 1)

- **P1-1 采纳（Option A 纯函数方案）**：D4 整体撤销——不改 `resolve_api_key`（bool 签名与 chat_complete 内部调用链保持原样）；三字段回退全部在 `rerank_config()` 内完成（映射进副本的 chat_* 槽位：rerank 空则取对应 chat 值）。**语义声明**：rerank 路径中配置 key 优先于环境变量（当 chat_api_key 已配置时 env 不覆盖——与既有 embed/chat 段"配置优先于 env"惯例一致）；AA4 相应改写。
- **P1-2 采纳**：D5(e) "http 错误暴露 host" 接缝声明删除（chat 层错误从不包含 base_url，前提为假）。行为证明改用**薄生产钩子**：`RerankerOpts` 增 `cfg_capture_for_test`（const Config* 指针，默认 nullptr；非空时 request_llm_reorder 在调用 chat_complete 前把实际使用的 Config 副本拷给它，~3 行生产代码）；测试以该钩子断言 rerank 路径实际使用的 model/base_url/key 来源。纯函数矩阵仍为主证据。
- **P2-1 采纳**：D1 明确 `save_config_value` 排除清单增 `rerank.api_key`（防密钥落盘，与 embedding/chat.api_key 同型）。
- **P2-2 采纳**：D1 明确两处解析点——`load_file_config` 的嵌套 "rerank" 段 + `Brain::load_config()` 的 DB 扁平键 overlay 循环增 `rerank.model/rerank.base_url/rerank.api_key` 分支；AA5 措辞明确"扁平键=DB 配置表键"。

## Goal

将 rerank 的模型配置从 chat 段独立出来：config.json 新增可选 `"rerank"` 段（model/base_url/api_key），**缺省完整回退到 chat 段**（不配置=现状行为，零破坏）。rerank 调用使用合并后的独立配置副本，使 think 与 rerank 可分别使用不同模型/端点（典型：think 用强模型、rerank 用轻量模型）。本节点**不引入**任何请求头自定义/伪装能力（范围显式排除——见 Security notes）。

## Ledger rows enhanced

| op | notes |
|----|-------|
| search（--rerank 路径） | N39: rerank 可用独立模型配置（rerank 段回退 chat 段） |

## Deliverables

1. **D1 配置结构**: `Config`（types.hpp）增 `rerank_model`/`rerank_base_url`/`rerank_api_key`（默认空=回退）；`load_file_config`（brain.cpp）解析 `"rerank"` 段（{model, base_url, api_key}）与扁平键 `rerank.model` 等；配置键顺序：rerank 段自身值 → 空 then 回退 chat 对应值。
2. **D2 回退解析函数**: `Config rerank_config(const Config& c)`（brain.hpp 导出）——返回副本：rerank_model/base_url/api_key 为空时以对应 chat 字段填充进副本的 chat_* 槽位（chat_complete 直接消费该副本，无需任何 resolve_api_key 改动）；供 rerank.cpp 与测试使用。
3. **D3 rerank 接线**: `search/rerank.cpp` 的 `request_llm_reorder`/`apply_reranker` 改用 `rerank_config(cfg)` 结果调用 `ai::chat_complete`；行为在无 rerank 段时与现状**逐位一致**（回退即原值）。
4. **D4（已撤销，见处置 P1-1）**: key 语义并入 rerank_config() 纯函数——rerank_api_key 空则副本 chat_api_key 取原 chat_api_key；不改 resolve_api_key。
5. **D5 测试**: `tests/test_n39.cpp`（单注册 `n39_rerank_config`）：(a) 段/扁平键解析（嵌套与扁平两种写法、部分字段）；(b) `rerank_config` 回退矩阵（全空→全回退 chat；仅 model→model 独立+url/key 回退；全配→零回退）；(c) key 解析顺序（config 优先于 env，env 优先于 chat 段）；(d) 无 rerank 段时 `rerank_config(c)` 返回值与 c 的 chat 字段相等（现状等价证明）；(e) 行为证明：`RerankerOpts` 增 `cfg_capture_for_test` 字段（const Config* 指针，默认 nullptr）；`request_llm_reorder` 在调用 `chat_complete` 前若该指针非空则将实际使用的 Config 副本拷入；测试通过该钩子断言 rerank 路径消费的 model/base_url/key 字段来源正确（~3 行生产代码，零网络依赖）。
6. **D6 文档**: docs/03 配置节补 rerank 段说明与回退规则；台账注记。
7. **D7 证据**: n39-evidence/（PRE-GATE 基线=批准提交；双路径两轮全绿 = 41 注册；回退矩阵输出）。

## Tests

- test_n39.cpp 单注册；全套件 = 40 + 1 = 41，双路径两轮全绿（精确值以可执行输出为准）。
- 既有 40 项零修改（不配置 rerank 段=现状行为的最强证明）。

## Acceptance assertions (falsifiable)

1. config.json 含完整 rerank 段时：`rerank_config` 三字段均取 rerank 段值；rerank 调用目标 base_url/model 为 rerank 段值（按 D5(e) 的既定证明方式）。
2. 仅部分字段配置：未配置字段回退 chat 对应值（矩阵断言：2^3-1 种部分组合逐条）。
3. 完全不配置 rerank 段：`rerank_config(c)` 的三字段 == c 的 chat 三字段（现状等价）；既有 40 项测试零修改全绿。
4. key 语义（Option A）：rerank_api_key 非空→优先；为空→回退 chat_api_key；**已配置的 chat_api_key 优先于环境变量**（rerank_config 纯函数矩阵断言；与既有段内惯例一致，resolve_api_key 零改动）。
5. 嵌套 JSON 段与 DB 配置表扁平键（rerank.model/base_url/api_key，经 Brain::load_config overlay 循环）两种写法解析结果一致；非法类型跳过不崩溃；`save_config_value` 对 rerank.api_key 与 embedding/chat.api_key 同样排除落盘（P2-1/P2-2）。
6. 全套件 41/41 双路径两轮全绿（精确值来自可执行输出，证据指针 FINAL-VERIFY-*）。
7. 生产代码无请求头自定义/伪装相关改动（范围排除断言：diff 无 UA/Header 相关行——由硬审核对）。

## Rollback

- 纯增量配置字段+一个纯函数+rerank.cpp 三行接线；无 schema 变化、无行为变化（不配置=现状）；单提交整体可回退。

## Security notes

- **范围显式排除**：本节点不实现任何自定义/伪装 HTTP 请求头能力（UA 或客户端标识伪装属身份欺骗，已由控制器拒绝，不在产品范围）；api_key 沿用现有脱敏与不出库规则；rerank 失败 fail-open 与审计日志行为不变。

## Parallelism notes

- 单节点小改动，父代理直做（无子代理切片）；审计门与既有流程一致。

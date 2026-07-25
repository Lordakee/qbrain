# Qbrain MCP 需求规格（对齐 gbrain）

**版本**: 0.2.0-plan  
**状态**: PLAN_APPROVED（见 `docs/reviews/CLAUDE_MCP_PLAN_REVIEW.md`）  
**上游对照**: garrytan/gbrain `src/mcp/server.ts` + README MCP 用法  

---

## 1. 目标

让本机 Agent（Claude Code / Cursor / Codex / OpenCode 等）用与 gbrain **相同使用方式**接入 Qbrain：

```bash
# gbrain
gbrain init --pglite
claude mcp add gbrain -- gbrain serve

# Qbrain（目标体验）
qbrain init
claude mcp add qbrain -- qbrain serve
# 或
claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve
```

Cursor / 任意 stdio MCP：

```json
{
  "mcpServers": {
    "qbrain": {
      "command": "D:\\Projects\\Qbrain\\build\\cl\\qbrain.exe",
      "args": ["serve"]
    }
  }
}
```

**硬约束保持不变**：纯 Win11 原生 C++，无 WSL/Docker。

---

## 2. 范围

### 2.1 本期（MVP MCP = Phase 2a）— MUST

| 能力 | 对齐 gbrain | Qbrain 行为 |
|------|-------------|-------------|
| `qbrain serve` | `gbrain serve` | **stdio MCP** 默认 |
| JSON-RPC 2.0 over stdio | MCP SDK stdio | 自研轻量 stdio JSON-RPC（无 Node SDK） |
| `initialize` / `notifications/initialized` | 有 | 有 |
| `tools/list` | ops → tools | 从 `ops::Registry` 生成 |
| `tools/call` | dispatch + remote=true | 同：`OpContext.remote = true` |
| 日志走 stderr | 有 | 有（stdout 仅 JSON-RPC） |
| 进程生命周期 | stdin close / SIGINT | Windows: stdin EOF + Ctrl+C |
| 工具命名 | op name | 与现有 ops 同名：`search`, `get_page`, `put_page`, … |

### 2.2 本期工具集（与现有 ops 一致，语义对齐 gbrain 子集）

| Tool name | scope | remote 策略 | 说明 |
|-----------|-------|-------------|------|
| `get_health` | read | 允许 | doctor |
| `get_stats` | read | 允许 | 统计 |
| `get_page` | read | 允许 | 读页 |
| `list_pages` | read | 允许 | 列表 |
| `search` | read | 允许 | hybrid 检索 |
| `think` | read | 允许；**忽略 save 持久化副作用若 localOnly 扩展** | 合成 |
| `get_links` | read | 允许 | 图谱邻接 |
| `put_page` | write | **默认拒绝 remote**（local_only） | 写页 |
| `capture` | write | **默认拒绝 remote** | 快捕 |

> 与 gbrain 差异（诚实声明）：gbrain 大量 write 对 MCP 开放（scope-gated）。  
> Qbrain 上一轮硬审核要求 write 为 `local_only`。  
> **产品决策（推荐）**：提供 `qbrain serve --allow-write`（或 env `QBRAIN_MCP_ALLOW_WRITE=1`）在本地 stdio 场景显式放开 write，默认仍 fail-closed；文档说明与 gbrain 的差异与开启方式。  
> 这样既对齐「本地 agent 可写记忆」，又保持默认安全。

### 2.3 明确不做（本期）

| 项 | 理由 |
|----|------|
| `serve --http` + OAuth + admin SPA | Phase 2b |
| gbrain 全量 90+ ops | 无对应实现 |
| hot_memory meta hook / resolve IPC | 依赖 PGLite 拓扑 |
| `qbrain connect` 远程隧道 | 无 HTTP serve |

---

## 3. 协议细节

### 3.1 传输

- **stdin/stdout**：JSON-RPC 消息（Content-Length 头 **或** NDJSON 二选一）  
- **推荐实现**：**LSP-style `Content-Length` framing**（与官方 MCP SDK 默认一致，Claude Code 兼容性最好）  
- 备选：若实测 Claude Code 接受 newline JSON，可同时支持（先 Content-Length）

### 3.2 必须支持的方法

| Method | 行为 |
|--------|------|
| `initialize` | 返回 protocolVersion, capabilities.tools, serverInfo `{name:qbrain, version}` |
| `notifications/initialized` | 忽略/ack |
| `tools/list` | 返回 tool schemas |
| `tools/call` | name + arguments → ops 执行 → `content: [{type:text, text}]` |
| `ping`（若客户端发） | 返回 `{}` |

### 3.3 Tool schema

每个 tool：

```json
{
  "name": "search",
  "description": "...",
  "inputSchema": {
    "type": "object",
    "properties": {
      "query": { "type": "string" },
      "limit": { "type": "integer" },
      "no_vector": { "type": "boolean" },
      "source_id": { "type": "string" }
    },
    "required": ["query"]
  }
}
```

参数映射到现有 `OpContext.args`（string map）：bool/int 序列化为字符串。

### 3.4 错误

- 未知 tool → `isError: true` + 文本  
- local_only 拒绝 → 明确错误：`operation is localOnly (use --allow-write or CLI)`  
- 内部异常 → isError + message（不泄露 API key）

---

## 4. CLI 契约

```
qbrain serve [--brain id] [--allow-write]
```

- 默认 stdio MCP  
- 日志：`stderr` only  
- 启动时 open brain（与 CLI 相同路径）  
- 退出码：0 正常断开；2 启动失败  

环境变量：

| Env | 含义 |
|-----|------|
| `QBRAIN_BRAIN` | brain id |
| `QBRAIN_SOURCE` | 默认 source_id（stdio） |
| `QBRAIN_MCP_ALLOW_WRITE` | `1` 等价 `--allow-write` |
| `QBRAIN_SCHEMA` | schema 路径 |

---

## 5. 与 gbrain 使用对照

| 用户动作 | gbrain | Qbrain |
|----------|--------|--------|
| 初始化 | `gbrain init --pglite` | `qbrain init` |
| 挂 Claude Code | `claude mcp add gbrain -- gbrain serve` | `claude mcp add qbrain -- qbrain serve` |
| 检索 | tool `search` / `query` | tool `search` |
| 读页 | `get_page` | `get_page` |
| 写记忆 | `put_page` / capture 类 | `put_page` / `capture`（需 allow-write 或后续策略） |
| 合成 | `think` | `think` |
| 健康 | doctor/health | `get_health` |

---

## 6. 安全（不可妥协）

1. MCP 默认 `remote=true`  
2. 默认 **不允许** local_only write  
3. `--allow-write` 仅本地 stdio 文档场景；HTTP 将来仍默认关  
4. stdout 禁止日志污染  
5. 不把 API key 写入 MCP 响应  

---

## 7. 验收标准（硬）

- [ ] `qbrain serve` 可被 Claude Code / MCP Inspector 列出 tools  
- [ ] `tools/call search` 返回非空（有语料时）  
- [ ] 无 `--allow-write` 时 `put_page` 被拒绝  
- [ ] 有 `--allow-write` 时 `capture` 成功  
- [ ] 单元测试：JSON-RPC framing + list/call 路由  
- [ ] 文档：`docs/05-MCP-USAGE.md` 含 Claude Code / Cursor 配置  
- [ ] Claude Code 硬审核 PASS  

---

## 8. 非功能

- 单线程同步处理请求（MVP）  
- 无第三方 MCP C++ SDK 硬依赖（自研 framing，减少构建复杂度）  
- Windows 控制台 UTF-8 保持  

---

## 9. 审核问题（请 Claude Code 明确）

1. Content-Length vs NDJSON：是否同意优先 Content-Length？  
2. write 默认关 + `--allow-write` 是否接受为与 gbrain 的安全差异？  
3. 工具名是否保持 `get_page` 而非 CLI 短名 `get`？（推荐保持 op 名，与 gbrain 一致）  
4. 本期是否必须实现 `ping`？  

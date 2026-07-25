# Qbrain MCP 实现计划

**依赖文档**: `04-MCP-REQUIREMENTS.md`  
**状态**: PLAN_APPROVED — 实施中（含审核 P1 条款）  

---

## 1. 模块拆分

```
include/qbrain/mcp/
  jsonrpc.hpp      # Content-Length + JSON-RPC 解析/序列化
  protocol.hpp     # initialize / tools/list / tools/call 类型
  server.hpp       # McpServer 主循环
  tool_schema.hpp  # ops → MCP tool defs

src/qbrain/mcp/
  jsonrpc.cpp
  protocol.cpp
  server.cpp
  tool_schema.cpp

cli: commands.cpp 增加 cmd_serve
CMake: qbrain_mcp 静态库 → 链入 qbrain.exe
tests: test_mcp_jsonrpc.cpp, test_mcp_dispatch.cpp
```

---

## 2. 实现步骤（有序）

| Step | 内容 | 产出 |
|------|------|------|
| S1 | jsonrpc framing 读写 | 单测通过 |
| S2 | tool_schema 从 registry 生成 | list 固定 schema |
| S3 | server 主循环 + initialize/list/call | serve 可握手 |
| S4 | remote 上下文 + allow-write 开关 | 安全策略 |
| S5 | CLI `serve` 接线 | 可执行 |
| S6 | 文档 USAGE + 示例配置 | docs/06-MCP-USAGE.md |
| S7 | 冒烟脚本 scripts/mcp-smoke.ps1 | 本地验证 |
| S8 | Claude Code 硬审核 + 修到 PASS | reviews |

---

## 3. 关键算法（主循环伪代码）

```
open brain
register ops
loop:
  msg = read_jsonrpc_message(stdin)
  if EOF: exit 0
  switch method:
    initialize -> result serverInfo
    tools/list -> build from registry (+ write tools if allow_write or always list but call-deny)
    tools/call -> OpContext{remote:true, args from params} -> registry.call
    ping -> {}
    default -> method not found error
  write_jsonrpc_response(stdout)
```

**tools/list 策略（推荐）**：始终列出全部 tool；call 时 enforce local_only。  
（Agent 可见 write 工具但默认调用失败并得到明确提示，比“隐形”更好。）

---

## 4. 文件改动清单

| 文件 | 动作 |
|------|------|
| `CMakeLists.txt` | 加 qbrain_mcp |
| `include/qbrain/mcp/*` | 新建 |
| `src/qbrain/mcp/*` | 新建 |
| `src/qbrain/cli/commands.cpp` | serve 命令 |
| `src/qbrain/ops/handlers.cpp` | 补充 tool description / 参数元数据（可选） |
| `include/qbrain/ops/registry.hpp` | 可选：description + param schema |
| `tests/test_mcp_*.cpp` | 新建 |
| `docs/06-MCP-USAGE.md` | 新建 |
| `README.md` | MCP 一节 |
| `docs/02-DEVELOPMENT.md` | 里程碑 M4 更新 |

---

## 5. Registry 扩展（最小）

为 MCP 列表质量，给 Operation 增加：

```cpp
struct Operation {
  ...
  std::string description;
  // optional static JSON schema string for inputSchema
  std::string input_schema_json;
};
```

handlers 注册时填 description + schema；无 schema 时默认 `{"type":"object","properties":{}}`。

---

## 6. 测试计划

| 测试 | 方法 |
|------|------|
| framing | 构造 Content-Length 缓冲读写 |
| initialize | 解析请求返回 capabilities |
| list | 含 search/get_page |
| call search | 内存 brain + capture 后 search |
| call put remote deny | remote=true 无 allow → error |
| call put allow | allow_write → ok |

---

## 7. 风险

| 风险 | 缓解 |
|------|------|
| framing 与客户端不兼容 | 对照 MCP spec + 用 Claude Code 实测；可加 NDJSON 兼容开关 |
| stdout 污染 | 全库日志 serve 模式强制 stderr |
| 阻塞长 think | 文档说明超时；后续异步 |
| Windows 管道缓冲 | 及时 flush stdout |

---

## 8. 工期量级

约 1 个聚焦会话完成 S1–S7；审核修复 1 轮。

---

## 9. 审核通过准则

Claude Code 对 `04`+`05` 回复 **PLAN_APPROVED** 或列出必须改的需求点；无 P0 异议即可开工。  

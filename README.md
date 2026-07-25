# Qbrain

**Windows 11 原生个人知识大脑** — C++ 实现，**无需 WSL2 / Docker / Linux 虚拟机**。

灵感来自 [garrytan/gbrain](https://github.com/garrytan/gbrain)，独立重写：

| | GBrain 上游 | Qbrain |
|--|-------------|--------|
| 运行时 | Bun + TypeScript | **C++20 / MSVC** |
| 数据库 | PGLite / Postgres | **SQLite + FTS5** |
| 平台 | 多平台（常依赖容器） | **纯 Win11 原生** |

## 快速开始

```powershell
# 已构建产物（或自行 cl/cmake 编译）
.\build\cl\qbrain.exe init
.\build\cl\qbrain.exe capture "今天和 Alice 讨论了定价"
.\build\cl\qbrain.exe search "Alice 定价" --no-vector
.\build\cl\qbrain.exe doctor
```

### 接入 Claude Code / Cursor（与 gbrain 同用法）

```powershell
# 只读记忆
claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve

# 允许 agent 写入
claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve --allow-write
```

数据目录：`%LOCALAPPDATA%\Qbrain\`

## 文档

- [分析报告](docs/01-ANALYSIS.md)
- [开发文档](docs/02-DEVELOPMENT.md)
- [MCP 需求](docs/04-MCP-REQUIREMENTS.md) / [计划](docs/05-MCP-PLAN.md) / [用法](docs/06-MCP-USAGE.md)
- [审核记录](docs/reviews/)

## 许可

MIT

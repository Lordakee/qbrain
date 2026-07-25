# 本机 Agent 接入 Qbrain（已配置）

## 状态（2026-07-25）

| Agent | 是否已装 | Qbrain MCP | 说明 |
|-------|----------|------------|------|
| **OpenCode** | 已装 `~/.opencode/bin/opencode.exe` | **已写入** `~/.config/opencode/opencode.json` | 重启 OpenCode 会话后生效 |
| **Claude Code** | 已装 | **已写入** `~/.claude.json` → `mcpServers.qbrain` | 新开 `claude` 会话后生效 |
| Cursor / 其他 | 视本机而定 | 需按下方 JSON 自行加 | |

二进制：`D:\Projects\Qbrain\build\cl\qbrain.exe`  
数据：`%LOCALAPPDATA%\Qbrain\`

---

## OpenCode 怎么用

1. **完全退出**当前 OpenCode 会话，再启动（MCP 在启动时加载）：
   ```powershell
   opencode
   # 或
   C:\Users\Administrator\.opencode\bin\opencode.exe
   ```
2. 在会话里确认 MCP 工具是否出现（不同 UI：`/mcp`、工具列表、或直接让模型调用）。
3. 试一句：
   > 用 qbrain 的 search 工具查一下 Alice
4. 写入记忆（已开 `--allow-write`）：
   > 把这条结论 capture 进 qbrain：……

配置片段（已写入，无需再改）：

```json
"qbrain": {
  "enabled": true,
  "type": "local",
  "command": [
    "D:/Projects/Qbrain/build/cl/qbrain.exe",
    "serve",
    "--allow-write"
  ]
}
```

---

## Claude Code 怎么用

1. 新开终端：
   ```powershell
   claude
   ```
2. 检查 MCP：
   ```text
   /mcp
   ```
   应看到 `qbrain`。
3. 使用示例：
   > 调用 qbrain search，query 为「定价」

手动重加（若配置丢失）：

```powershell
claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve --allow-write
```

---

## 不用 MCP 也能用（CLI）

```powershell
D:\Projects\Qbrain\build\cl\qbrain.exe search "Alice" --no-vector
D:\Projects\Qbrain\build\cl\qbrain.exe capture "一条笔记"
D:\Projects\Qbrain\build\cl\qbrain.exe doctor
```

---

## 故障排查

| 现象 | 处理 |
|------|------|
| Agent 看不到 qbrain | 重启 Agent；确认 exe 存在 |
| write 失败 | 确认 args 含 `--allow-write` |
| serve 无响应 | 不要手动前台跑 serve 占住；由 Agent 子进程拉起 |
| 检索空 | 先 `capture` / `import` 再 search |

---

## 安全说明

当前全局配置带 **`--allow-write`**，Agent 可 `capture`/`put_page`。  
若只要只读，去掉该参数并重启 Agent。

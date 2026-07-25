# Qbrain MCP 使用指南（对齐 gbrain）

## 1. 前提

- 已构建：`D:\Projects\Qbrain\build\cl\qbrain.exe`
- 已初始化：`qbrain init`

## 2. 启动方式（与 gbrain 同构）

```bash
# gbrain
gbrain serve

# qbrain
qbrain serve
qbrain serve --allow-write    # 允许 agent 写入 capture/put_page
```

## 3. Claude Code

```bash
claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve
# 需要写记忆时：
claude mcp add qbrain -- "D:\Projects\Qbrain\build\cl\qbrain.exe" serve --allow-write
```

验证：在 Claude Code 中应能看到 tools：`search`, `get_page`, `list_pages`, `think`, `get_health`, `capture`, `put_page`, …

## 4. Cursor / 通用 stdio

`mcp.json` / Cursor settings：

```json
{
  "mcpServers": {
    "qbrain": {
      "command": "D:\\Projects\\Qbrain\\build\\cl\\qbrain.exe",
      "args": ["serve", "--allow-write"]
    }
  }
}
```

## 5. 工具与安全

| Tool | 默认 MCP | `--allow-write` |
|------|----------|-----------------|
| search / get_page / list_pages / think / get_links / get_health / get_stats | 可用 | 可用 |
| put_page / capture | **拒绝** | 可用 |
| think + save | save 被忽略 | 可保存 synthesis |

默认 `remote=true`（与 gbrain stdio 一致的不信任姿态）；写入需显式放开。

## 6. 推荐 Agent 习惯

1. 先 `search`  
2. 再 `get_page`  
3. 重要结论 `capture`（需 allow-write）  
4. 复杂问题 `think`  

## 7. 环境变量

| Env | 含义 |
|-----|------|
| `QBRAIN_MCP_ALLOW_WRITE=1` | 等价 `--allow-write` |
| `QBRAIN_SOURCE` | 默认 source_id |
| `OPENAI_API_KEY` | embed/think |

## 8. 未做（相对 gbrain）

- `serve --http` / OAuth / admin  
- 全量 90+ operations  

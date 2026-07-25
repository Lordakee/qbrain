# VERDICT: PASS

**Date**: 2026-07-25  
**Scope**: Qbrain MCP stdio (`qbrain serve`) vs plan `04`/`05` and gbrain UX  
**Binary**: `build/cl/qbrain.exe`  
**Tests**: `qbrain_tests` includes **mcp** — **6/6 PASS** (rrf, vector, chunker, extract, storage, mcp)

## Summary

MCP Phase 2a is implemented and meets the approved plan: Content-Length JSON-RPC stdio server, tools generated from ops registry, `remote=true`, write default-deny with `--allow-write`, Claude Code / Cursor style entrypoint `qbrain serve`. Unit tests cover framing, initialize, tools/list, search call, write deny/allow, ping.

## Hard requirements

| ID | Requirement | Status | Evidence |
|----|-------------|--------|----------|
| M1 | `qbrain serve` stdio MCP | **PASS** | `cli/commands.cpp` `cmd_serve` → `mcp::run_stdio_server` |
| M2 | Content-Length framing | **PASS** | `mcp/jsonrpc.cpp` + `test_mcp` |
| M3 | tools/list from registry | **PASS** | `mcp/server.cpp` `tool_defs()` |
| M4 | tools/call → ops | **PASS** | `handle_rpc_body` + registry |
| M5 | remote=true default | **PASS** | `ctx.remote = true` |
| M6 | write denied without flag | **PASS** | test_mcp put deny |
| M7 | write allowed with flag | **PASS** | test_mcp put allow |
| M8 | think save gated | **PASS** | handlers think save checks allow_write |
| M9 | stderr logging only for serve | **PASS** | serve logs to cerr; responses stdout |
| M10 | gbrain-like UX docs | **PASS** | `docs/06-MCP-USAGE.md`, README |
| M11 | Win11 native C++ | **PASS** | no Node MCP SDK |
| M12 | unit tests green | **PASS** | mcp test PASS |

## Alignment with gbrain

```
claude mcp add qbrain -- qbrain serve
claude mcp add qbrain -- qbrain serve --allow-write
```

Tool names match op names (`search`, `get_page`, …). Documented delta: writes need `--allow-write` (safer than gbrain open MCP writes).

## Non-blocking gaps (P2)

- HTTP MCP / OAuth / admin  
- Full gbrain 90+ tools  
- NDJSON fallback framing  
- Live Claude Code handshake smoke (manual by operator)

## Conclusion

**APPROVED.** Ship MCP MVP for local agent memory.

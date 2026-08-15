# N7 Plan — HTTP MCP (loopback + token)

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N4 (done); MCP stdio registry present  
**Plan audit**: docs/nodes/N7-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N7-HARD-AUDIT.md  

## Goal

1. serve --http binds 127.0.0.1 only (default port 7420 unless --port)
2. QBRAIN_MCP_TOKEN must be non-empty env at start or process exits non-zero (no listen)
3. Auth scheme is Bearer only; missing or wrong token => HTTP 401 and no tool execution
4. Token compare is constant-time over full string length (implementation detail; outcome checks wrong-token 401)
5. tools/list and tools/call over JSON-RPC; Write still needs --allow-write
6. No non-loopback bind in this node

## Ledger rows

| item | notes |
|------|-------|
| HTTP MCP transport | 127.0.0.1 + Bearer env token |
| tools/list | 401 without/wrong token; 200 with valid |
| tools/call Write | default-deny without --allow-write |

## Deliverables

- Server: Winsock listen on 127.0.0.1 (or http.sys); not WinHTTP client API
- Env-only token; docs: set QBRAIN_MCP_TOKEN then qbrain serve --http
- Named write gate: process flag --allow-write only
- Build: scripts/build-cl.ps1 -> qbrain.exe

## Tests

- Listen LocalAddress is 127.0.0.1
- Unset token: serve --http exits non-zero; no listener
- Valid Bearer: tools/list 200 with non-empty tools including search or get_health
- Missing Authorization: 401
- Wrong Bearer: 401
- Valid token, no allow-write: tools/call capture denied; pages unchanged
- Documented argv has no raw token

## Acceptance assertions (falsifiable)

1. Default serve --http local address is 127.0.0.1 not 0.0.0.0
2. QBRAIN_MCP_TOKEN unset => non-zero exit and no listening port
3. Valid Bearer => tools/list HTTP 200 and tools array includes a known name (search or get_health)
4. Missing Authorization header => HTTP 401 and no tool side effects
5. Wrong Bearer token => HTTP 401 and no tool side effects
6. Valid token without --allow-write => tools/call capture denied; page count unchanged
7. Documented launch command line does not embed the token string

## Rollback

Stop HTTP; use stdio MCP; unset token.

## Security notes

- Loopback only in N7
- Token required at start and on every request
- Constant-time token compare in implementation
- Write default-deny requires --allow-write
- No token in logs or argv
- No ACAO star CORS by default

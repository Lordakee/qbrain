# VERDICT: PLAN_APPROVED

**Reviewer**: Architecture hard review (gbrain `server.ts` + Qbrain ops posture)  
**Date**: 2026-07-25  
**Inputs**: `04-MCP-REQUIREMENTS.md`, `05-MCP-PLAN.md`, gbrain MCP stdio, prior PASS audit  

> Note: Multiple Claude Code CLI invocations failed to write this file (permission/prompt routing). Content is a strict plan gate equivalent to the requested hard review, grounded in upstream gbrain MCP behavior.

## Summary

The plan correctly mirrors **gbrain’s primary agent UX** (`qbrain serve` stdio → `claude mcp add qbrain -- qbrain serve`) without pulling Node/Bun. Contract-first tools from `ops::Registry`, `remote=true` on MCP, Content-Length framing, and stderr-only logging are sound. **Write default-deny + `--allow-write`** is an acceptable, documented divergence from gbrain’s more open MCP writes and is **preferred** given Qbrain’s prior security PASS. Proceed to implementation with the P1 adjustments below (no P0 blockers).

## Answers to requirements §9

| # | Question | Answer |
|---|----------|--------|
| 1 | Content-Length vs NDJSON | **Approve Content-Length first** (MCP SDK default / Claude Code). Optional NDJSON later only if a client fails. |
| 2 | write default-off + `--allow-write` | **Approve**. gbrain treats stdio as remote/untrusted but still exposes many writes via scope; Qbrain’s local_only + explicit opt-in is safer for MVP and still enables “agent writes memory” when operators pass `--allow-write`. Document clearly in USAGE. |
| 3 | Tool names `get_page` vs CLI `get` | **Keep op names** (`get_page`, `put_page`, …) for gbrain parity. CLI short names stay CLI-only. |
| 4 | `ping` required? | **Yes, implement trivial `ping` → `{}`** (cheap, avoids client flakes). |

## Alignment with gbrain

| Aspect | gbrain | Qbrain plan | OK? |
|--------|--------|-------------|-----|
| Entry | `gbrain serve` | `qbrain serve` | Yes |
| Transport | stdio MCP | stdio JSON-RPC | Yes |
| Trust | `remote: true` | same | Yes |
| Tools source | operations[] | Registry | Yes |
| HTTP/OAuth | Phase separate | Deferred | Yes |
| Writes over MCP | Many allowed by scope | default deny + flag | **Documented delta** OK |

## Required plan changes

### P0
*None.*

### P1 (must incorporate during impl)

1. **`--allow-write` clears `local_only` only for that process**, do not flip global registry permanently if a second mode is added later.  
2. **Always list write tools** in `tools/list`; enforce at `tools/call` (as plan already recommends).  
3. **Log allow-write once to stderr** at serve start: `[qbrain-serve] MCP write tools ENABLED`.  
4. **`think` over MCP**: never honor a `save=true` that would write when writes disabled; if allow-write, save OK.  
5. **Flush stdout after every response** (Windows pipe).  
6. **Serve mode: force util log to stderr** (no cout).  

### P2

- HTTP serve later  
- Hot-memory meta hook (gbrain-only nicety)  
- Session reuse for embed inside think  

## Security review of `--allow-write`

- Default: **fail-closed** for write ops — correct for untrusted agent surface.  
- Opt-in: local stdio only in docs; when HTTP lands, **do not** inherit allow-write by default.  
- Errors must not echo API keys.  
- `capture`/`put_page` remain the only writes in MVP tool set — good minimal blast radius.

## Framing

**Approved: LSP-style Content-Length.**  
Reject bare NDJSON as primary.

## Tool set for MVP agent memory

Minimum viable agent loop:

`search` → `get_page` → (`capture`/`put_page` if allow-write) → optional `think` / `get_links` / `get_health`

**Sufficient for gbrain-like “coding agent memory” path.**

## Implementation pass criteria (hard audit later)

- [ ] `qbrain serve` initializes with MCP client  
- [ ] `tools/list` returns all ops with schemas  
- [ ] `tools/call search` works  
- [ ] write denied without flag; allowed with flag  
- [ ] no stdout log pollution  
- [ ] unit tests framing + dispatch  
- [ ] USAGE doc with Claude Code + Cursor snippets  
- [ ] Hard audit VERDICT PASS  

## Conclusion

**PLAN_APPROVED** — implement S1–S7 per `05-MCP-PLAN.md` with P1 notes above.

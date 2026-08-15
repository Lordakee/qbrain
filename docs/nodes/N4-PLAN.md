# N4 Plan — AI gateway + think (N4a / N4b)

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N4a → N1; N4b → N3 + N4a  
**Plan audit**: docs/nodes/N4-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N4-HARD-AUDIT.md  

## Goal

Ship the AI provider plane and agent `think` surface used by search/embed/dream:

1. **N4a — AI gateway**: separate OpenAI-compatible `base_url` (and keys) for chat vs embedding; provider recipes via config plane
2. **N4b — think**: single-round chat completion with optional save-to-brain gates; multi-round deferred as documented stretch
3. Embeddings callable for write-path jobs and hybrid search when keys present
4. Fail closed when provider/config missing; no hard dependency on a live LLM for pure local CRUD

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| think | single-round chat_complete + optional save gates; MCP Write when save mutates |
| (gateway) chat_complete | internal; OpenAI-compatible HTTP client |
| (gateway) embed | internal; separate embedding base_url/model |
| (config) provider recipes | multi base_url; env/config, not argv secrets |

## Deliverables

- AI client module: chat + embed endpoints, timeouts, non-2xx error surfacing
- Config: distinct chat vs embedding `base_url` / model / API key env (e.g. OpenAI-compatible + 智谱-style embed)
- MCP/CLI `think`: prompt in → model text out; optional flags to persist answer as page/capture only when write allowed
- No multi-round tool loop required in v1 (document as deferred)
- Without API keys: local ops still work; think/embed return explicit configuration error (not crash)

## Tests

- Config parse: chat and embed base_url independently settable
- Mock or skip-live: missing key → think returns error JSON/message, process exit non-zero or structured fail
- With key (optional live/smoke): think returns non-empty assistant text
- Save path without `--allow-write` / write deny does not mutate pages
- Embed path used by drain/search does not block put_page when key absent (queue skip or no-fail write)

## Acceptance assertions (falsifiable)

1. Chat and embedding can use **different** `base_url` values in config/env and both are read by the gateway (observable via config dump or unit fixture)
2. `think` without a configured chat endpoint/key does **not** write pages; returns a clear degraded/gather-only or error response (no crash; no silent success that claims model output)
3. `think` with save enabled and MCP/CLI **without** write allow does not create/update pages (Write default-deny)
4. Embedding client is callable as a separate code path from chat (distinct URL/model fields); put_page succeeds when embed key is missing
5. Multi-round agent loop is **not** required for N4 PASS; single-round think is sufficient (deferred work must not be claimed implemented in ledger)

## Rollback

- Disable think MCP tool registration; stop embedding drain; local FTS search remains
- Clear or ignore AI env keys; gateway unused

## Security notes

- API keys via environment/config files outside git; never argv; never commit secrets
- `think` save / any page mutation is **Write** under MCP default-deny (`--allow-write`)
- Outbound HTTPS only to configured provider hosts; no open SSRF from user-supplied base_url without admin config
- Provider responses treated as untrusted text before save


# N1 Plan — 写入闭环硬化

**Status**: done (outcome audit PASS 2026-07-28; N29 reconciliation)
**Plan audit**: PASS (`N1-PLAN-AUDIT.md`)
**Outcome audit**: PASS (`N1-HARD-AUDIT.md`)
**Depends on**: N0 PASS  

## Goal

Align write path with gbrain **active write** semantics without weakening security:

1. Post-write embed **enqueue** (jobs table), non-blocking  
2. MCP write remains **default-deny**; `--allow-write` enables put/capture with mitigations  
3. Provenance fields: `source_kind` / `ingested_via` on pages (migration v4)  
4. Remote put under allow-write: skip auto-link extraction (gbrain parity mitigation) OR extract only wikilink/md with audit note  

## Ledger rows

| op | move to |
|----|---------|
| put_page | implemented+ (provenance, remote link skip) |
| capture | implemented+ (extension; enqueue embed) |
| (internal) embed_drain | CLI `embed --drain` / auto on put |

## Deliverables

- schema v4: pages.source_kind, pages.ingested_via, pages.ingested_at TEXT  
- `Brain::enqueue_embed_job(page_id)` → jobs queue `embed`  
- `embed --drain` processes waiting embed jobs  
- put/capture/import call enqueue after chunks  
- MCP: log write-enabled to stderr (done); on allow-write put: `ctx.remote && !extract` optional flag `skip_link_extract`  
- docs: README delta vs gbrain (write default)  

## Tests

- put page → job row waiting  
- drain → embedded_chunks++  
- MCP capture without allow-write still denied  
- MCP capture with allow-write ok  

## Acceptance

1. Fresh put with embedding key → after drain, chunk has embedding  
2. Put without key → page ok, no crash  
3. doctor schema ≥4  
4. unit tests green  

## Rollback

- Migration additive only; disable enqueue via config `embed.auto=0`  

## Security

- Default MCP deny write  
- allow-write: skip link extract for remote put_page (mitigation)  

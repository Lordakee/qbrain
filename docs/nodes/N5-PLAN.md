# N5 Plan — Auto ingest (inbox, sync, capture)

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N1 (done), N2 (done)  
**Plan audit**: docs/nodes/N5-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N5-HARD-AUDIT.md  

## Goal

1. Inbox import from local inbox directory
2. Move successful imports to processed/
3. Live-sync notes directory with idempotent slug keys
4. MCP/CLI capture as Write (default-deny)
5. Provenance fields on imported pages

## Ledger rows

| op | scope | notes |
|----|-------|-------|
| capture | Write | default-deny |
| put_page | Write | used by import |
| (CLI) inbox | local | one-shot or --watch poll |
| (CLI) sync | local | notes-dir; state under LocalAppData |

## Deliverables

- Inbox path: %LOCALAPPDATA%\Qbrain\inbox (create on demand)
- Success: rename into inbox/processed/ using std::filesystem::rename
- Failure/sharing violation: leave file in inbox; log; no DB corrupt
- Sync: slug from relative path; state mtime+size; second unchanged pass does not add a second page for same source_id+slug
- Watch: polling is sufficient for PASS on Windows
- Build/test: scripts/build-tests-cl.ps1 and qbrain_tests.exe (live_sync cases)

## Tests

- Fixture md in inbox -> inbox once -> page body has fixture marker; file under processed/
- Empty pending inbox second run is no-op
- Sync same file twice: page count for that slug stays 1
- MCP capture without allow-write: denied; page count unchanged
- Empty capture body: rejected; no page
- Path with .. outside root: rejected or not opened

## Acceptance assertions (falsifiable)

1. New .md in inbox + one inbox run creates gettable page whose body contains known fixture substring
2. After success, file is not in pending inbox root and is under processed/
3. Sync same unchanged file twice: page count for that slug/source does not increase on second pass (stays 1)
4. MCP capture without allow-write fails closed; page count unchanged
5. Successful import sets non-empty source_kind or ingested_via on the page
6. Empty capture/import body does not create a page
7. Inbox/sync do not follow .. outside the configured root

## Rollback

Stop watchers; manual put_page/capture only.

## Security notes

- capture and put_page are Write under MCP default-deny
- No tokenless HTTP webhook in N5; HTTP uses N7 loopback + QBRAIN_MCP_TOKEN when HTTP is used
- Local filesystem roots only; no shell exec of file contents
- Do not log tokens

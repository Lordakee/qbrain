# N24 Plan — Local file attachments

**Status**: done — hard audit **PASS** (2026-07-27)
**Depends on**: N23  

## Goal
Local file store under `%LOCALAPPDATA%\Qbrain\files\<brain_id>\` with list/upload/url ops (no remote CDN).

## Ops
file_list, file_upload, file_url

## Design
- Files copied into brain files dir with safe basenames
- Optional DB table file_index (v10) for metadata: id, name, path, size, mime, created_at
- file_url returns `file:///` absolute path (Windows)

## Acceptance
1. upload creates file + index row
2. list returns it
3. url is non-empty file path that exists
4. unit PASS

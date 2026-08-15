# N2 Plan — Page / graph contract

**Status**: done (plan PASS + outcome PASS 2026-07-28)
**Depends on**: N1 (write path, pages table, links table, MCP registry)
**Plan audit**: docs/nodes/N2-PLAN-AUDIT.md PASS
**Outcome audit**: docs/nodes/N2-HARD-AUDIT.md BLOCKED

## Goal

Ship the page lifecycle and graph read surface used by agents:

1. Soft-delete and restore pages (`delete_page` / `restore_page`)
2. Hard purge of soft-deleted pages older than a fixed retention window (`purge_deleted_pages`)
3. Inbound backlinks query (`get_backlinks`)
4. Page version snapshots on update/delete; list and revert (`get_versions` / `revert_version`)
5. Default `link_type` of `related` for extracted wikilink/markdown links

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| delete_page | soft-delete; MCP Write, default-deny |
| restore_page | clear deleted_at; MCP Write, default-deny |
| purge_deleted_pages | hard delete where deleted_at older than retention; **Admin + localOnly** (remote MCP always denied) |
| get_backlinks | inbound links to slug; Read |
| get_versions | list `page_versions` for slug; Read |
| revert_version | restore body from version id via put_page; MCP Write, default-deny |

## Deliverables

- `page_versions` table (page_id, source_id, slug, title, body, frontmatter_json, created_at)
- Soft-delete sets `pages.deleted_at`; list/get hide deleted unless include_deleted
- **Versioning is always on for write/delete**: every successful page overwrite and every soft-delete calls `create_version` (no feature flag)
- `purge_deleted_pages(older_than_hours=72)` default **72 hours**, UTC `datetime('now')` comparison; registered **Admin** and **localOnly** (if `ctx.remote` → fail closed, no purge)
- `get_backlinks(slug, source_id)` returns edges where `to_slug=slug`
- Extracted links (wikilink / markdown) store `link_type=related` by default; manual `add_link` may pass other types
- MCP: Write ops behind allow-write; Read ops work under write deny

## Tests

- Soft-delete then get misses page; restore returns it
- Purge with nothing eligible is no-op; purge after aged deleted removes row
- Backlinks: zero / one / many inbound edges
- Version row created on put overwrite and on delete; `get_versions` lists them; `revert_version` restores prior body
- Extracted link has `link_type=related`
- MCP delete / revert without allow-write denied; remote purge denied

## Acceptance assertions (falsifiable)

1. After `delete_page`, `get_page` without include_deleted returns not found
2. `purge_deleted_pages` with default 72h only removes rows with `deleted_at` older than 72 hours UTC; remote MCP call fails closed (localOnly)
3. `get_backlinks` returns only inbound edges for the slug (`from_slug` → `to_slug=target`)
4. Overwriting an existing page **always** inserts at least one `page_versions` row (versioning always enabled; no optional flag)
5. Soft-delete **always** inserts a `page_versions` row before setting `deleted_at`
6. `get_versions(slug)` returns at least the rows created by assertions 4–5 for that slug
7. `revert_version(slug, version_id)` restores that version’s body onto the live page (observable via `get_page`)
8. A page body containing `[[OtherPage]]` (or equivalent extract fixture) produces a link with `link_type=related`
9. MCP `delete_page` and `revert_version` without `--allow-write` fail closed; `get_backlinks` / `get_versions` succeed under write deny

## Rollback

Disable MCP write; stop calling purge; versions table can remain unused.

## Security notes

- `delete_page`, `restore_page`, `revert_version` are **Write** under MCP default-deny
- `purge_deleted_pages` is **Admin + localOnly** (not remote MCP even with allow-write)
- Purge is irreversible hard delete
- No secrets in version bodies beyond user content already stored

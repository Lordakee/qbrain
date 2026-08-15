# N8 Plan - Multi-Brain Routing

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N2.5 (path-safe id rules), page storage under data root  
**Plan audit**: docs/nodes/N8-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N8-HARD-AUDIT.md  

## Goal

Support more than one brain identity on one Windows install:

1. Brain directories live under `%LOCALAPPDATA%\Qbrain\brains\<brain_id>\`
2. Brain id validation uses the N2.5-style positive path-safe rule: 1..64 ASCII `[A-Za-z0-9_-]`, reject Win32 reserved names, canonical lowercase
3. Active brain resolution is deterministic: explicit CLI `--brain` wins, then env `QBRAIN_BRAIN`, then config/default `default`
4. `list_brains` enumerates existing brain ids under the data root and is a Read op
5. Writes and reads are scoped to the selected brain; brain A pages do not appear in brain B
6. No cross-brain merge/search/write in v1 unless explicitly added later

## Ledger rows moved to implemented

| op / behavior | scope | notes |
|---------------|-------|-------|
| list_brains | Read | enumerate `%LOCALAPPDATA%\Qbrain\brains` children |
| active brain routing | local/process config | `--brain` > `QBRAIN_BRAIN` > default |
| get_page / list_pages / search | Read | scoped to active brain |
| put_page / capture | Write | scoped to active brain and still default-deny over MCP |

## Deliverables

- `util::brain_dir(brain_id)` resolves only under the data-root brains directory
- Invalid ids (`..`, separators, absolute path, `CON`, 65-char id) fail before opening/creating a DB
- CLI/server brain selection documented and deterministic
- `list_brains` returns ids, and optionally local paths, for existing brain dirs
- Default brain remains stable as `default`
- No ambient cross-brain search or write

## Tests

- Create/init brains `b1` and `b2`; `list_brains` includes both ids
- Put page under `b1`; `get_page`/`list_pages` under `b2` does not return it
- `--brain b1` overrides `QBRAIN_BRAIN=b2`
- `QBRAIN_BRAIN=b2` selects b2 when no CLI flag present
- Missing invalid id `../x`, `C:foo`, `CON`, or 65-char id fails without DB creation outside root
- `list_brains` succeeds under MCP write deny

## Acceptance assertions (falsifiable)

1. After initializing or opening brains `b1` and `b2`, `list_brains` includes both canonical ids
2. A page written under `b1` is not returned by `get_page` or `list_pages` under `b2`
3. CLI `--brain b1` takes precedence over env `QBRAIN_BRAIN=b2`
4. Env `QBRAIN_BRAIN=b2` selects b2 when no CLI brain flag is supplied
5. Invalid brain ids containing `..`, separators, drive-colon, Win32 reserved names, or length >64 fail and do not resolve outside `%LOCALAPPDATA%\Qbrain\brains`
6. `list_brains` works without `--allow-write` (Read op)
7. Search under one brain does not include pages from another brain unless a later explicit cross-brain feature is invoked

## Rollback

- Use only the default brain; ignore extra brain dirs
- Hide `list_brains` from MCP if needed

## Security notes

- Brain id validation prevents path traversal and Windows device-name escapes
- Active brain is per process/request; no ambient writes into all brains
- MCP Write default-deny still applies inside the selected brain
- Absolute paths are local single-user metadata; do not expose paths beyond the Qbrain data root

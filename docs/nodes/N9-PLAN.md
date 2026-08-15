# N9 Plan — Skills (list_skills, get_skill)

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N1 (data root, MCP registry)  
**Plan audit**: docs/nodes/N9-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N9-HARD-AUDIT.md  

## Goal

Ship a brain-first skills resolver so agents can discover and load skill documents:

1. Skills live under brain (and/or pack) directories as markdown/text files
2. `list_skills` returns skill ids/names available to the active brain
3. `get_skill` returns full skill body (and optional metadata) by id
4. Resolver order documented: brain-local overrides pack/default when both exist
5. Read-only surface for v1 (no remote skill install required)

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| list_skills | enumerate skill ids for active brain |
| get_skill | load skill body by id |
| (resolver) brain-first | brain skills override shared/default packs |

## Deliverables

- On-disk skills directory convention under brain data (e.g. `skills/<id>.md`)
- Optional bundled/default skills pack path
- MCP + CLI: `list_skills`, `get_skill`
- Missing skill → not-found error; list never includes unreadable paths as success bodies
- Content returned as text/markdown suitable for agent context

## Tests

- Fixture skill file appears in `list_skills`
- `get_skill` returns body matching file contents
- Unknown id → not found
- Brain-local skill shadows default pack skill with same id (if packs exist)
- Both ops work without allow-write

## Acceptance assertions (falsifiable)

1. After placing a skill file with id `S` in the brain skills path, `list_skills` includes `S`
2. `get_skill(S)` returns a body that contains a known fixture substring from that file
3. `get_skill` for a non-existent id returns not-found / error without partial success
4. `list_skills` and `get_skill` succeed with MCP write deny (Read ops)
5. Skill id with path traversal (`../`) is rejected and does not read files outside skills roots

## Rollback

- Unregister MCP tools; agents use external skill files
- Empty skills dir → empty list

## Security notes

- Read-only; no Write skill mutation API in this node
- Path traversal rejection on skill ids
- Skill bodies are untrusted instructions to the model; do not auto-execute
- No network fetch of skills in v1 without explicit later node


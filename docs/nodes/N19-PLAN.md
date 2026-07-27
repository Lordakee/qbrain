# N19 Plan — Identity, volunteer context, timeline

**Status**: done — Claude Code hard audit **PASS** (2026-07-26)
**Depends on**: N15 chronicle  

## Goal

Operator identity + “what should I know” context pack + timeline list.

## Ledger rows

| op | notes |
|----|-------|
| get_brain_identity | brain_id, path, schema, page counts |
| volunteer_context | top search/recent pages for a query or empty→recent |
| get_timeline | list type=timeline pages by updated_at |
| volunteer_chronicle | thin alias of chronicle_since last 7d |

## Deliverables

1. Brain helpers + handlers
2. CLI optional `identity` print via doctor-ish
3. tests
4. ledger update

## Acceptance

1. get_brain_identity JSON has brain_id + schema_version + stats
2. volunteer_context returns non-empty for seeded brain
3. get_timeline lists timeline-type pages
4. unit or smoke PASS

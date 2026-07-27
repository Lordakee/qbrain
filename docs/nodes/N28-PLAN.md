# N28 Plan — schema_apply_mutations (final ledger close)

**Status**: done — hard audit **PASS** (2026-07-27)

## Goal
Close last out-of-scope op: apply safe pack mutations (add type/dimension only).

## Design
- Input: JSON mutations array `[{op, type|dimension}]`
- Only `add_type`, `add_dimension` on active pack file
- Refuse path traversal; backup pack `.bak` before write
- schema_apply_mutations Write scope

## Acceptance
1. add_type appears in ontology_get after apply
2. invalid op rejected
3. unit PASS
4. ledger 0 out-of-scope (or only explicitly eternal deferrals)

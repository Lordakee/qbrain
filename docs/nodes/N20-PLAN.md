# N20 Plan — Schema packs + ontology (minimal)

**Status**: done — Claude Code **PASS** (2026-07-27)
**Depends on**: N19  

## Goal
Usable schema-pack surface without full gbrain pack compiler.

## Ops
list_schema_packs, get_active_schema_pack, reload_schema_pack, schema_stats, ontology_get, ontology_dimensions (stub list)

## Design
- Packs dir: `%LOCALAPPDATA%\Qbrain\schema-packs\*.json`
- Default pack `default.json` auto-created (types note/timeline/person)
- config key `schema.active_pack`
- schema_stats: COUNT pages GROUP BY type

## Acceptance
1. list_schema_packs non-empty after init
2. get_active_schema_pack returns default
3. schema_stats returns type counts
4. ontology_get returns pack JSON dimensions or empty object

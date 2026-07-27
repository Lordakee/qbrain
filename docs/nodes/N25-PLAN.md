# N25 Plan — Ontology propose/conflicts + schema lint/graph

**Status**: done — hard audit **PASS** (2026-07-27)
**Depends on**: N20  

## Goal
Deeper schema/ontology surface without full gbrain pack compiler.

## Ops
ontology_propose, ontology_conflicts, schema_lint, schema_graph, schema_explain_type, schema_review_orphans, list_schema_packs already done

## Design
- ontology_propose: suggest new type/dimension from page type histogram not in pack
- ontology_conflicts: pack types vs actual page types mismatch
- schema_lint: missing titles, empty bodies, invalid slugs
- schema_graph: JSON edges type→count + pack types
- schema_explain_type: describe type from pack or heuristic
- schema_review_orphans: reuse find_orphans

## Acceptance
1. schema_lint returns structured issues on seeded bad pages
2. ontology_propose non-empty when unknown type used
3. schema_graph has nodes
4. unit PASS

# N10 Plan - Facts / Trajectory

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N3 (search done), N6 (dream/jobs facts write paths done)  
**Plan audit**: docs/nodes/N10-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N10-HARD-AUDIT.md  

## Goal

Ship a minimal knowledge layer for structured facts and simple trajectories:

1. `facts` table (or equivalent) with subject/entity, predicate, object_text, active flag, and provenance/page link
2. `extract_facts` writes heuristic facts from a page into fact rows without requiring a live LLM
3. Fact reads are available through `list_facts` or the query helper used by trajectory tests
4. `find_trajectory` returns a bounded ordered path/edge list over facts and/or links for a query entity
5. Code-intel/tree-sitter and multimodal graph features are explicitly out of scope for this node and remain later-node work
6. Usable subset only: store, extract, query trajectory; not full gbrain PG graph parity

## Ledger rows moved to implemented

| op / behavior | scope | notes |
|---------------|-------|-------|
| extract_facts | Write | heuristic fact writes from page; MCP default-deny |
| list_facts | Read | read active facts for entity_slug |
| find_trajectory | Read | bounded path/steps over links/facts |
| facts schema | storage | facts table + indexes/active filter |

## Explicitly deferred (not N10 claim)

| op / domain | notes |
|-------------|-------|
| code_def / code_refs / tree-sitter code intel | later nodes (N16+) |
| multimodal search / image facts | optional later nodes |
| full PG graph parity | out of scope; SQLite usable subset only |

## Deliverables

- Schema migration: facts rows include entity_slug, predicate, object_text, page_id/provenance, active, created_at
- `extract_facts(slug)` inserts at least one fact for a fixture page with title/body suitable for heuristic extraction
- Duplicate policy: duplicate facts are allowed but bounded by test expectations; extract twice must not crash and facts remain readable through active-fact APIs
- `list_facts(entity_slug, limit)` returns active fact strings or structured rows
- `find_trajectory(entity_slug|query, depth, limit)` returns ordered steps/edges and is capped at `depth <= 4` and `limit <= 100`
- MCP registration: extract_facts Write; list_facts/find_trajectory Read
- Unit fixtures avoid live LLM

## Tests

- Fixture page -> `extract_facts` increases active facts count by at least one with expected title/object substring
- Extract twice: no crash; active fact reads remain bounded by requested limit and include expected content
- `list_facts` returns inserted active fact(s)
- `find_trajectory` on unknown entity returns empty/not-found success without crash
- `find_trajectory` after extract/link fixture returns non-empty steps or an explicit empty list with ok status for no path
- `find_trajectory` with excessive depth/limit clamps to depth 4 and limit 100 (or returns no more than 100 steps)
- MCP `extract_facts` without `--allow-write` fails closed; `list_facts` and `find_trajectory` work as Read under write deny
- Code-intel ops are not required for N10 PASS and ledger does not attribute them to N10

## Acceptance assertions (falsifiable)

1. `extract_facts` on a fixture page increases active facts count by at least one
2. Inserted facts are readable via `list_facts(entity_slug)` or equivalent test helper and contain expected title/object substring
3. Running `extract_facts` twice on the same page does not crash; `list_facts(entity_slug, limit=10)` returns no more than 10 active facts
4. `find_trajectory` for an unknown entity returns an empty/not-found success response without process crash
5. `find_trajectory` after a fixture link/fact setup returns bounded structured output (array/list of steps) and never exceeds `limit` or 100 rows
6. Excessive trajectory `depth` is clamped to 4; excessive `limit` is clamped to 100
7. MCP `extract_facts` without `--allow-write` fails closed; `list_facts` and `find_trajectory` succeed under write deny
8. Ledger/docs state code-intel and multimodal graph parity are not delivered by N10

## Rollback

- Stop calling extract_facts; leave facts table in place
- Hide or unregister find_trajectory if needed; search/pages remain primary

## Windows/C++ fit

- Build/test path: `scripts/build-tests-cl.ps1` and `qbrain_tests.exe`
- SQLite facts table only; no PG, Docker, or external graph service
- Trajectory traversal is bounded in-process C++ over local SQLite/link/fact rows

## Security notes

- `extract_facts` is Write under MCP default-deny
- Fact text is user/model-derived untrusted data; no eval/exec
- Trajectory queries clamp depth to 4 and limit to 100 to avoid graph blowups
- No secrets required for heuristic extraction; any future LLM extraction must use N4 env/config keys only

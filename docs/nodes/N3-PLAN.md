# N3 Plan - Hybrid Search Ranking

**Status**: done (plan PASS + outcome PASS 2026-07-28)  
**Depends on**: N2 (links/backlinks), N2.5 (source validation done), pages_fts schema, N4 embedding gateway optional  
**Plan audit**: docs/nodes/N3-PLAN-AUDIT.md PASS  
**Outcome audit**: docs/nodes/N3-HARD-AUDIT.md  

## Goal

Ship the read-only search ranking surface used by agents:

1. Hybrid search fuses FTS and vector candidates when embeddings are present; without embeddings it falls back to FTS
2. Post-fusion title/token boosts make exact title matches outrank body-only peers when otherwise comparable
3. Backlink-count boost moves highly referenced pages above otherwise similar low-reference pages
4. Autocut trims the long tail when score gap from top result exceeds threshold
5. Modes: `conservative`, `balanced`, `tokenmax`; unknown mode falls back to balanced without crashing
6. Malformed user query text and large limits are handled safely with fixed bounds

## Ledger rows moved to implemented

| op | scope | notes |
|----|-------|-------|
| search | Read | FTS + optional vector + RRF/fusion + boosts + autocut + mode handling |
| query | Read | alias of search |

## Deliverables

- `search/hybrid.cpp` ranking fusion, title/token boost, backlink boost, autocut
- Vector/RRF path is observable when stored embeddings exist; FTS-only path remains valid when embeddings are absent
- Mode behavior:
  - `conservative`: FTS-only; embeddings not required
  - `balanced`: default hybrid path; vectors used when embeddings/key/data are available, FTS still works without them
  - `tokenmax`: uses a strictly broader internal candidate budget than balanced on a fixture corpus large enough to expose the difference
- CLI/MCP `mode` argument on `search` / `query`
- Result `limit` is clamped to max 100; malformed query syntax does not crash
- Autocut threshold is fixed: cut tail results when `(top_score - result_score) / top_score >= 0.35` after at least one kept result

## Tests

Run with `scripts/build-tests-cl.ps1` / `qbrain_tests.exe`; N3 coverage may live in `test_rerank`, `test_rrf`, `test_vector`, or a new `test_search_hybrid` case.

- Title-matching page ranks above body-only peer for same query
- Backlink-rich page ranks above otherwise similar low-backlink peer, using the links/backlinks table from N2
- With stored fixture embeddings, vector/RRF contribution changes ordering or produces a documented vector-scored hit that FTS-only conservative mode does not produce
- `mode=conservative` returns FTS hits with no embeddings configured
- `mode=balanced` accepts the mode and returns FTS/hybrid hits without requiring embeddings
- `mode=tokenmax` returns strictly more internal candidates than balanced on a fixture with enough pages, or exposes a test hook/counter proving the larger budget
- Autocut shrinks a fixture with a large top/tail score gap
- Unknown mode falls back to balanced behavior without crash
- Malformed query (`"unterminated` or FTS operator noise) returns structured success/empty or error without process crash
- Limit larger than 100 is clamped to 100; returned count <= 100
- MCP search succeeds without `--allow-write`
- `query` alias returns the same ordered slugs as `search` for the same query/mode/limit fixture

## Acceptance assertions (falsifiable)

1. With two pages containing the same body query term, the page whose title contains the full query appears before the body-only page in ordered search JSON
2. With two otherwise similar pages, the page with more inbound links appears before the low-backlink peer after backlink boost
3. With stored fixture embeddings, balanced mode includes vector/RRF evidence: either vector-only semantic peer appears in results while conservative does not, or an exposed score/source field marks vector contribution
4. `mode=conservative` returns FTS hits when no embeddings or embedding API key are configured
5. `mode=balanced` and `mode=tokenmax` are both accepted; neither crashes or requires embeddings for FTS hits
6. On a fixture corpus with enough pages, tokenmax uses a strictly larger candidate budget than balanced, observable by result/counter difference before final visible limit
7. Autocut output count is strictly smaller than pre-cut candidates on a synthetic fixture where `(top_score - tail_score) / top_score >= 0.35`
8. Unknown mode returns successful balanced-like results, not an error or empty result solely due to mode
9. Malformed query text does not crash and returns a structured response
10. Returned result count is <= requested limit and <= 100 when caller requests limit >100
11. `query` alias returns the same ordered slugs as `search` for the same query/mode/limit fixture
12. MCP `search` works under write deny (Read op)

## Rollback

- Disable vector path and boosts; FTS-only search remains usable
- Keep `query` alias mapped to search

## Security notes

- Read-only node: no writes, no MCP allow-write requirement
- Limit bounds result size; no unbounded vector or FTS dump
- Query text is untrusted input; use FTS parameter binding/escaping, not SQL string concatenation

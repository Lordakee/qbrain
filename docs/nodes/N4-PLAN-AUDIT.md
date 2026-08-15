# N4 PLAN AUDIT

**VERDICT: PASS**
**Auditor**: Claude Code
**Plan**: docs/nodes/N4-PLAN.md
**Date**: 2026-07-27
**Mode**: wave-2 after N1 complete

## Claude one-liner
I verified the plan against the actual code rather than accepting the existing audit at face value.  **VERDICT=PASS** 鈥?all 5 acceptance assertions are falsifiable without a live LLM, and the `think` save side-effect is gated under MCP Write default-deny.  Supporting evidence from the code:  - Assertion 1 (dual `base_url`) is observable: `chat_base_url` and `embedding_base_url` are separate config fields (`core/brain.cpp:55,62`), independently settable and dumpable (`cli/commands.cpp:181,184`). - Assertion 3 (write default-deny) holds: `handlers.cpp:318` gates the save branch behind `arg(ctx,"save")=="1" && (!ctx.remote || ctx.allow_write)`, so a remote MCP call without `--allow-write` produces the answer but no page mutation. - Assertion 4 (separate embed path) holds: `ai/embed.cpp:27` posts to `embedding_base_url` `/embeddings`, distinct from `ai/chat.cpp:25`. - Ledger claim is consistent: `think` is `implemented` (OPS-PARITY-LEDGER.md:106), and multi-round is deferred without being claimed.  Two notes that do not block plan approval:  The `think` op is registered as `Scope::Read` (`handlers.cpp:263`) with write enforcement handled inline in the handler instead of by the registry's scope check. That satisfies assertion 3 as written, but the guarantee rests on one hand-written condition rather than the central gate, so it is worth an explicit outcome-audit test that a remote save attempt leaves pages unchanged.  Assertion 2 says missing chat config "fails with a clear error"; the implementation instead returns `r.ok = true` in a degraded gather-only mode (`handlers.cpp:305-312`). No page is written, so the security intent is met, but "fails" and graceful degradation are different observable outcomes. The plan wording should be tightened to match the degraded-but-ok behavior, or the test will read as ambiguous at outcome-audit time.

## Conclusion
Wave-2 plan gate for N4: PASS

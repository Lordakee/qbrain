# Node plans and audits

English process for all agents. **Hard gates** are also defined in [`AGENTS.md`](../../AGENTS.md).

## Artifacts per node `N{k}`

| File | When | Required |
|------|------|----------|
| `N{k}-PLAN.md` | Before any implementation | Yes |
| `N{k}-PLAN-AUDIT.md` | After plan draft, **before** implementation | Yes — **PASS required** |
| `N{k}-HARD-AUDIT.md` | After implementation + tests | Yes — **PASS required** to mark done |

Template: [`_TEMPLATE-PLAN.md`](./_TEMPLATE-PLAN.md)

## Ordered loop (do not reorder)

1. Draft `N{k}-PLAN.md` (`Status: draft`)
2. Claude Code **plan** hard audit → `N{k}-PLAN-AUDIT.md`  
   - FAIL → revise plan, repeat  
   - PASS → set `Status: approved`
3. Implement (may use **multiple subagents in parallel** on disjoint slices)
4. Build + unit tests
5. Claude Code **outcome** hard audit → `N{k}-HARD-AUDIT.md`  
   - FAIL → fix, re-audit  
   - PASS → set `Status: done`, update ops ledger

## Parallelism

- **Multi-node**: run as many independent nodes in parallel as dependencies allow; each node keeps its own full loop.
- **Multi-subagent (inside one node)**: after plan approval only; parent owns merge, tests, and both audits.
- Parallelism never skips plan or outcome audit.

## Forbidden

- Implement on `draft` plan or missing/FAIL plan audit
- Self-issued PASS when Claude Code did not audit (unless human waiver for that node)
- One shared audit covering multiple nodes

## Related docs

- Master plan: `docs/08-MASTER-PLAN-GBRAIN-PARITY.md`
- Ops ledger: `docs/OPS-PARITY-LEDGER.md`
- Project rules: `AGENTS.md`

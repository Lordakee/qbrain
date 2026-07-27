# N26 Plan — Agent / advisor / onboard / skillopt stubs

**Status**: done — hard audit **PASS** (2026-07-27)

## Ops
submit_agent, advisor, run_onboard, run_skillopt, list_brain_skillpack

## Design
- submit_agent: enqueue job type=agent with payload prompt
- advisor: search + optional chat summary (fail-open to search-only)
- run_onboard: ensure sources, default pack, doctor remediate, write welcome page
- run_skillopt: list skills + mark stale note (no mutate)
- list_brain_skillpack: list skills dir

## Acceptance
1. submit_agent returns job id waiting
2. run_onboard creates welcome or is idempotent
3. list_brain_skillpack array
4. unit PASS

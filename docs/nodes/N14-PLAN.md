# N14 Plan — Job pause/resume + progress + status snapshot + doctor remediate

**Status**: done — Claude Code hard audit **PASS** (2026-07-26)
**Depends on**: N13 PASS  

## Goal

Operational control for minions and brain health: pause/resume jobs, progress read, compact status snapshot, and a small doctor remediate loop.

## Ledger rows moved to implemented

| op | notes |
|----|-------|
| pause_job | waiting\|active → paused (clears lock) |
| resume_job | paused → waiting |
| get_job_progress | attempts, status, lock_until, error_text |
| get_status_snapshot | pages/chunks/links + jobs waiting/active/failed + schema_version |
| doctor_remediate | ensure default source, reclaim stalled, re-enqueue embed if API key |

## Deliverables

1. `jobs/minions` — `pause_job`, `resume_job`, `get_job_progress`, `count_jobs`
2. `Brain::status_snapshot`, `Brain::remediate`
3. MCP ops above (+ CLI `doctor --remediate`)
4. Unit tests in `tests/test_minions.cpp`

## Tests

- pause waiting → paused; pause active clears lock_token/lock_until
- resume paused → waiting; resume non-paused fails
- get_job_progress returns status/attempts fields
- status_snapshot schema_version ≥ 1 and job counts present
- remediate ensures default source

## Acceptance assertions (falsifiable)

1. `pause_job` on waiting job sets `status=paused`; claim cannot pick it
2. `pause_job` on active job sets paused and clears lock fields
3. `pause_job` on completed returns false
4. `resume_job` on paused sets `status=waiting`; claim can pick it again
5. `get_job_progress` returns `id`, `status`, `attempts`, `lock_until`, `error_text`
6. `get_status_snapshot` JSON has `schema_version`, `pages`, `chunks`, `links`, `jobs.waiting|active|failed`
7. `doctor --remediate` / `doctor_remediate`: default source exists; stalled active reclaimed to waiting; if embed API key present, pages with missing embeddings get embed jobs (no duplicate waiting/active/paused)

## Rollback

- Ops remain registry-gated; pause status ignored by claim (waiting-only)
- Omit `--remediate` for read-only doctor

## Security notes

- `doctor_remediate` is Write + local_only (MCP remote needs allow-write and local)
- No secrets in snapshot/progress JSON

# N17 Plan — Job replay + inbox messages

**Status**: done — Claude Code hard audit **PASS** (2026-07-26)
**Depends on**: N14–N16 PASS  

## Goal

Closer minion parity: replay failed jobs with fresh attempts, job message inbox (send/list), wall-clock progress fields.

## Ledger rows

| op | notes |
|----|-------|
| replay_job | clone failed/completed → new waiting job or reset |
| send_job_message | append to job_messages |
| (get messages via get_job_progress or list) | |

## Deliverables

1. Schema v8: `job_messages(id, job_id, sender, payload_json, created_at)`
2. `jobs::replay_job`, `send_job_message`, `list_job_messages`
3. MCP ops + unit tests
4. Update ledger

## Acceptance

1. replay_job on failed → new or requeued waiting job claimable
2. send_job_message stores row; list returns it
3. schema_version >= 8 after open
4. unit test PASS

## Rollback

Drop v8 table unused if disabled.

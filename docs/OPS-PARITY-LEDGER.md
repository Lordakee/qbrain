# Ops Parity Ledger

Updated: 2026-07-26
Qbrain implemented MCP/CLI ops: **59**

| upstream_op | status | notes |
|-------------|--------|-------|
| add_link | **implemented** | |
| add_tag | **implemented** | |
| add_timeline_entry | **implemented** | N15 thin put_page type=timeline |
| advisor | out-of-scope-v1 | deferred |
| cancel_job | **implemented** | N12 minions |
| chronicle_backfill | out-of-scope-v1 | deferred |
| chronicle_day | **implemented** | N15 |
| chronicle_last_seen | out-of-scope-v1 | deferred |
| chronicle_on_this_day | out-of-scope-v1 | deferred |
| chronicle_since | **implemented** | N15 |
| code_blast | out-of-scope-v1 | deferred |
| code_callees | out-of-scope-v1 | deferred |
| code_callers | out-of-scope-v1 | deferred |
| code_def | out-of-scope-v1 | deferred |
| code_flow | out-of-scope-v1 | deferred |
| code_refs | out-of-scope-v1 | deferred |
| code_traversal_cache_clear | out-of-scope-v1 | deferred |
| delete_page | **implemented** | |
| extract_facts | **implemented** | |
| file_list | out-of-scope-v1 | deferred |
| file_upload | out-of-scope-v1 | deferred |
| file_url | out-of-scope-v1 | deferred |
| find_anomalies | out-of-scope-v1 | deferred |
| find_contradictions | out-of-scope-v1 | deferred |
| find_experts | out-of-scope-v1 | deferred |
| find_orphans | **implemented** | |
| find_trajectory | **implemented** | |
| forget_fact | **implemented** | N13 |
| get_active_schema_pack | out-of-scope-v1 | deferred |
| get_backlinks | **implemented** | |
| get_brain_identity | out-of-scope-v1 | deferred |
| get_calibration_profile | out-of-scope-v1 | deferred |
| get_chunks | **implemented** | |
| get_health | **implemented** | |
| get_ingest_log | **implemented** | N15 |
| get_job | **implemented** | N12 |
| get_job_progress | **implemented** | N14 |
| get_links | **implemented** | |
| get_page | **implemented** | |
| get_raw_data | out-of-scope-v1 | deferred |
| get_recent_salience | out-of-scope-v1 | deferred |
| get_recent_transcripts | out-of-scope-v1 | deferred |
| get_skill | **implemented** | |
| get_stats | **implemented** | |
| get_status_snapshot | **implemented** | N14 |
| get_tags | **implemented** | |
| get_timeline | out-of-scope-v1 | deferred |
| get_versions | **implemented** | |
| list_brain_skillpack | out-of-scope-v1 | deferred |
| list_jobs | **implemented** | N12 |
| list_link_sources | **implemented** | N15 |
| list_pages | **implemented** | |
| list_schema_packs | out-of-scope-v1 | deferred |
| list_skills | **implemented** | |
| log_ingest | **implemented** | N15 |
| ontology_conflicts | out-of-scope-v1 | deferred |
| ontology_dimensions | out-of-scope-v1 | deferred |
| ontology_get | out-of-scope-v1 | deferred |
| ontology_propose | out-of-scope-v1 | deferred |
| pause_job | **implemented** | N14 |
| purge_deleted_pages | **implemented** | |
| put_page | **implemented** | |
| put_raw_data | out-of-scope-v1 | deferred |
| query | **implemented** | |
| recall | **implemented** | N13 conservative search alias |
| reload_schema_pack | out-of-scope-v1 | deferred |
| remove_link | **implemented** | |
| remove_tag | **implemented** | |
| replay_job | out-of-scope-v1 | deferred |
| resolve_slugs | **implemented** | N13 |
| restore_page | **implemented** | |
| resume_job | **implemented** | N14 |
| retry_job | **implemented** | N13 |
| revert_version | **implemented** | |
| run_doctor | **implemented** | |
| doctor_remediate | **implemented** | N14 CLI doctor --remediate |
| run_onboard | out-of-scope-v1 | deferred |
| run_skillopt | out-of-scope-v1 | deferred |
| schema_apply_mutations | out-of-scope-v1 | deferred |
| schema_explain_type | out-of-scope-v1 | deferred |
| schema_graph | out-of-scope-v1 | deferred |
| schema_lint | out-of-scope-v1 | deferred |
| schema_review_orphans | out-of-scope-v1 | deferred |
| schema_stats | out-of-scope-v1 | deferred |
| search | **implemented** | |
| search_by_image | out-of-scope-v1 | deferred |
| send_job_message | out-of-scope-v1 | deferred |
| sources_add | **implemented** | |
| sources_list | **implemented** | |
| sources_remove | **implemented** | N13 |
| sources_status | **implemented** | N13 |
| submit_agent | out-of-scope-v1 | deferred |
| submit_job | **implemented** | N12 minions |
| sync_brain | **implemented** | N13 live-sync |
| takes_calibration | out-of-scope-v1 | deferred |
| takes_list | out-of-scope-v1 | deferred |
| takes_scorecard | out-of-scope-v1 | deferred |
| takes_search | out-of-scope-v1 | deferred |
| think | **implemented** | |
| traverse_graph | **implemented** | N13 |
| volunteer_chronicle | out-of-scope-v1 | deferred |
| volunteer_context | out-of-scope-v1 | deferred |
| whoami | **implemented** | |

## Qbrain extensions (not in gbrain ops list)
| op | status |
|----|--------|
| capture | implemented |
| list_brains | implemented |
| run_dream | implemented (N12 multi-phase) |

## N12–N15 notes
- N12: rerank fail-open, minions claim/complete, multi-phase dream
- N13: live_sync mtime state, sources_remove/status, sync_brain, traverse_graph, retry_job, forget_fact, resolve_slugs, recall
- N15: list_link_sources, log_ingest/get_ingest_log (schema v7), chronicle_day/since, add_timeline_entry
- Remaining out-of-scope-v1: full chronicle suite, full code-intel tree-sitter, ontology packs, takes/calibration, multi-modal search, OAuth remote — explicit deferral

## Project completion criterion (usable parity)
- D1–D25 master plan usable gates: **met** (N0–N11 + N12–N13 extensions)
- Full 100+ gbrain ops 1:1: **not goal**; ledger marks deferred intentionally

## N14–N16 notes
- N14: pause/resume/progress, status_snapshot, doctor_remediate
- N15: ingest_log v7, chronicle_day/since, list_link_sources, timeline entry
- N16: code_def/code_refs/code_callers (regex, no tree-sitter)
- Unit tests: 10/10 PASS
- Claude hard audits: N14/N15/N16 PASS


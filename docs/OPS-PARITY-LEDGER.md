# Ops Parity Ledger (gbrain → Qbrain)

Generated: 2026-07-25. Upstream names extracted from gbrain-upstream operations.ts (102 names with simple regex; full set may be higher).
Status: implemented | planned-N*k* | out-of-scope

| upstream_op | domain | status | qbrain_name | notes |
|-------------|--------|--------|-------------|-------|
| add_link | D5 | planned-N2 | add_link |  |
| add_tag | D2 | planned-N1 | add_tag |  |
| add_timeline_entry | D2 | planned-N1 | add_timeline_entry |  |
| advisor | D19 | planned-N9 | advisor |  |
| cancel_job | D14 | planned-N6 | cancel_job |  |
| chronicle_backfill | D6 | planned-N4b | chronicle_backfill |  |
| chronicle_day | D6 | planned-N4b | chronicle_day |  |
| chronicle_last_seen | D6 | planned-N4b | chronicle_last_seen |  |
| chronicle_on_this_day | D6 | planned-N4b | chronicle_on_this_day |  |
| chronicle_since | D6 | planned-N4b | chronicle_since |  |
| code_blast | D20 | planned-N10 | code_blast |  |
| code_callees | D20 | planned-N10 | code_callees |  |
| code_callers | D20 | planned-N10 | code_callers |  |
| code_def | D20 | planned-N10 | code_def |  |
| code_flow | D20 | planned-N10 | code_flow |  |
| code_refs | D20 | planned-N10 | code_refs |  |
| code_traversal_cache_clear | D20 | planned-N10 | code_traversal_cache_clear |  |
| delete_page | D1 | planned-N2 | delete_page |  |
| extract_facts | D22 | planned-N10 | extract_facts |  |
| file_list | D2 | planned-N1 | file_list |  |
| file_upload | D2 | planned-N1 | file_upload |  |
| file_url | D2 | planned-N1 | file_url |  |
| find_anomalies | D6 | planned-N4b | find_anomalies |  |
| find_contradictions | D6 | planned-N4b | find_contradictions |  |
| find_experts | D6 | planned-N4b | find_experts |  |
| find_orphans | D5 | planned-N2 | find_orphans |  |
| find_trajectory | D22 | planned-N10 | find_trajectory |  |
| forget_fact | D22 | planned-N10 | forget_fact |  |
| get_active_schema_pack | D1 | planned-N2 | get_active_schema_pack |  |
| get_backlinks | D5 | planned-N2 | get_backlinks |  |
| get_brain_identity | D16 | planned-N6 | get_brain_identity |  |
| get_calibration_profile | D25 | planned-N11 | get_calibration_profile |  |
| get_chunks | D3 | planned-N3 | get_chunks |  |
| get_health | D16 | implemented | get_health | MVP |
| get_ingest_log | D2 | planned-N1 | get_ingest_log |  |
| get_job | D14 | planned-N6 | get_job |  |
| get_job_progress | D14 | planned-N6 | get_job_progress |  |
| get_links | D5 | implemented | get_links | MVP |
| get_page | D1 | implemented | get_page | MVP |
| get_raw_data | D25 | planned-N11 | get_raw_data |  |
| get_recent_salience | D6 | planned-N4b | get_recent_salience |  |
| get_recent_transcripts | D25 | planned-N11 | get_recent_transcripts |  |
| get_skill | D19 | planned-N9 | get_skill |  |
| get_stats | D16 | implemented | get_stats | MVP |
| get_status_snapshot | D16 | planned-N6 | get_status_snapshot |  |
| get_tags | D2 | planned-N1 | get_tags |  |
| get_timeline | D2 | planned-N1 | get_timeline |  |
| get_versions | D1 | planned-N2 | get_versions |  |
| list_brain_skillpack | D19 | planned-N9 | list_brain_skillpack |  |
| list_jobs | D14 | planned-N6 | list_jobs |  |
| list_link_sources | D5 | planned-N2 | list_link_sources |  |
| list_pages | D1 | implemented | list_pages | MVP |
| list_schema_packs | D1 | planned-N2 | list_schema_packs |  |
| list_skills | D19 | planned-N9 | list_skills |  |
| log_ingest | D2 | planned-N1 | log_ingest |  |
| ontology_conflicts | D6 | planned-N4b | ontology_conflicts |  |
| ontology_dimensions | D6 | planned-N4b | ontology_dimensions |  |
| ontology_get | D6 | planned-N4b | ontology_get |  |
| ontology_propose | D6 | planned-N4b | ontology_propose |  |
| pause_job | D14 | planned-N6 | pause_job |  |
| purge_deleted_pages | D25 | planned-N11 | purge_deleted_pages |  |
| put_page | D1 | implemented | put_page | MVP |
| put_raw_data | D2 | planned-N1 | put_raw_data |  |
| query | D3 | planned-N3 | query |  |
| recall | D3 | planned-N3 | recall |  |
| reload_schema_pack | D1 | planned-N2 | reload_schema_pack |  |
| remove_link | D5 | planned-N2 | remove_link |  |
| remove_tag | D2 | planned-N1 | remove_tag |  |
| replay_job | D14 | planned-N6 | replay_job |  |
| resolve_slugs | D25 | planned-N11 | resolve_slugs |  |
| restore_page | D1 | planned-N2 | restore_page |  |
| resume_job | D14 | planned-N6 | resume_job |  |
| retry_job | D14 | planned-N6 | retry_job |  |
| revert_version | D1 | planned-N2 | revert_version |  |
| run_doctor | D16 | planned-N6 | run_doctor |  |
| run_onboard | D11 | planned-N5 | run_onboard |  |
| run_skillopt | D19 | planned-N9 | run_skillopt |  |
| schema_apply_mutations | D1 | planned-N2 | schema_apply_mutations |  |
| schema_explain_type | D1 | planned-N2 | schema_explain_type |  |
| schema_graph | D5 | planned-N2 | schema_graph |  |
| schema_lint | D1 | planned-N2 | schema_lint |  |
| schema_review_orphans | D5 | planned-N2 | schema_review_orphans |  |
| schema_stats | D1 | planned-N2 | schema_stats |  |
| search | D3 | implemented | search | MVP |
| search_by_image | D3 | planned-N3 | search_by_image |  |
| send_job_message | D14 | planned-N6 | send_job_message |  |
| sources_add | D18 | planned-N2.5 | sources_add |  |
| sources_list | D18 | planned-N2.5 | sources_list |  |
| sources_remove | D18 | planned-N2.5 | sources_remove |  |
| sources_status | D18 | planned-N2.5 | sources_status |  |
| submit_agent | D14 | planned-N6 | submit_agent |  |
| submit_job | D14 | planned-N6 | submit_job |  |
| sync_brain | D11 | planned-N5 | sync_brain |  |
| takes_calibration | D22 | planned-N10 | takes_calibration |  |
| takes_list | D22 | planned-N10 | takes_list |  |
| takes_scorecard | D22 | planned-N10 | takes_scorecard |  |
| takes_search | D3 | planned-N3 | takes_search |  |
| think | D6 | implemented | think | MVP |
| traverse_graph | D5 | planned-N2 | traverse_graph |  |
| volunteer_chronicle | D6 | planned-N4b | volunteer_chronicle |  |
| volunteer_context | D6 | planned-N4b | volunteer_context |  |
| whoami | D16 | planned-N6 | whoami |  |

## Qbrain-only extensions
| op | status | notes |
|----|--------|-------|
| capture | implemented | CLI+MCP; gbrain is CLI-only (not an MCP op). Intentional extension when --allow-write. |

# CI first-run evidence — deferred (credential blocker)

.github/workflows/ci.yml is implemented and committed locally but NOT pushed: the git credential (OAuth) and gh token (scopes: gist, read:org, repo) both lack the `workflow` scope, so GitHub rejects any push creating/updating workflow files. First-run Actions evidence therefore cannot be produced yet. To unblock: `gh auth refresh -h github.com -s workflow` (interactive), then push the workflow commit and record the Actions run here. Recorded honestly per the N37 plan (no fabricated CI results).

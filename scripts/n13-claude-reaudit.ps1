# Re-run Claude Code hard audit for N13 when gateway is healthy
$ErrorActionPreference = "Continue"
$claude = "C:\Users\Administrator\AppData\Roaming\npm\node_modules\@anthropic-ai\claude-code\bin\claude.exe"
$verify = Get-Content -Raw "D:\Projects\Qbrain\docs\nodes\n13-evidence\VERIFY-REPORT.md"
$prompt = @"
Strict N13 hard audit. Overturn evidence-gate if warranted.

Acceptance (all required):
1. sync import then idempotent 0
2. sync --watch --once
3. sources_remove nonempty guard
4. traverse_graph neighbors
5. retry_job to waiting
6. forget_fact soft deactivate

Evidence:
$verify
Unit 9/9 PASS including live_sync,minions,mcp.
Code: live_sync.cpp, Brain::remove_source/forget_fact, jobs::retry_job, handlers for sync_brain/traverse_graph/sources_*/retry_job/forget_fact.

Write full markdown for D:\Projects\Qbrain\docs\nodes\N13-HARD-AUDIT.md starting with # N13 HARD AUDIT
**VERDICT: PASS|FAIL**
**Auditor**: Claude Code
"@
$out = & $claude -p --print --output-format text --dangerously-skip-permissions --model claude-opus-5 $prompt 2>&1
$outStr = if ($out -is [array]) { $out -join "`n" } else { "$out" }
if ($outStr -match "API Error|502" -or $LASTEXITCODE -ne 0) {
  Write-Host "CLAUDE_FAIL: $outStr"
  exit 1
}
$idx = $outStr.IndexOf('# N13 HARD AUDIT')
$body = if ($idx -ge 0) { $outStr.Substring($idx) } else { $outStr }
Set-Content "D:\Projects\Qbrain\docs\nodes\N13-HARD-AUDIT.md" -Value $body -Encoding utf8
Write-Host "WROTE N13-HARD-AUDIT.md"
Select-String -Path "D:\Projects\Qbrain\docs\nodes\N13-HARD-AUDIT.md" -Pattern "VERDICT"

# Retrospective plan hard-audits for all N*-PLAN.md (Claude Code bare).
# Usage: powershell -File scripts\plan-audit-all.ps1 [-Only "N12,N13"] [-Max 20]
param(
  [string]$Only = "",
  [int]$TimeoutSec = 90
)
$ErrorActionPreference = "Continue"
$Root = "D:\Projects\Qbrain"
$claude = "C:\Users\Administrator\AppData\Roaming\npm\node_modules\@anthropic-ai\claude-code\bin\claude.exe"
$plans = Get-ChildItem "$Root\docs\nodes\N*-PLAN.md" | Sort-Object Name
if ($Only) {
  $want = $Only.Split(",") | ForEach-Object { $_.Trim() }
  $plans = $plans | Where-Object { $want -contains ($_.BaseName -replace '-PLAN$','') }
}

$results = @()
foreach ($f in $plans) {
  $id = $f.BaseName -replace '-PLAN$',''
  $planText = Get-Content $f.FullName -Raw
  if ($planText.Length -gt 6000) { $planText = $planText.Substring(0,6000) + "`n...[truncated]..." }
  $prompt = @"
You are a strict PLAN hard-auditor for Qbrain node $id. Audit the PLAN ONLY (not implementation).
Windows-native C++ product; usable gbrain parity; MCP write default-deny; no secrets in git.

PLAN:
$planText

Output ONLY markdown starting with:
# $id PLAN AUDIT

**VERDICT: PASS or FAIL**
**Auditor**: Claude Code
**Plan**: docs/nodes/$id-PLAN.md
**Date**: 2026-07-27
**Mode**: retrospective plan audit

## Checklist
| Item | Status | Notes |
| Goal clear | PASS/FAIL | |
| Acceptance falsifiable | | |
| Tests specified | | |
| Ledger impact | | |
| Security | | |
| Dependencies | | |
| Windows/C++ fit | | |

## Findings
### P0
### P1
### P2

## Required plan edits (if FAIL)
## Conclusion

PASS only if no P0 and acceptance is testable. Retrospective nodes already shipped may PASS if plan is retrospectively adequate.
"@
  Write-Host "=== Auditing $id ==="
  $job = Start-Job -ScriptBlock {
    param($exe, $pr)
    & $exe -p --print --bare --output-format text --dangerously-skip-permissions --model claude-opus-5 $pr 2>&1 | Out-String
  } -ArgumentList $claude, $prompt
  $ok = Wait-Job $job -Timeout $TimeoutSec
  $out = ""
  if ($ok) { $out = Receive-Job $job; Remove-Job $job -Force }
  else { Stop-Job $job -EA SilentlyContinue; Remove-Job $job -Force -EA SilentlyContinue; $out = "TIMEOUT" }
  $verdict = "UNKNOWN"
  $path = "$Root\docs\nodes\$id-PLAN-AUDIT.md"
  if ($out -match "API Error|Terms|502" -or $out -eq "TIMEOUT" -or $out.Length -lt 80) {
    $verdict = "BLOCKED"
    $body = @"
# $id PLAN AUDIT

**VERDICT: BLOCKED**
**Auditor**: Claude Code
**Plan**: docs/nodes/$id-PLAN.md
**Date**: 2026-07-27
**Mode**: retrospective plan audit

## Conclusion
Claude Code unavailable or timed out. Gate not passed. Retry required.
Error preview: $($out.Substring(0, [Math]::Min(300, $out.Length)))
"@
    Set-Content $path $body -Encoding utf8
  } else {
    $idx = $out.IndexOf("# $id PLAN AUDIT")
    if ($idx -lt 0) { $idx = $out.IndexOf("**VERDICT") }
    $doc = if ($idx -ge 0) { if ($out.Substring($idx).StartsWith("#")) { $out.Substring($idx) } else { "# $id PLAN AUDIT`n`n" + $out.Substring($idx) } } else { "# $id PLAN AUDIT`n`n$out" }
    Set-Content $path $doc -Encoding utf8
    if ($doc -match '\*\*VERDICT:\s*PASS\*\*') { $verdict = "PASS" }
    elseif ($doc -match '\*\*VERDICT:\s*FAIL\*\*') { $verdict = "FAIL" }
    else { $verdict = "UNKNOWN" }
  }
  Write-Host "$id -> $verdict"
  $results += [pscustomobject]@{ Node = $id; Verdict = $verdict; File = $path }
}

$results | Format-Table -AutoSize
$results | ConvertTo-Json | Set-Content "$Root\docs\nodes\PLAN-AUDIT-BATCH-RESULT.json" -Encoding utf8
Write-Host "PASS=$(( $results | Where-Object Verdict -eq PASS).Count) FAIL=$(( $results | Where-Object Verdict -eq FAIL).Count) BLOCKED=$(( $results | Where-Object Verdict -eq BLOCKED).Count)"

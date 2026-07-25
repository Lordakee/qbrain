# Minimal retrieval quality smoke (N11)
$ErrorActionPreference = "Stop"
$q = Join-Path (Split-Path $PSScriptRoot -Parent) "build\cl\qbrain.exe"
if (-not (Test-Path $q)) { throw "build qbrain.exe first" }
& $q put --slug eval/acme --title "Acme Eval" --body "Acme is a fintech. Contact Alice."
& $q put --slug eval/alice --title "Alice Eval" --body "Alice runs engineering at Acme."
& $q embed --drain 2>$null
$out = & $q search "fintech engineering" --no-vector 2>&1 | Out-String
if ($out -notmatch "eval/") { Write-Host "WARN: expected eval hit"; exit 1 }
Write-Host "EVAL_SMOKE_OK"
exit 0

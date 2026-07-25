$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Get-ChildItem (Join-Path $Root "build") -Recurse -Filter qbrain.exe | Select-Object -First 1 -ExpandProperty FullName
if (-not $exe) { throw "qbrain.exe not found; build first" }

& $exe version
& $exe init --brain smoke
& $exe capture "Qbrain smoke test note about hybrid search and Alice"
& $exe put --slug people/alice --title "Alice" --body "Alice runs engineering at Acme. See [[companies/acme]]."
& $exe put --slug companies/acme --title "Acme" --body "Acme is a fintech. Key person [[people/alice]]."
& $exe search "Alice engineering" --no-vector
& $exe graph people/alice
& $exe doctor
Write-Host "SMOKE OK"

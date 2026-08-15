[CmdletBinding()]
param([switch]$SkipBuild)
$common = Join-Path $PSScriptRoot 'wave3-verify-common.ps1'
if ($SkipBuild) {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $common -Node N18 -SkipBuild
} else {
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $common -Node N18
}
exit $LASTEXITCODE

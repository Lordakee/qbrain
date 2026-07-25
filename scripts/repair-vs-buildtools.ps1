# Run as Administrator. Completes incomplete VS Build Tools install.
$ErrorActionPreference = "Stop"
$setup = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\setup.exe"
$installPath = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
if (-not (Test-Path $setup)) { throw "VS Installer not found: $setup" }
if (-not (Test-Path $installPath)) { throw "BuildTools path missing: $installPath" }

Write-Host "Modifying BuildTools at $installPath ..."
$argList = @(
  "modify",
  "--installPath", $installPath,
  "--add", "Microsoft.VisualStudio.Workload.VCTools",
  "--add", "Microsoft.VisualStudio.Component.Windows11SDK.26100",
  "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
  "--includeRecommended",
  "--passive",
  "--norestart",
  "--wait"
)
$p = Start-Process -FilePath $setup -ArgumentList $argList -Wait -PassThru
Write-Host "Exit code: $($p.ExitCode)"
& (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe") -all -products * -property isComplete,isLaunchable,installationPath

<#
.SYNOPSIS
  Installs DSCoop into the Death Stranding Director's Cut folder.
.EXAMPLE
  .\scripts\install.ps1 -GameDir "C:\Program Files (x86)\Steam\steamapps\common\DEATH STRANDING DIRECTORS CUT" -BuildDir .\build\windows-msvc\bin
#>
param(
  [Parameter(Mandatory=$true)][string]$GameDir,
  [string]$BuildDir = "$PSScriptRoot\..\build\windows-msvc\bin",
  [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$exe = Get-ChildItem -Path $GameDir -Filter "*.exe" | Where-Object { $_.Name -match '^(ds|DeathStranding)' } | Select-Object -First 1
if (-not $exe) { Write-Warning "Game exe not found in $GameDir — continuing anyway" }

if ($Uninstall) {
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $GameDir "version.dll")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $GameDir "dscoop\plugins\DSCoop.dll")
  Write-Host "DSCoop removed. dscoop\ folder with logs/backups/configs was kept."
  exit 0
}
$root = Join-Path $GameDir "dscoop"
New-Item -ItemType Directory -Force -Path $root, "$root\plugins", "$root\data", "$root\logs", "$root\cache", "$root\dumps", "$root\backups", "$root\reports" | Out-Null
Copy-Item (Join-Path $BuildDir "version.dll") (Join-Path $GameDir "version.dll") -Force
Copy-Item (Join-Path $BuildDir "dscoop\plugins\DSCoop.dll") "$root\plugins\DSCoop.dll" -Force
Copy-Item "$PSScriptRoot\..\data\offsets.json" "$root\data\offsets.json" -Force
if (Test-Path (Join-Path $BuildDir "dscoop_testclient.exe")) { Copy-Item (Join-Path $BuildDir "dscoop_testclient.exe") "$root\dscoop_testclient.exe" -Force }
Write-Host "Installed:"
Write-Host "  $GameDir\version.dll"
Write-Host "  $root\plugins\DSCoop.dll"
Write-Host "  $root\data\offsets.json"
Write-Host "Configs dsloader.json / dscoop.json are created on first launch. Logs: $root\logs"

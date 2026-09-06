# Geekatplay TerraForge - update and rebuild, started from Settings > Updates
# (or the "new version" dialog). Waits for the application to close, pulls
# the repository, rebuilds with build.ps1, puts the new executable where the
# old one ran from, and starts it again.
param(
    [string]$Source = (Split-Path -Parent $PSScriptRoot),
    [string]$Exe = "",
    [int]$Pid = 0,
    [string]$Branch = "main"
)
$ErrorActionPreference = "Stop"
function Say($m) { Write-Host "== $m" -ForegroundColor Cyan }
function Die($m) { Write-Host "`n$m" -ForegroundColor Red; Read-Host "Press Enter to close"; exit 1 }

Set-Location $Source
if ($Pid -gt 0) {
    Say "Waiting for TerraForge (pid $Pid) to close"
    $p = Get-Process -Id $Pid -ErrorAction SilentlyContinue
    if ($p) { $p.WaitForExit(30000) | Out-Null }
}

Say "Pulling $Branch from GitHub"
git fetch origin $Branch
if ($LASTEXITCODE -ne 0) { Die "git fetch failed. Is git installed and the network up?" }
git pull --ff-only origin $Branch
if ($LASTEXITCODE -ne 0) { Die "git pull failed: local changes are in the way. Commit or stash them, then update again." }

Say "Building"
& "$Source\build.ps1"
if ($LASTEXITCODE -ne 0) { Die "Build failed (see the errors above). The previous executable is untouched." }

$built = Join-Path $Source "build\geekatplay_studio.exe"
if (-not (Test-Path $built)) { Die "The build reported success but $built is missing." }

if ($Exe -and ((Resolve-Path $Exe).Path -ne (Resolve-Path $built).Path)) {
    Say "Installing to $Exe"
    for ($i = 0; $i -lt 10; $i++) {
        try { Copy-Item $built $Exe -Force; break }
        catch { Start-Sleep 1 }
    }
    $dest = Split-Path -Parent $Exe
    if (Test-Path "$Source\studio\resources") {
        Copy-Item "$Source\studio\resources" $dest -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    $Exe = $built
}

Say "Starting TerraForge"
Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe)
Write-Host "Updated to $(git rev-parse --short HEAD)" -ForegroundColor Green
Start-Sleep 2

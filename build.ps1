# Geekatplay TerraForge — configure + build (Ninja)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Test-Path "external\imgui")) {
    Write-Host "Dependencies missing. Run scripts\get_deps.ps1 first." -ForegroundColor Red
    exit 1
}

# a running instance locks the exe; close it before linking
$running = Get-Process geekatplay_studio -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "Closing running TerraForge instance (pid $($running.Id))..." -ForegroundColor Yellow
    $running.CloseMainWindow() | Out-Null
    if (-not $running.WaitForExit(5000)) { Stop-Process -Id $running.Id -Force }
}

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "`nCONFIGURE FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

cmake --build build
if ($LASTEXITCODE -ne 0) {
    Write-Host "`nBUILD FAILED (exit $LASTEXITCODE) - see the errors above" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "Build complete: build\geekatplay_studio.exe" -ForegroundColor Green
Write-Host "Run tests:      .\test.ps1" -ForegroundColor DarkGray
Write-Host "Start app:      .\start.ps1" -ForegroundColor DarkGray

# Geekatplay TerraForge — configure + build (Ninja, MinGW GCC)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# a running instance locks the exe; close it before linking
$running = Get-Process geekatplay_studio -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "Closing running TerraForge instance (pid $($running.Id))..." -ForegroundColor Yellow
    $running.CloseMainWindow() | Out-Null
    if (-not $running.WaitForExit(5000)) { Stop-Process -Id $running.Id -Force }
}

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
Write-Host "Build complete: build\geekatplay_studio.exe" -ForegroundColor Green
Write-Host "Run tests:      build\engine_tests.exe" -ForegroundColor DarkGray
Write-Host "Start app:      .\start.ps1" -ForegroundColor DarkGray

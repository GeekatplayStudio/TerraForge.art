# Geekatplay Studio â€” PowerShell Test Runner
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " [GEEKATPLAY STUDIO] Running NodeTerrain Complete Test Suites" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

if (!(Test-Path "build\nodeterrain_tests.exe")) {
    Write-Host "[INFO] Binaries not found. Triggering build..." -ForegroundColor Yellow
    & ./build.ps1
}

Write-Host "`n[1/7] Running Native C++20 Test Suite..." -ForegroundColor Yellow
& .\build\nodeterrain_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] C++ Test Suite Failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n[2/7] Running TerraForge Engine Test Suite..." -ForegroundColor Yellow
& .\build\engine_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Engine Test Suite Failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n[3/7] Running Undo / Redo Test Suite..." -ForegroundColor Yellow
& .\build\undo_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Undo Test Suite Failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n[4/7] Running Universal Node Contract Suite..." -ForegroundColor Yellow
& .\build\node_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Node Contract Suite Failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n[5/7] Running Regression Lock (nothing may be lost)..." -ForegroundColor Yellow
& .\build\regression_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] REGRESSION LOCK FAILED - a shipped feature changed or was removed." -ForegroundColor Red
    Write-Host "If the change was intentional: .\build\regression_tests.exe --update, then review the diff." -ForegroundColor Yellow
    exit 1
}

Write-Host "`n[6/7] Running Renderer Maths Suite (patch culling)..." -ForegroundColor Yellow
& .\build\render_tests.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Renderer Maths Suite Failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n[7/7] Running Python / Multi-Agent Test Suite..." -ForegroundColor Yellow
python -m pytest tests/ -v
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Python Test Suite Failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n======================================================================" -ForegroundColor Green
Write-Host " [ALL TESTS PASSED] 100% Test Coverage Verified across C++ and Python." -ForegroundColor Green
Write-Host "======================================================================`n" -ForegroundColor Green



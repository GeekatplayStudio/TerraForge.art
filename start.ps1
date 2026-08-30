# Geekatplay Studio — launch the native terrain studio
Set-Location $PSScriptRoot
if (-not (Test-Path "build\geekatplay_studio.exe")) {
    Write-Host "Not built yet — running build.ps1 first..." -ForegroundColor Yellow
    & .\build.ps1
}
Start-Process -FilePath "build\geekatplay_studio.exe" -WorkingDirectory "build"

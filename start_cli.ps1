# Geekatplay Studio — CLI Benchmark Runner
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " [GEEKATPLAY STUDIO] NodeTerrain Native C++ CLI Benchmark Runner" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

if (!(Test-Path "build\nodeterrain_cli.exe")) {
    & ./build.ps1
}

$prompt = Read-Host "`nEnter terrain description (press Enter for default)"
if ([string]::IsNullOrWhiteSpace($prompt)) {
    $prompt = "Alpine Glacial Peaks with Cascading Fluvial Gorge and Coniferous EcoSystem"
}

& .\build\nodeterrain_cli.exe --prompt "$prompt" --resolution 512

@echo off
setlocal
echo ======================================================================
echo  [GEEKATPLAY STUDIO] NodeTerrain Native C++ CLI Benchmark Runner
echo ======================================================================

if not exist build\nodeterrain_cli.exe (
    call build.bat
    if %errorlevel% neq 0 exit /b 1
)

set /p PROMPT_INPUT="Enter terrain description (or press Enter for default): "
if "%PROMPT_INPUT%"=="" set PROMPT_INPUT=Alpine Glacial Peaks with Cascading Fluvial Gorge and Coniferous EcoSystem

build\nodeterrain_cli.exe --prompt "%PROMPT_INPUT%" --resolution 512
pause
endlocal

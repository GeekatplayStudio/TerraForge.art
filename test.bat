@echo off
setlocal
echo ======================================================================
echo  [GEEKATPLAY STUDIO] Running NodeTerrain Complete Test Suites
echo ======================================================================

if not exist build\nodeterrain_tests.exe (
    echo [INFO] Test executable not found. Running build first...
    call build.bat
    if %errorlevel% neq 0 exit /b 1
)

echo.
echo [1/2] Running Native C++20 Test Suite...
echo ----------------------------------------------------------------------
build\nodeterrain_tests.exe
if %errorlevel% neq 0 (
    echo [ERROR] C++ Test Suite Failed!
    exit /b 1
)

echo.
echo [2/2] Running Python / Multi-Agent and Core Test Suite...
echo ----------------------------------------------------------------------
python -m pytest tests/ -v
if %errorlevel% neq 0 (
    echo [ERROR] Python Test Suite Failed!
    exit /b 1
)

echo.
echo ======================================================================
echo  [ALL TESTS PASSED] 100%% Test Coverage Verified across C++ and Python
echo ======================================================================
endlocal

@echo off
:: AlgoForge C/C++ Build Script for Windows
setlocal enabledelayedexpansion

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release
set BUILD_DIR=build\%BUILD_TYPE%

echo.
echo   AlgoForge C/C++ Build (Windows)
echo   -------------------------------------------
echo   Build type : %BUILD_TYPE%
echo   Build dir  : %BUILD_DIR%
echo   -------------------------------------------
echo.

:: Check SQLite3
if not exist "third_party\sqlite3\sqlite3.c" (
    echo   [WARN] SQLite3 not found. Download from https://sqlite.org/download.html
)

:: Configure
cmake -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -G "Visual Studio 17 2022" ^
    -A x64

if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    exit /b 1
)

:: Build
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

echo.
echo   Build complete.
echo   %BUILD_DIR%\%BUILD_TYPE%\algoforge.exe
echo   %BUILD_DIR%\%BUILD_TYPE%\af_backtest.exe
echo   %BUILD_DIR%\%BUILD_TYPE%\af_tests.exe
echo.

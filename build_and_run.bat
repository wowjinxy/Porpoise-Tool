@echo off
REM Porpoise Tool - Build and Run Script for Windows
REM 
REM This script builds Porpoise Tool and optionally runs it

echo ============================================
echo    Porpoise Tool - Build Script
echo ============================================
echo.

REM Check if make is available
where make >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Error: 'make' command not found
    echo Please install make or compile manually:
    echo   gcc -Iinclude -Wall -Wextra -std=c99 -O2 -o bin/porpoise_tool.exe src/porpoise_tool.c
    pause
    exit /b 1
)

REM Build
echo Building Porpoise Tool...
make

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo ============================================
echo    Build Successful!
echo ============================================
echo.
echo Binary: bin\porpoise_tool.exe
echo.
echo Usage:
echo   bin\porpoise_tool.exe ^<directory^> [skip_list.txt]
echo.
echo Example:
echo   bin\porpoise_tool.exe "Test Asm"
echo   bin\porpoise_tool.exe "Test Asm" skip_functions_example.txt
echo.

REM Check if user provided arguments
if "%~1"=="" (
    echo No arguments provided.
    echo.
    choice /C YN /M "Do you want to run on 'Test Asm' directory"
    if errorlevel 2 goto end
    if errorlevel 1 goto run_test
) else (
    echo Running on: %~1
    bin\porpoise_tool.exe %*
    goto end
)

:run_test
if exist "Test Asm" (
    echo.
    echo Running Porpoise Tool on "Test Asm"...
    echo.
    bin\porpoise_tool.exe "Test Asm"
) else (
    echo "Test Asm" directory not found.
    echo Create it and add .s files to transpile.
)

:end
echo.
pause


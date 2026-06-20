@echo off
if "%1"=="quiet" (
    set QUIET_MODE=1
) else (
    echo ========================================
    echo Gen2 Build and Deployment Script
    echo ========================================
    echo.
)

set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
set PROJECT_DIR=%~dp0

REM ── Auto-detect Qt ────────────────────────────────────────────────────────────
set QT_DIR=
for %%i in (6.8.1 6.8.2 6.9.1 6.11.1 6.11.0 6.7.1 6.6.1) do (
    if exist "C:\Qt\%%i\mingw_64\" (
        set QT_DIR=C:\Qt\%%i\mingw_64
        echo Found Qt at: C:\Qt\%%i\mingw_64
        goto :found_qt
    )
)
echo ERROR: Qt not found. Install Qt 6.x with MinGW to C:\Qt
pause & exit /b 1
:found_qt

REM ── Auto-detect CMake ─────────────────────────────────────────────────────────
if exist "C:\Qt\Tools\CMake\bin\cmake.exe" (
    set CMAKE_PATH=C:\Qt\Tools\CMake\bin\cmake.exe
) else if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" (
    set CMAKE_PATH=C:\Qt\Tools\CMake_64\bin\cmake.exe
) else if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set CMAKE_PATH=C:\Program Files\CMake\bin\cmake.exe
) else (
    set CMAKE_PATH=cmake.exe
)

REM ── Auto-detect MinGW ────────────────────────────────────────────────────────
set MINGW_PATH=
for %%i in (mingw1310_64 mingw1120_64 mingw1020_64) do (
    if exist "C:\Qt\Tools\%%i\bin" (
        set MINGW_PATH=C:\Qt\Tools\%%i\bin
        goto :found_mingw
    )
)
set MINGW_PATH=C:\Qt\Tools\mingw1310_64\bin
:found_mingw
echo Found MinGW at: %MINGW_PATH%

if not exist "%QT_DIR%"      ( echo ERROR: Qt not found: %QT_DIR%      & pause & exit /b 1 )
if not exist "%MINGW_PATH%"  ( echo ERROR: MinGW not found: %MINGW_PATH% & pause & exit /b 1 )

REM ── Step 1: Clear cached runtime + stale build ───────────────────────────────
echo Step 1: Cleaning previous build...
cd /d "%PROJECT_DIR%"

if exist "%TEMP%\Gen2_runtime" (
    echo Clearing cached runtime: %TEMP%\Gen2_runtime
    rmdir /s /q "%TEMP%\Gen2_runtime"
)

REM Clear stale single-instance lock so first launch after build is never blocked
if exist "%TEMP%\Gen2.instance.lock" (
    echo Clearing stale instance lock: %TEMP%\Gen2.instance.lock
    del /f /q "%TEMP%\Gen2.instance.lock"
)

taskkill /f /im cmake.exe        2>nul
taskkill /f /im mingw32-make.exe 2>nul
taskkill /f /im g++.exe          2>nul

timeout /t 2 /nobreak >nul
if exist build_win (
    echo Removing existing build directory...
    rmdir /s /q build_win 2>nul
    if exist build_win echo WARNING: Could not fully remove build_win — continuing anyway...
)
mkdir build_win
cd build_win

REM ── Step 2: CMake configure ──────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 2: Configuring with CMake...
set PATH=%MINGW_PATH%;%PATH%

if defined QUIET_MODE (
    "%CMAKE_PATH%" .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" ^
      -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" ^
      -DCMAKE_MAKE_PROGRAM="%MINGW_PATH%\mingw32-make.exe" ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" >nul 2>&1
) else (
    "%CMAKE_PATH%" .. -G "MinGW Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" ^
      -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" ^
      -DCMAKE_MAKE_PROGRAM="%MINGW_PATH%\mingw32-make.exe" ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%"
)
if %ERRORLEVEL% neq 0 ( echo ERROR: CMake configuration failed! & pause & exit /b 1 )

REM ── Step 3: Build ────────────────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 3: Building application...
if defined QUIET_MODE (
    "%MINGW_PATH%\mingw32-make.exe" >nul 2>&1
) else (
    "%MINGW_PATH%\mingw32-make.exe"
)
if %ERRORLEVEL% neq 0 ( echo ERROR: Build failed! & pause & exit /b 1 )

REM ── Step 4: Deployment folder ────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 4: Creating deployment folder...
cd /d "%PROJECT_DIR%"
if exist manual_deployment rmdir /s /q manual_deployment
mkdir manual_deployment

if exist build_win\bin\GeneticsEditor.exe (
    copy build_win\bin\GeneticsEditor.exe manual_deployment\GeneticsEditor.exe
) else (
    echo ERROR: GeneticsEditor.exe not found in build_win\bin\
    pause & exit /b 1
)

if exist resources xcopy /E /I /Q resources manual_deployment\resources

REM ── Step 5: windeployqt ──────────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 5: Deploying Qt6 dependencies...
if defined QUIET_MODE (
    "%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw manual_deployment\GeneticsEditor.exe >nul 2>&1
) else (
    "%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw manual_deployment\GeneticsEditor.exe
)
if %ERRORLEVEL% neq 0 ( echo ERROR: windeployqt failed! & pause & exit /b 1 )

REM ── Step 6: Remove truly unnecessary files ────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 6: Removing unnecessary files...
if exist manual_deployment\D3Dcompiler_47.dll del manual_deployment\D3Dcompiler_47.dll
if exist manual_deployment\opengl32sw.dll   del manual_deployment\opengl32sw.dll

REM ── Step 7: NSIS single-exe packaging ────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 7: Building single portable Gen2.exe with NSIS...
cd /d "%PROJECT_DIR%"

set NSIS_PATH=
if exist "C:\Program Files\NSIS\makensis.exe"     set NSIS_PATH=C:\Program Files\NSIS\makensis.exe
if exist "C:\Program Files (x86)\NSIS\makensis.exe" set NSIS_PATH=C:\Program Files (x86)\NSIS\makensis.exe

if not defined NSIS_PATH (
    echo WARNING: NSIS not found - skipping portable exe build
    echo Install NSIS from https://nsis.sourceforge.io to enable single-exe packaging
    goto :skip_nsis
)

set GEN2_VERSION_FULL=unknown
for /f "tokens=3" %%v in ('findstr /c:"#define GEN2_VERSION_FULL " "%PROJECT_DIR%include\version_generated.h"') do set GEN2_VERSION_FULL=%%v
set GEN2_VERSION_FULL=%GEN2_VERSION_FULL:"=%
if not defined QUIET_MODE echo Packaging version: %GEN2_VERSION_FULL%

"%NSIS_PATH%" /DVERSION=%GEN2_VERSION_FULL% gen2_launcher.nsi
if %ERRORLEVEL% neq 0 ( echo ERROR: NSIS packaging failed! & goto :skip_nsis )
if not defined QUIET_MODE echo SUCCESS: Single portable exe saved to C:\DSSAT48\Tools\gen2\Gen2.exe

:skip_nsis
if not defined QUIET_MODE (
    echo.
    echo ========================================
    echo SUCCESS: Build and deployment complete!
    echo ========================================
    echo.
    echo Deployment folder: %PROJECT_DIR%manual_deployment
    echo Portable single exe: C:\DSSAT48\Tools\gen2\Gen2.exe
    echo ========================================
    pause
) else (
    echo Build complete. Deployment folder: %PROJECT_DIR%manual_deployment
)

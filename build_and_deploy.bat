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

REM Code signing (optional): set one of these BEFORE running this script to
REM sign the built exe with a REAL trusted-CA certificate. A self-signed cert
REM (what this script used to generate) does NOT help with Defender/AV
REM reputation - no AV or OS trust store honors a self-signed cert, so that
REM step was removed. Leave these unset to build unsigned, same as before.
REM   GEN2_SIGN_CERT           path to a .pfx certificate file
REM   GEN2_SIGN_PASS           password for that .pfx (paired with GEN2_SIGN_CERT)
REM   GEN2_SIGN_THUMBPRINT     SHA1 thumbprint of a cert in the Windows cert
REM                            store (use this for an EV cert on a hardware
REM                            token, which can't be exported as a .pfx)
REM   GEN2_SIGN_TIMESTAMP_URL  optional, defaults to DigiCert's RFC3161 server
REM Requires signtool.exe (from the Windows SDK) on PATH or in its usual
REM Windows Kits install location.

REM Gen2 is linked against a statically-built Qt (no Qt6*.dll, no wrapper/
REM extraction step at build or run time — see CMakeLists.txt's GEN2_STATIC_QT
REM option). This is the same static Qt built for GB2CPP, shared since both
REM only need Core+Widgets (Gen2) / Core+Widgets+Charts (GB2) from the same
REM qtbase build. Not part of the normal Qt online installer; see
REM C:\Qt6Static for the qtbase source/build trees used to produce it.
set STATIC_QT_DIR=C:\Qt6Static\install
set CMAKE_PATH=
set MINGW_PATH=
set NINJA_PATH=

if not exist "%STATIC_QT_DIR%\lib\cmake\Qt6" (
    echo ERROR: Static Qt install not found at %STATIC_QT_DIR%
    echo This build requires a Qt6 built with -static ^(qtbase, matching the
    echo MinGW toolchain below^). It is not part of the normal Qt online
    echo installer and must be built from source once. See GB2CPP's memory
    echo notes / build_and_deploy.bat for the configure command used.
    pause
    exit /b 1
)

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

REM ── Auto-detect Ninja (used as the CMake generator for the static build) ──────
if exist "C:\Qt\Tools\Ninja\ninja.exe" (
    set NINJA_PATH=C:\Qt\Tools\Ninja\ninja.exe
) else (
    set NINJA_PATH=ninja.exe
)

REM ── Auto-detect MinGW - must match the toolchain the static Qt was built with ──
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

if not exist "%CMAKE_PATH%"  ( echo ERROR: CMake not found: %CMAKE_PATH%   & pause & exit /b 1 )
if not exist "%MINGW_PATH%"  ( echo ERROR: MinGW not found: %MINGW_PATH%  & pause & exit /b 1 )

REM ── Step 1: Clear cached runtime + stale build ───────────────────────────────
echo Step 1: Cleaning previous build...
cd /d "%PROJECT_DIR%"

REM Clear stale single-instance lock so first launch after build is never blocked
if exist "%TEMP%\Gen2.instance.lock" (
    echo Clearing stale instance lock: %TEMP%\Gen2.instance.lock
    del /f /q "%TEMP%\Gen2.instance.lock"
)

taskkill /f /im cmake.exe          2>nul
taskkill /f /im GeneticsEditor.exe 2>nul

timeout /t 2 /nobreak >nul
if exist build_win (
    echo Removing existing build directory...
    rmdir /s /q build_win 2>nul
    if exist build_win echo WARNING: Could not fully remove build_win — continuing anyway...
)
mkdir build_win
cd build_win

REM ── Step 2: CMake configure (static Qt) ──────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 2: Configuring with CMake ^(static Qt^)...
set PATH=%MINGW_PATH%;%PATH%

if defined QUIET_MODE (
    "%CMAKE_PATH%" .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DGEN2_STATIC_QT=ON -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%" -DCMAKE_PREFIX_PATH="%STATIC_QT_DIR%" >nul 2>&1
) else (
    "%CMAKE_PATH%" .. -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DGEN2_STATIC_QT=ON ^
      -DCMAKE_CXX_COMPILER="%MINGW_PATH%\g++.exe" ^
      -DCMAKE_C_COMPILER="%MINGW_PATH%\gcc.exe" ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%" ^
      -DCMAKE_PREFIX_PATH="%STATIC_QT_DIR%"
)
if %ERRORLEVEL% neq 0 ( echo ERROR: CMake configuration failed! & pause & exit /b 1 )

REM ── Step 3: Build ────────────────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 3: Building application...
if defined QUIET_MODE (
    "%CMAKE_PATH%" --build . >nul 2>&1
) else (
    "%CMAKE_PATH%" --build .
)
if %ERRORLEVEL% neq 0 ( echo ERROR: Build failed! & pause & exit /b 1 )

REM ── Step 4: Deployment folder ────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 4: Creating deployment folder...
cd /d "%PROJECT_DIR%"
if exist manual_deployment rmdir /s /q manual_deployment
mkdir manual_deployment

REM Statically linked - no Qt6*.dll / MinGW runtime DLLs needed
if exist build_win\bin\GeneticsEditor.exe (
    copy build_win\bin\GeneticsEditor.exe manual_deployment\Gen2.exe
) else (
    echo ERROR: GeneticsEditor.exe not found in build_win\bin\
    pause & exit /b 1
)

call :sign_exe "%PROJECT_DIR%manual_deployment\Gen2.exe"

if exist resources xcopy /E /I /Q resources manual_deployment\resources

REM ── Step 5: Deploy directly ───────────────────────────────────────────────────
if not defined QUIET_MODE echo.
if not defined QUIET_MODE echo Step 5: Deploying directly to C:\DSSAT48\Tools\gen2\...
REM No windeployqt, no NSIS wrapper: Gen2.exe is fully self-contained (static
REM Qt + static MinGW runtime), so deployment is just "copy the exe and its
REM small resources folder to the install location."
if exist "C:\DSSAT48\Tools\gen2" (
    del /f /q "C:\DSSAT48\Tools\gen2\*.*" 2>nul
    for /d %%d in ("C:\DSSAT48\Tools\gen2\*") do rmdir /s /q "%%d" 2>nul
) else (
    mkdir "C:\DSSAT48\Tools\gen2"
)
xcopy /E /I /Y manual_deployment\* "C:\DSSAT48\Tools\gen2\" >nul
if %ERRORLEVEL% neq 0 ( echo ERROR: Deployment to C:\DSSAT48\Tools\gen2 failed! & pause & exit /b 1 )
if not defined QUIET_MODE echo SUCCESS: Deployed to C:\DSSAT48\Tools\gen2\Gen2.exe

if not defined QUIET_MODE (
    echo.
    echo ========================================
    echo SUCCESS: Build and deployment complete!
    echo ========================================
    echo.
    echo Deployment folder: %PROJECT_DIR%manual_deployment
    echo Installed at: C:\DSSAT48\Tools\gen2\Gen2.exe
    echo ========================================
    pause
) else (
    echo Build complete. Deployment folder: %PROJECT_DIR%manual_deployment
)

exit /b 0

REM ============================================================
REM :sign_exe "path\to\file.exe"
REM No-op unless GEN2_SIGN_CERT or GEN2_SIGN_THUMBPRINT is set (see top of file).
REM ============================================================
:sign_exe
if not defined GEN2_SIGN_CERT if not defined GEN2_SIGN_THUMBPRINT (
    if not defined QUIET_MODE echo Skipping code signing for %~1 ^(set GEN2_SIGN_CERT or GEN2_SIGN_THUMBPRINT to enable^)
    exit /b 0
)

if not defined SIGNTOOL_PATH (
    where signtool.exe >nul 2>&1
    if not errorlevel 1 (
        set SIGNTOOL_PATH=signtool.exe
    ) else (
        for /f "delims=" %%s in ('dir /b /s "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" 2^>nul') do if not defined SIGNTOOL_PATH set SIGNTOOL_PATH=%%s
    )
)
if not defined SIGNTOOL_PATH (
    echo WARNING: signtool.exe not found ^(install the Windows SDK^) - cannot sign %~1
    exit /b 0
)

if not defined GEN2_SIGN_TIMESTAMP_URL set GEN2_SIGN_TIMESTAMP_URL=http://timestamp.digicert.com

if not defined QUIET_MODE echo Signing %~1 ...
if defined GEN2_SIGN_THUMBPRINT (
    "%SIGNTOOL_PATH%" sign /sha1 %GEN2_SIGN_THUMBPRINT% /fd SHA256 /tr "%GEN2_SIGN_TIMESTAMP_URL%" /td SHA256 "%~1"
) else (
    "%SIGNTOOL_PATH%" sign /f "%GEN2_SIGN_CERT%" /p "%GEN2_SIGN_PASS%" /fd SHA256 /tr "%GEN2_SIGN_TIMESTAMP_URL%" /td SHA256 "%~1"
)
if errorlevel 1 (
    echo WARNING: Signing failed for %~1
) else (
    if not defined QUIET_MODE echo Signed: %~1
)
exit /b 0

@echo off
setlocal

:: ── Paths ────────────────────────────────────────────────────────────────────
set QT_DIR=C:\Qt\6.9.1\mingw_64
set CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set MINGW_DIR=C:\Qt\Tools\mingw1310_64\bin
set BUILD_DIR=%~dp0build_win

:: Add MinGW to PATH so cmake/ninja can find g++, etc.
set PATH=%MINGW_DIR%;%PATH%

:: ── Configure ────────────────────────────────────────────────────────────────
echo Configuring...
"%CMAKE_EXE%" -S "%~dp0." -B "%BUILD_DIR%" ^
  -G "MinGW Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
  -DCMAKE_C_COMPILER="%MINGW_DIR%\gcc.exe" ^
  -DCMAKE_CXX_COMPILER="%MINGW_DIR%\g++.exe"

if errorlevel 1 (
    echo CMake configure FAILED.
    pause & exit /b 1
)

:: ── Build ────────────────────────────────────────────────────────────────────
echo Building...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release -- -j%NUMBER_OF_PROCESSORS%

if errorlevel 1 (
    echo Build FAILED.
    pause & exit /b 1
)

echo.
echo Build succeeded.
echo Executable: %BUILD_DIR%\bin\GeneticsEditor.exe

:: ── Copy exe into manual_deployment ──────────────────────────────────────────
echo Copying to manual_deployment...
copy /Y "%BUILD_DIR%\bin\GeneticsEditor.exe" "%~dp0manual_deployment\GeneticsEditor.exe"
if errorlevel 1 (
    echo Warning: could not copy to manual_deployment.
) else (
    echo Done: manual_deployment\GeneticsEditor.exe is ready to distribute.
)

:: ── Launch the application ────────────────────────────────────────────────────
echo Launching GeneticsEditor...
start "" "%BUILD_DIR%\bin\GeneticsEditor.exe"
pause

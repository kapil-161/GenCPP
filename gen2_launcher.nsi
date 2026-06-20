; Gen2 Portable Launcher
; - Skips re-extraction if this exact version is already deployed
; - Forces re-extraction when a new version is bundled (version marker mismatch)
; - Uses ExecWait so terminal waits for Gen2 to finish

!include "FileFunc.nsh"

; Version injected by build_and_deploy.bat via /DVERSION=x.y.z
!ifndef VERSION
  !define VERSION "unknown"
!endif

Name "Gen2"
OutFile "C:\DSSAT48\Tools\gen2\Gen2.exe"
Icon "manual_deployment\resources\final.ico"
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma

Section
    ; Skip extraction only if this exact version marker exists
    IfFileExists "$TEMP\Gen2_runtime\version_${VERSION}.marker" launch

    ; Remove stale runtime before extracting new one
    RMDir /r "$TEMP\Gen2_runtime"

    SetOutPath "$TEMP\Gen2_runtime"
    File "manual_deployment\GeneticsEditor.exe"
    File "manual_deployment\Qt6Core.dll"
    File "manual_deployment\Qt6Gui.dll"
    File "manual_deployment\Qt6Widgets.dll"

    ; MinGW runtime DLLs (present if not fully static-linked)
    IfFileExists "manual_deployment\libgcc_s_seh-1.dll"  0 +2
        File "manual_deployment\libgcc_s_seh-1.dll"
    IfFileExists "manual_deployment\libstdc++-6.dll"     0 +2
        File "manual_deployment\libstdc++-6.dll"
    IfFileExists "manual_deployment\libwinpthread-1.dll" 0 +2
        File "manual_deployment\libwinpthread-1.dll"

    SetOutPath "$TEMP\Gen2_runtime\platforms"
    File "manual_deployment\platforms\qwindows.dll"

    SetOutPath "$TEMP\Gen2_runtime\resources"
    File "manual_deployment\resources\final.ico"

    ; Write version marker so future launches skip re-extraction
    SetOutPath "$TEMP\Gen2_runtime"
    FileOpen $0 "$TEMP\Gen2_runtime\version_${VERSION}.marker" w
    FileClose $0

    launch:
    ${GetParameters} $R0
    ExecWait '"$TEMP\Gen2_runtime\GeneticsEditor.exe" $R0'
SectionEnd

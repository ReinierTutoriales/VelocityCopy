Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "Retired-Sdk-Components.nsh"

!ifndef PAYLOAD_DIR
  !error "PAYLOAD_DIR is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif
!ifndef DISPLAY_VERSION
  !error "DISPLAY_VERSION is required"
!endif
!ifndef PAYLOAD_ARCH
  !error "PAYLOAD_ARCH is required"
!endif

; Installer, uninstaller and application use the same normalized multi-size icon.
; Fail at compilation if staging forgot the application asset.
!if /FileExists "${PAYLOAD_DIR}\Assets\VelocityCopy.ico"
  !define MUI_ICON "${PAYLOAD_DIR}\Assets\VelocityCopy.ico"
  !define MUI_UNICON "${PAYLOAD_DIR}\Assets\VelocityCopy.ico"
!else
  !error "Installer payload is missing Assets\VelocityCopy.ico"
!endif

Name "VelocityCopy"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\VelocityCopy"
BrandingText "VelocityCopy"
VIProductVersion "${DISPLAY_VERSION}"
VIAddVersionKey "ProductName" "VelocityCopy"
VIAddVersionKey "FileDescription" "VelocityCopy Installer"
VIAddVersionKey "CompanyName" "ReinierTutoriales"
VIAddVersionKey "FileVersion" "${DISPLAY_VERSION}"
VIAddVersionKey "ProductVersion" "${DISPLAY_VERSION}"
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
; Without MUI_FINISHPAGE_RUN the finish page has no "launch now" option at
; all: the wizard just closes, autostart is only registered for the NEXT
; login (HKCU\...\Run below), and the person has to go find the Start Menu
; shortcut themselves to run it the first time. Offer to launch immediately
; instead, checked by default like a normal installer.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchVelocityCopyAsUser
!define MUI_FINISHPAGE_RUN_TEXT "$(FinishRunText)"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

; NSIS selects the current user's Windows UI language, including a primary
; language match (es-MX/es-US -> Spanish), and falls back to the first language.
; Keep English first for users whose display language is not supported.
LangString InstallSectionName ${LANG_ENGLISH} "Install VelocityCopy"
LangString FinishRunText ${LANG_ENGLISH} "Start VelocityCopy in the notification area"
LangString FinishRunText ${LANG_SPANISH} "Iniciar VelocityCopy en el área de notificación"
LangString InstallSectionName ${LANG_SPANISH} "Instalar VelocityCopy"
LangString Requires64Bit ${LANG_ENGLISH} "VelocityCopy requires 64-bit Windows."
LangString Requires64Bit ${LANG_SPANISH} "VelocityCopy requiere Windows de 64 bits."
LangString RequiresX64 ${LANG_ENGLISH} "This installer is for x64 Windows. Use VelocityCopy-Setup-ARM64.exe."
LangString RequiresX64 ${LANG_SPANISH} "Este instalador es para Windows x64. Use VelocityCopy-Setup-ARM64.exe."
LangString RequiresARM64 ${LANG_ENGLISH} "This installer is for Windows on ARM. Use VelocityCopy-Setup-x64.exe."
LangString RequiresARM64 ${LANG_SPANISH} "Este instalador es para Windows ARM. Use VelocityCopy-Setup-x64.exe."
LangString StartupConfig ${LANG_ENGLISH} "Configuring VelocityCopy startup for the interactive desktop user..."
LangString StartupConfig ${LANG_SPANISH} "Configurando el inicio de VelocityCopy para el usuario de la sesión interactiva..."
LangString StartupSkipped ${LANG_ENGLISH} "No interactive Explorer session; startup registration was skipped."
LangString StartupSkipped ${LANG_SPANISH} "No hay una sesión interactiva de Explorer; se omitió el registro de inicio."
LangString StartupFailed ${LANG_ENGLISH} "Startup registration helper failed with exit code $0: $1"
LangString StartupFailed ${LANG_SPANISH} "Falló el registro de inicio con el código $0: $1"
LangString StartupAbort ${LANG_ENGLISH} "VelocityCopy could not configure startup for the interactive user."
LangString StartupAbort ${LANG_SPANISH} "VelocityCopy no pudo configurar el inicio para el usuario de la sesión interactiva."
LangString ClosingApp ${LANG_ENGLISH} "Closing VelocityCopy if it is running..."
LangString ClosingApp ${LANG_SPANISH} "Cerrando VelocityCopy si está en ejecución..."
LangString ShellLocked ${LANG_ENGLISH} "VelocityCopy.Shell.dll is locked and could not be retired."
LangString ShellLocked ${LANG_SPANISH} "VelocityCopy.Shell.dll está bloqueado y no se pudo retirar."
LangString ShellAbort ${LANG_ENGLISH} "VelocityCopy.Shell.dll is in use and was not replaced."
LangString ShellAbort ${LANG_SPANISH} "VelocityCopy.Shell.dll está en uso y no se reemplazó."
LangString HelperMissing ${LANG_ENGLISH} "Startup helper is unavailable; no elevated HKCU fallback is used."
LangString HelperMissing ${LANG_SPANISH} "El asistente de inicio no está disponible; no se modifica HKCU desde el instalador elevado."

; Some NSIS packages omit the LogicLib ${IsARM64} helper. Detect native ARM64
; with IsWow64Process2 (IMAGE_FILE_MACHINE_ARM64 = 0xAA64 = 43620) instead.
Function .onInit
  SetRegView 64
  System::Call "kernel32::GetCurrentProcess()p.r0"
  System::Call "kernel32::IsWow64Process2(pr0,*i.r1,*i.r2)i.r3"
!if "${PAYLOAD_ARCH}" == "x64"
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "$(Requires64Bit)"
    Abort
  ${EndIf}
  ${If} $3 <> 0
  ${AndIf} $2 = 43620
    MessageBox MB_ICONSTOP "$(RequiresX64)"
    Abort
  ${EndIf}
!else if "${PAYLOAD_ARCH}" == "ARM64"
  ${If} $3 = 0
  ${OrIf} $2 <> 43620
    MessageBox MB_ICONSTOP "$(RequiresARM64)"
    Abort
  ${EndIf}
!else
  !error "PAYLOAD_ARCH must be x64 or ARM64"
!endif
FunctionEnd

; The installer runs elevated. Exec/ExecShell would inherit that token, so the
; startup helper asks the interactive desktop shell to start VelocityCopy with
; the user's normal token and --startup: it stays resident in the notification
; area instead of leaving a window open after setup. If the shell cannot be
; reached, fall back to explorer.exe (no arguments: opens the window).
Function LaunchVelocityCopyAsUser
  nsExec::ExecToStack '"$INSTDIR\VelocityCopy.StartupHelper.exe" --launch "$INSTDIR\VelocityCopy.WinUI.exe"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    Exec '"$WINDIR\explorer.exe" "$INSTDIR\VelocityCopy.WinUI.exe"'
  ${EndIf}
FunctionEnd

!macro ConfigureInteractiveStartup ACTION
  DetailPrint "$(StartupConfig)"
  nsExec::ExecToStack '"$INSTDIR\VelocityCopy.StartupHelper.exe" --${ACTION} "$INSTDIR\VelocityCopy.WinUI.exe"'
  Pop $0
  Pop $1
  ${If} $0 == 10
    DetailPrint "$(StartupSkipped)"
  ${ElseIf} $0 != 0
    DetailPrint "$(StartupFailed)"
    Abort "$(StartupAbort)"
  ${EndIf}
!macroend

!macro RemoveLegacyShell
  DeleteRegKey HKLM "Software\Classes\*\shell\VelocityCopy.Copy"
  DeleteRegKey HKLM "Software\Classes\*\shell\VelocityCopy.CopyTo"
  DeleteRegKey HKLM "Software\Classes\Directory\shell\VelocityCopy.Copy"
  DeleteRegKey HKLM "Software\Classes\Directory\shell\VelocityCopy.CopyTo"
  DeleteRegKey HKLM "Software\Classes\Directory\shell\VelocityCopy.Paste"
  DeleteRegKey HKLM "Software\Classes\Directory\Background\shell\VelocityCopy.Paste"
  DeleteRegKey HKLM "Software\Classes\Directory\Background\shell\VelocityCopy.Open"
  DeleteRegKey HKLM "Software\Classes\CLSID\{7E1D27A7-BA17-4EEA-9B93-967EE777BD21}"
  DeleteRegKey HKLM "Software\Classes\CLSID\{CBBA1A7E-35B4-4708-9D03-9446D03FC843}"
  DeleteRegKey HKLM "Software\Classes\CLSID\{D0B92E7D-7A23-4C9A-9AE2-2B2A1A6F3A0D}"
  DeleteRegKey HKLM "Software\Classes\CLSID\{A6209C12-10B0-4D25-8BF3-2D3C3E6A7B11}"
!macroend

; Active transfer windows treat WM_CLOSE as cancel-and-retire, while idle windows
; can close independently of the resident tray process. An upgrade or uninstall therefore
; cannot rely on a polite window close to terminate the process. Stop the resident process
; before touching installed binaries. taskkill is part of Windows and nsExec is
; bundled with NSIS; a non-zero result is harmless when no process is running.
!macro CloseRunningApp
  DetailPrint "$(ClosingApp)"
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM VelocityCopy.WinUI.exe /F'
  Sleep 500
!macroend

; A loaded in-process shell DLL cannot be overwritten in place. Windows still
; allows renaming the mapped image, which frees the installed name for the new
; file. The old image stays mapped until the locker exits; Delete /REBOOTOK
; removes that renamed file at reboot if it is still locked. Do not report
; success if the installed name still cannot be replaced.
Function ReleaseLoadedShellDll
  StrCpy $0 "$INSTDIR\VelocityCopy.Shell.dll"
  StrCpy $1 ""
  IfFileExists $0 0 shell_release_done

  ; Generate a unique retirement path for every upgrade. The mapped image may
  ; remain locked until Explorer exits, so a fixed .old filename can collide
  ; with a previous still-mapped generation on the next upgrade.
  GetTempFileName $1 "$INSTDIR"
  IfErrors shell_release_failed
  Delete $1
  ClearErrors
  Rename $0 $1
  IfErrors shell_release_failed
  Goto shell_release_done

shell_release_failed:
  DetailPrint "$(ShellLocked)"
  Abort "$(ShellAbort)"

shell_release_done:
FunctionEnd

Section "$(InstallSectionName)" SEC_INSTALL
  SetRegView 64
  !insertmacro CloseRunningApp
  Call ReleaseLoadedShellDll
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*.*"
  !insertmacro RetireUnusedSdkComponents
  ${If} $1 != ""
    Delete /REBOOTOK "$1"
  ${EndIf}

  Delete "$SMPROGRAMS\VelocityCopy\VelocityCopy.lnk"
  RMDir "$SMPROGRAMS\VelocityCopy"
  CreateDirectory "$SMPROGRAMS\VelocityCopy"
  CreateShortcut "$SMPROGRAMS\VelocityCopy\VelocityCopy.lnk" "$INSTDIR\VelocityCopy.WinUI.exe" "" "$INSTDIR\VelocityCopy.WinUI.exe" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ; Paths longer than 260 characters only work in long-path-aware apps
  ; (VelocityCopy's manifest declares it) once this system policy is on.
  ; Off by default; deep folder trees otherwise fail mid-copy. Left enabled on
  ; uninstall because other applications may rely on it.
  WriteRegDWORD HKLM "SYSTEM\CurrentControlSet\Control\FileSystem" "LongPathsEnabled" 1
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayName" "VelocityCopy"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "Publisher" "ReinierTutoriales"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayIcon" '"$INSTDIR\VelocityCopy.WinUI.exe",0'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "InstallLocation" "$INSTDIR"
  ; Never write HKCU from the elevated installer: alternate administrator
  ; credentials would target the wrong profile. The helper impersonates the
  ; same-session Explorer token before opening the interactive user's HKCU.
  !insertmacro ConfigureInteractiveStartup install

  ; Retire old registrations during upgrades as well as uninstall.
  !insertmacro RemoveLegacyShell
  WriteRegStr HKLM "Software\Classes\CLSID\{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}\InprocServer32" "" "$INSTDIR\VelocityCopy.Shell.dll"
  WriteRegStr HKLM "Software\Classes\CLSID\{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}\InprocServer32" "ThreadingModel" "Apartment"
  WriteRegStr HKLM "Software\Classes\Directory\shellex\DragDropHandlers\VelocityCopy" "" "{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}"
  WriteRegStr HKLM "Software\Classes\Drive\shellex\DragDropHandlers\VelocityCopy" "" "{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}"
  WriteRegStr HKLM "Software\Classes\Folder\shellex\DragDropHandlers\VelocityCopy" "" "{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" "{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}" "VelocityCopy transfer handler"
  ; Notify associations. A loaded old COM DLL may still require sign-out before upgrade.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetRegView 64
  !insertmacro CloseRunningApp
  Delete "$SMPROGRAMS\VelocityCopy\VelocityCopy.lnk"
  RMDir "$SMPROGRAMS\VelocityCopy"
  ${If} ${FileExists} "$INSTDIR\VelocityCopy.StartupHelper.exe"
    !insertmacro ConfigureInteractiveStartup remove
  ${Else}
    DetailPrint "$(HelperMissing)"
  ${EndIf}

  !insertmacro RemoveLegacyShell
  DeleteRegKey HKLM "Software\Classes\Directory\shellex\DragDropHandlers\VelocityCopy"
  DeleteRegKey HKLM "Software\Classes\Drive\shellex\DragDropHandlers\VelocityCopy"
  DeleteRegKey HKLM "Software\Classes\Folder\shellex\DragDropHandlers\VelocityCopy"
  DeleteRegKey HKLM "Software\Classes\CLSID\{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" "{6BD80C35-7CE8-4A63-92D4-51AF4DACB821}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy"
  RMDir /r "$INSTDIR"
SectionEnd

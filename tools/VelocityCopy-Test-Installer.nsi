Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

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
!define MUI_FINISHPAGE_RUN "$INSTDIR\VelocityCopy.WinUI.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

; Some NSIS packages omit the LogicLib ${IsARM64} helper. Detect native ARM64
; with IsWow64Process2 (IMAGE_FILE_MACHINE_ARM64 = 0xAA64 = 43620) instead.
Function .onInit
  SetRegView 64
  System::Call "kernel32::GetCurrentProcess()p.r0"
  System::Call "kernel32::IsWow64Process2(pr0,*i.r1,*i.r2)i.r3"
!if "${PAYLOAD_ARCH}" == "x64"
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "VelocityCopy requires 64-bit Windows."
    Abort
  ${EndIf}
  ${If} $3 <> 0
  ${AndIf} $2 = 43620
    MessageBox MB_ICONSTOP "This installer is for x64 Windows. Use VelocityCopy-Setup-ARM64.exe."
    Abort
  ${EndIf}
!else if "${PAYLOAD_ARCH}" == "ARM64"
  ${If} $3 = 0
  ${OrIf} $2 <> 43620
    MessageBox MB_ICONSTOP "This installer is for Windows on ARM. Use VelocityCopy-Setup-x64.exe."
    Abort
  ${EndIf}
!else
  !error "PAYLOAD_ARCH must be x64 or ARM64"
!endif
FunctionEnd

; The installer is elevated because it writes Program Files and HKLM. Launching
; the application directly from the finish page would therefore leak the
; installer's administrator token into the normal desktop application. Ask
; Explorer (the unelevated shell) to perform the launch instead.
Function LaunchVelocityCopyAsUser
  System::Call 'shell32::SHGetFolderPathW(p 0, i 0x0000, p 0, i 0, w .r0)i.r1'
  ${If} $1 = 0
    GetFullPathName $2 "$INSTDIR\VelocityCopy.WinUI.exe"
    ExecShell "open" "$2"
  ${Else}
    MessageBox MB_ICONEXCLAMATION|MB_OK "VelocityCopy was installed successfully, but Setup could not start it automatically. Launch it from the Start menu."
  ${EndIf}
FunctionEnd

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

; VelocityCopy deliberately treats WM_CLOSE as hide-to-tray, so an upgrade or
; uninstall cannot rely on a polite window close. Stop the resident process
; before touching installed binaries. taskkill is part of Windows and nsExec is
; bundled with NSIS; a non-zero result is harmless when no process is running.
!macro CloseRunningApp
  DetailPrint "Closing VelocityCopy if it is running..."
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM VelocityCopy.WinUI.exe /F'
  Sleep 500
!macroend

Section "Install VelocityCopy" SEC_INSTALL
  SetRegView 64
  !insertmacro CloseRunningApp
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*.*"

  CreateDirectory "$SMPROGRAMS\VelocityCopy"
  CreateShortcut "$SMPROGRAMS\VelocityCopy\VelocityCopy.lnk" "$INSTDIR\VelocityCopy.WinUI.exe" "" "$INSTDIR\VelocityCopy.WinUI.exe" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayName" "VelocityCopy"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "Publisher" "ReinierTutoriales"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayIcon" "$INSTDIR\VelocityCopy.WinUI.exe,0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "InstallLocation" "$INSTDIR"
  ; Installer owns startup registration. Runtime must never create/repair this value.
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "VelocityCopy" '"$INSTDIR\VelocityCopy.WinUI.exe" --startup'

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
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "VelocityCopy"

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

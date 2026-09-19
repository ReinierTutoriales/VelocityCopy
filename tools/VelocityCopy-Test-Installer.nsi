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
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetRegView 64
  ${If} "${PAYLOAD_ARCH}" == "x64"
    ${IfNot} ${RunningX64}
      MessageBox MB_ICONSTOP "VelocityCopy requires 64-bit Windows."
      Abort
    ${EndIf}
    ${If} ${IsARM64}
      MessageBox MB_ICONSTOP "This installer is for x64 Windows. Use VelocityCopy-Setup-ARM64.exe."
      Abort
    ${EndIf}
  ${ElseIf} "${PAYLOAD_ARCH}" == "ARM64"
    ${IfNot} ${IsARM64}
      MessageBox MB_ICONSTOP "This installer is for Windows on ARM. Use VelocityCopy-Setup-x64.exe."
      Abort
    ${EndIf}
  ${Else}
    MessageBox MB_ICONSTOP "Unknown VelocityCopy installer architecture."
    Abort
  ${EndIf}
FunctionEnd

Section "Install VelocityCopy" SEC_INSTALL
  SetRegView 64
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

  ; Classic unpackaged Explorer integration. The in-process COM server implements
  ; IExplorerCommand; keep registration machine-wide because the installer is elevated.
  WriteRegStr HKLM "Software\Classes\CLSID\{7E1D27A7-BA17-4EEA-9B93-967EE777BD21}\InprocServer32" "" "$INSTDIR\VelocityCopy.Shell.dll"
  WriteRegStr HKLM "Software\Classes\CLSID\{7E1D27A7-BA17-4EEA-9B93-967EE777BD21}\InprocServer32" "ThreadingModel" "Apartment"
  WriteRegStr HKLM "Software\Classes\CLSID\{CBBA1A7E-35B4-4708-9D03-9446D03FC843}\InprocServer32" "" "$INSTDIR\VelocityCopy.Shell.dll"
  WriteRegStr HKLM "Software\Classes\CLSID\{CBBA1A7E-35B4-4708-9D03-9446D03FC843}\InprocServer32" "ThreadingModel" "Apartment"
  WriteRegStr HKLM "Software\Classes\CLSID\{D0B92E7D-7A23-4C9A-9AE2-2B2A1A6F3A0D}\InprocServer32" "" "$INSTDIR\VelocityCopy.Shell.dll"
  WriteRegStr HKLM "Software\Classes\CLSID\{D0B92E7D-7A23-4C9A-9AE2-2B2A1A6F3A0D}\InprocServer32" "ThreadingModel" "Apartment"
  WriteRegStr HKLM "Software\Classes\CLSID\{A6209C12-10B0-4D25-8BF3-2D3C3E6A7B11}\InprocServer32" "" "$INSTDIR\VelocityCopy.Shell.dll"
  WriteRegStr HKLM "Software\Classes\CLSID\{A6209C12-10B0-4D25-8BF3-2D3C3E6A7B11}\InprocServer32" "ThreadingModel" "Apartment"

  ; Selection commands: files and folders.
  WriteRegStr HKLM "Software\Classes\*\shell\VelocityCopy.Copy" "ExplorerCommandHandler" "{7E1D27A7-BA17-4EEA-9B93-967EE777BD21}"
  WriteRegStr HKLM "Software\Classes\*\shell\VelocityCopy.CopyTo" "ExplorerCommandHandler" "{D0B92E7D-7A23-4C9A-9AE2-2B2A1A6F3A0D}"
  WriteRegStr HKLM "Software\Classes\Directory\shell\VelocityCopy.Copy" "ExplorerCommandHandler" "{7E1D27A7-BA17-4EEA-9B93-967EE777BD21}"
  WriteRegStr HKLM "Software\Classes\Directory\shell\VelocityCopy.CopyTo" "ExplorerCommandHandler" "{D0B92E7D-7A23-4C9A-9AE2-2B2A1A6F3A0D}"

  ; Paste is meaningful on a folder itself and on an Explorer folder background.
  WriteRegStr HKLM "Software\Classes\Directory\shell\VelocityCopy.Paste" "ExplorerCommandHandler" "{CBBA1A7E-35B4-4708-9D03-9446D03FC843}"
  WriteRegStr HKLM "Software\Classes\Directory\Background\shell\VelocityCopy.Paste" "ExplorerCommandHandler" "{CBBA1A7E-35B4-4708-9D03-9446D03FC843}"

  ; Background entry to open the resident UI explicitly.
  WriteRegStr HKLM "Software\Classes\Directory\Background\shell\VelocityCopy.Open" "ExplorerCommandHandler" "{A6209C12-10B0-4D25-8BF3-2D3C3E6A7B11}"

  ; Notify Explorer that shell associations changed; no Explorer restart is required.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetRegView 64
  Delete "$SMPROGRAMS\VelocityCopy\VelocityCopy.lnk"
  RMDir "$SMPROGRAMS\VelocityCopy"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "VelocityCopy"

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
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy"
  RMDir /r "$INSTDIR"
SectionEnd

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

Section "Install VelocityCopy" SEC_INSTALL
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
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\VelocityCopy\VelocityCopy.lnk"
  RMDir "$SMPROGRAMS\VelocityCopy"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy"
  RMDir /r "$INSTDIR"
SectionEnd

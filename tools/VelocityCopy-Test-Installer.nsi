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
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Section "Install VelocityCopy" SEC_INSTALL
  SetOutPath "$PLUGINSDIR\VelocityCopy"
  File /r "${PAYLOAD_DIR}\*.*"

  Delete "$TEMP\VelocityCopy-Install.log"
  DetailPrint "Installing VelocityCopy and required Windows runtimes..."
  ${If} ${RunningX64}
    ${DisableX64FSRedirection}
  ${EndIf}
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\VelocityCopy\Install-VelocityCopy-Test.ps1" -LogPath "$TEMP\VelocityCopy-Install.log"'
  Pop $0
  ${If} ${RunningX64}
    ${EnableX64FSRedirection}
  ${EndIf}
  ${If} $0 != 0
    DetailPrint "VelocityCopy deployment failed with exit code $0."
    DetailPrint "Diagnostic log: $TEMP\VelocityCopy-Install.log"
    MessageBox MB_ICONSTOP|MB_OK "VelocityCopy installation failed.$\r$\n$\r$\nDiagnostic log:$\r$\n$TEMP\VelocityCopy-Install.log" /SD IDOK
    SetErrorLevel $0
    Quit
  ${EndIf}

  SetOutPath "$INSTDIR\InstallerSupport"
  File "/oname=Install-VelocityCopy-Test.ps1" "${PAYLOAD_DIR}\Install-VelocityCopy-Test.ps1"
  File "/oname=VelocityCopy-Test.cer" "${PAYLOAD_DIR}\VelocityCopy-Test.cer"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayName" "VelocityCopy"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "Publisher" "ReinierTutoriales"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy" "NoRepair" 1

  DetailPrint "VelocityCopy installation completed."
SectionEnd

Section "Uninstall"
  DetailPrint "Removing VelocityCopy..."
  Delete "$TEMP\VelocityCopy-Install.log"
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\InstallerSupport\Install-VelocityCopy-Test.ps1" -Uninstall -LogPath "$TEMP\VelocityCopy-Install.log"'
  Pop $0
  ${If} $0 != 0
    DetailPrint "VelocityCopy uninstall failed with exit code $0."
    DetailPrint "Diagnostic log: $TEMP\VelocityCopy-Install.log"
    MessageBox MB_ICONSTOP|MB_OK "VelocityCopy uninstall failed.$\r$\n$\r$\nDiagnostic log:$\r$\n$TEMP\VelocityCopy-Install.log" /SD IDOK
    SetErrorLevel $0
    Quit
  ${EndIf}

  Delete "$INSTDIR\InstallerSupport\Install-VelocityCopy-Test.ps1"
  Delete "$INSTDIR\InstallerSupport\VelocityCopy-Test.cer"
  RMDir "$INSTDIR\InstallerSupport"
  Delete "$INSTDIR\Uninstall.exe"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelocityCopy"
  RMDir "$INSTDIR"
SectionEnd

Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef PAYLOAD_DIR
  !error "PAYLOAD_DIR is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif

Name "VelocityCopy"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\VelocityCopy"
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

  DetailPrint "Installing VelocityCopy and required Windows runtimes..."
  nsExec::ExecToLog '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\VelocityCopy\Install-VelocityCopy-Test.ps1"'
  Pop $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "VelocityCopy installation failed (exit code $0). See the installer details for the failing step."
    Abort
  ${EndIf}

  DetailPrint "VelocityCopy installation completed."
SectionEnd

Unicode true

!ifndef CASTWEAVE_VERSION
  !error "CASTWEAVE_VERSION is required"
!endif
!ifndef CASTWEAVE_STAGE
  !error "CASTWEAVE_STAGE is required"
!endif
!ifndef CASTWEAVE_OUTPUT
  !error "CASTWEAVE_OUTPUT is required"
!endif

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"
!include "x64.nsh"
${StrStr}
${UnStrStr}

!define PRODUCT "CastWeave"
!define PUBLISHER "VodzGaming"
!define PLUGIN_SUBDIR "obs-studio\plugins\castweave"
!define SUPPORT_ROOT "$PROGRAMFILES64\${PUBLISHER}\${PRODUCT}"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\CastWeave"

Name "${PRODUCT} ${CASTWEAVE_VERSION}"
OutFile "${CASTWEAVE_OUTPUT}"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES64\${PUBLISHER}\${PRODUCT}\plugin"

VIProductVersion "${CASTWEAVE_VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "FileDescription" "${PRODUCT} OBS plugin installer"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 ${PUBLISHER}"
VIAddVersionKey "FileVersion" "${CASTWEAVE_VERSION}"
VIAddVersionKey "ProductVersion" "${CASTWEAVE_VERSION}"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install CastWeave for OBS Studio"
!define MUI_WELCOMEPAGE_TEXT "CastWeave adds native chat and stream-management docks to OBS Studio.$\r$\n$\r$\nOBS must be closed before installation. Your CastWeave account and preference data are stored separately and remain available after updates."
!define MUI_FINISHPAGE_TITLE "CastWeave is ready"
!define MUI_FINISHPAGE_TEXT "Open OBS Studio, then enable CastWeave Chat and CastWeave Streams from the Docks menu."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open OBS Studio"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchObs
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function EnsureObsClosed
  nsExec::ExecToStack '"$SYSDIR\tasklist.exe" /FI "IMAGENAME eq obs64.exe" /FO CSV /NH'
  Pop $0
  Pop $1
  ${StrStr} $2 $1 "obs64.exe"
  ${If} $2 != ""
    MessageBox MB_ICONSTOP "Close OBS Studio completely, then try again."
    Abort
  ${EndIf}
FunctionEnd

Function LaunchObs
  SetRegView 32
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio" "DisplayIcon"
  SetRegView 64
  ${If} $0 == ""
    StrCpy $0 "$PROGRAMFILES64\obs-studio\bin\64bit\obs64.exe"
  ${EndIf}
  ${If} ${FileExists} "$0"
    ${GetParent} $1 "$0"
    SetOutPath "$1"
    ExecShell "open" "$0"
  ${Else}
    MessageBox MB_ICONINFORMATION "CastWeave was installed, but OBS Studio could not be found automatically. Open OBS normally to continue."
  ${EndIf}
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "CastWeave requires 64-bit Windows and OBS Studio."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ReadEnvStr $0 "ProgramData"
  ${If} $0 == ""
    MessageBox MB_ICONSTOP "Windows did not provide the shared ProgramData folder."
    Abort
  ${EndIf}
  StrCpy $INSTDIR "$0\${PLUGIN_SUBDIR}"
  Call EnsureObsClosed
FunctionEnd

Section "Install"
  SetShellVarContext all
  SetRegView 64
  SetOutPath "$INSTDIR"
  File /r /x *.pdb "${CASTWEAVE_STAGE}\castweave\*.*"

  SetOutPath "${SUPPORT_ROOT}"
  File "..\README.md"
  File "..\LICENSE"
  WriteUninstaller "${SUPPORT_ROOT}\Uninstall.exe"

  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${CASTWEAVE_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"${SUPPORT_ROOT}\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" '"${SUPPORT_ROOT}\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/zerithvt-Coder/castweave"
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
  ReadEnvStr $0 "ProgramData"
  ${If} $0 == ""
    MessageBox MB_ICONSTOP "Windows did not provide the shared ProgramData folder."
    Abort
  ${EndIf}
  Call un.EnsureObsClosed
FunctionEnd

Function un.EnsureObsClosed
  nsExec::ExecToStack '"$SYSDIR\tasklist.exe" /FI "IMAGENAME eq obs64.exe" /FO CSV /NH'
  Pop $0
  Pop $1
  ${UnStrStr} $2 $1 "obs64.exe"
  ${If} $2 != ""
    MessageBox MB_ICONSTOP "Close OBS Studio completely, then try again."
    Abort
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 64
  ReadEnvStr $0 "ProgramData"
  RMDir /r "$0\${PLUGIN_SUBDIR}"
  RMDir /r "${SUPPORT_ROOT}"
  RMDir "$PROGRAMFILES64\${PUBLISHER}"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd

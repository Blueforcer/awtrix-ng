Unicode true
ManifestDPIAware true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"

!ifndef VERSION
  !error "makensis /DVERSION=x.y.z /DSTAGE=dir /DOUTFILE=file /DICON=file installer.nsi"
!endif

!define PRODUCT "AWTRIX Script Host"
!define PROGRAM "AWTRIX Script Host.exe"
!define WINDOW "AWTRIXScriptHostWindow"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\AWTRIXScriptHost"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define APPROVED_KEY "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"
!define RULE "AWTRIX Script Host"

Name "${PRODUCT}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${PRODUCT} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "CompanyName" "AWTRIX NG"
VIAddVersionKey "LegalCopyright" "Stephan Muehl (Blueforcer)"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Setup installs AWTRIX Script Host. It runs the scripts of your AWTRIX clocks on this computer.$\r$\n$\r$\nIt starts when you sign in and shows an icon in the notification area. Click the icon for the pairing code, or to quit."
!define MUI_FINISHPAGE_TEXT "Pair your clock in its web UI under System > Script host. The pairing code is in the menu of the AWTRIX Script Host icon."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Start AWTRIX Script Host"
!define MUI_FINISHPAGE_RUN_FUNCTION StartProgram

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\license-page.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var Data

!macro Run command
  nsExec::ExecToLog `${command}`
  Pop $0
!macroend

!macro DeleteRules
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (pairing)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (discovery)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (scripts)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (display)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (wake-up)"'
!macroend

!macro AddRule name program protocol ports
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="${RULE} (${name})" dir=in action=allow program="${program}" protocol=${protocol} localport=${ports} remoteip=localsubnet profile=any enable=yes'
!macroend

!macro Busy file
  ${If} ${FileExists} "${file}"
    ClearErrors
    FileOpen $2 "${file}" a
    ${If} ${Errors}
      StrCpy $0 1
    ${Else}
      FileClose $2
    ${EndIf}
  ${EndIf}
!macroend

!macro CloseProgram un
  Function ${un}CloseProgram
    StrCpy $1 0
    ${Do}
      FindWindow $0 "${WINDOW}"
      ${If} $0 = 0
      ${OrIf} $1 >= 2
        ${Break}
      ${EndIf}
      DetailPrint "Quitting the running ${PRODUCT}"
      SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=10000
      IntOp $1 $1 + 1
    ${Loop}
    StrCpy $1 0
    ${Do}
      StrCpy $0 0
      !insertmacro Busy "$INSTDIR\${PROGRAM}"
      !insertmacro Busy "$INSTDIR\awtrix-host.exe"
      !insertmacro Busy "$INSTDIR\awtrix-script-runner.exe"
      ${If} $0 = 0
        ${Break}
      ${EndIf}
      ${If} $1 < 60
        Sleep 250
        IntOp $1 $1 + 1
        ${Continue}
      ${EndIf}
      ${IfNot} ${Cmd} `MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${PRODUCT} is still running, maybe for another user. Quit it there, then click Retry." /SD IDCANCEL IDRETRY`
        Abort
      ${EndIf}
      StrCpy $1 0
    ${Loop}
  FunctionEnd
!macroend
!insertmacro CloseProgram ""
!insertmacro CloseProgram "un."

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${PRODUCT} needs 64-bit Windows 10 or 11."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Function StartProgram
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${PROGRAM}"'
FunctionEnd

Section "Install"
  Call CloseProgram

  SetOutPath "$INSTDIR"
  File "${STAGE}\${PROGRAM}"
  File "${STAGE}\awtrix-host.exe"
  File "${STAGE}\awtrix-script-runner.exe"
  File "${STAGE}\ca-certificates.crt"
  SetOutPath "$INSTDIR\webui"
  File "${STAGE}\webui\index.html"
  SetOutPath "$INSTDIR\licenses"
  File /r "${STAGE}\licenses\*.*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\${PRODUCT}.lnk" "$INSTDIR\${PROGRAM}" "" "$INSTDIR\${PROGRAM}" 0

  DetailPrint "Allowing clocks on the local network through the firewall"
  !insertmacro DeleteRules
  !insertmacro AddRule "pairing" "$INSTDIR\awtrix-host.exe" "TCP" "8300"
  !insertmacro AddRule "discovery" "$INSTDIR\awtrix-host.exe" "UDP" "4211"
  !insertmacro AddRule "scripts" "$INSTDIR\awtrix-script-runner.exe" "TCP" "8301-8399"
  !insertmacro AddRule "display" "$INSTDIR\awtrix-script-runner.exe" "UDP" "8301-8399"
  !insertmacro AddRule "wake-up" "$INSTDIR\awtrix-host.exe" "UDP" "8301-8399"

  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "AWTRIX NG"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/Blueforcer/awtrix-ng"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${PROGRAM}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $2 $3 $4
  IntFmt $2 "0x%08X" $2
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $2
SectionEnd

Section "Uninstall"
  Call un.CloseProgram
  Delete "$SMPROGRAMS\${PRODUCT}.lnk"
  DeleteRegValue HKCU "${RUN_KEY}" "${PRODUCT}"
  DeleteRegValue HKCU "${APPROVED_KEY}" "${PRODUCT}"
  DeleteRegKey HKCU "Software\${PRODUCT}"
  !insertmacro DeleteRules

  Delete "$INSTDIR\${PROGRAM}"
  Delete "$INSTDIR\awtrix-host.exe"
  Delete "$INSTDIR\awtrix-script-runner.exe"
  Delete "$INSTDIR\ca-certificates.crt"
  RMDir /r "$INSTDIR\webui"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"

  SetShellVarContext current
  StrCpy $Data "$LOCALAPPDATA\${PRODUCT}"
  SetShellVarContext all
  ${If} ${FileExists} "$Data\*.*"
  ${AndIfNot} ${Silent}
  ${AndIf} ${Cmd} `MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete the paired clocks and their scripts?$\r$\n$\r$\nChoose No to keep them for a later installation." IDYES`
    RMDir /r "$Data"
  ${EndIf}
SectionEnd

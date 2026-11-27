Unicode true
ManifestDPIAware true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !error "makensis /DVERSION=x.y.z /DSTAGE=dir /DOUTFILE=file installer.nsi"
!endif

!define PRODUCT "AWTRIX Script Host"
!define SERVICE "AWTRIXScriptHost"
!define ACCOUNT "NT SERVICE\${SERVICE}"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${SERVICE}"
!define STATUS_URL "http://localhost:8300/"
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

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Setup installs the script host for AWTRIX clocks as a Windows service. It runs in the background, also when nobody is signed in.$\r$\n$\r$\nAfterwards, pair your clock in its web UI under System > Script host."
!define MUI_FINISHPAGE_TEXT "The script host is running. The status page shows the pairing code."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open the status page"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenStatusPage

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

!macro StopService
  !insertmacro Run '"$SYSDIR\net.exe" stop ${SERVICE}'
!macroend

!macro DeleteRules
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (pairing)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (discovery)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (scripts)"'
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="${RULE} (display)"'
!macroend

!macro AddRule name program protocol ports
  !insertmacro Run '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="${RULE} (${name})" dir=in action=allow program="${program}" protocol=${protocol} localport=${ports} remoteip=localsubnet profile=any enable=yes'
!macroend

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

Function OpenStatusPage
  Exec '"$WINDIR\explorer.exe" "${STATUS_URL}"'
FunctionEnd

Section "Install"
  StrCpy $Data "$APPDATA\${PRODUCT}"
  DetailPrint "Stopping the running script host, if any"
  !insertmacro StopService

  SetOutPath "$INSTDIR"
  File "${STAGE}\awtrix-host.exe"
  File "${STAGE}\awtrix-script-runner.exe"
  File "${STAGE}\ca-certificates.crt"
  SetOutPath "$INSTDIR\webui"
  File "${STAGE}\webui\index.html"
  SetOutPath "$INSTDIR\licenses"
  File /r "${STAGE}\licenses\*.*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  StrCpy $1 '\"$INSTDIR\awtrix-host.exe\" --state \"$Data\" --binary \"$INSTDIR\awtrix-script-runner.exe\" --webui \"$INSTDIR\webui\index.html\" --ca-file \"$INSTDIR\ca-certificates.crt\"'
  DetailPrint "Registering the service ${SERVICE}"
  !insertmacro Run '"$SYSDIR\sc.exe" query ${SERVICE}'
  ${If} $0 == 0
    !insertmacro Run '"$SYSDIR\sc.exe" config ${SERVICE} binPath= "$1" start= auto obj= "${ACCOUNT}" DisplayName= "${PRODUCT}"'
  ${Else}
    !insertmacro Run '"$SYSDIR\sc.exe" create ${SERVICE} binPath= "$1" start= auto obj= "${ACCOUNT}" DisplayName= "${PRODUCT}"'
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "The service could not be registered (error $0)."
    Abort
  ${EndIf}
  !insertmacro Run '"$SYSDIR\sc.exe" description ${SERVICE} "Runs the scripts of the AWTRIX clocks paired with this computer. Status page: ${STATUS_URL}"'
  !insertmacro Run '"$SYSDIR\sc.exe" sidtype ${SERVICE} unrestricted'
  !insertmacro Run '"$SYSDIR\sc.exe" failure ${SERVICE} reset= 86400 actions= restart/5000/restart/10000/restart/60000'
  !insertmacro Run '"$SYSDIR\sc.exe" failureflag ${SERVICE} 1'

  DetailPrint "Keeping the data folder private"
  CreateDirectory "$Data"
  !insertmacro Run '"$SYSDIR\icacls.exe" "$Data" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F "${ACCOUNT}:(OI)(CI)F" /Q'

  DetailPrint "Allowing clocks on the local network through the firewall"
  !insertmacro DeleteRules
  !insertmacro AddRule "pairing" "$INSTDIR\awtrix-host.exe" "TCP" "8300"
  !insertmacro AddRule "discovery" "$INSTDIR\awtrix-host.exe" "UDP" "4211"
  !insertmacro AddRule "scripts" "$INSTDIR\awtrix-script-runner.exe" "TCP" "8301-8399"
  !insertmacro AddRule "display" "$INSTDIR\awtrix-script-runner.exe" "UDP" "8301-8399"

  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "AWTRIX NG"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/Blueforcer/awtrix-ng"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\awtrix-host.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $2 $3 $4
  IntFmt $2 "0x%08X" $2
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $2

  DetailPrint "Starting the script host"
  !insertmacro Run '"$SYSDIR\sc.exe" start ${SERVICE}'
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION "The script host did not start (error $0). Its log is in $Data\awtrix-host.log."
  ${EndIf}
SectionEnd

Section "Uninstall"
  StrCpy $Data "$APPDATA\${PRODUCT}"
  DetailPrint "Stopping and removing the service"
  !insertmacro StopService
  !insertmacro Run '"$SYSDIR\sc.exe" delete ${SERVICE}'
  !insertmacro DeleteRules

  Delete "$INSTDIR\awtrix-host.exe"
  Delete "$INSTDIR\awtrix-script-runner.exe"
  Delete "$INSTDIR\ca-certificates.crt"
  RMDir /r "$INSTDIR\webui"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"

  IfSilent keep
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete the paired clocks and their scripts?$\r$\n$\r$\nChoose No to keep them for a later installation." IDNO keep
  !insertmacro Run '"$SYSDIR\cmd.exe" /c rmdir /s /q "$Data"'
  keep:
SectionEnd

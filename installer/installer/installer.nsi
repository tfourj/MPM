;================================
; MPM - MQTT Power Manager - NSIS Script
;================================
; Based on the Qt Installer Template. Adds Windows service setup and
; in-place upgrades of an existing installation in any folder.
;================================

Unicode true
ManifestDPIAware true

;--------------------------------
; Use Modern UI 2
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "x64.nsh"
!include "nsProcess.nsh"

;--------------------------------
; Include version information (generated during build)
!include "version.nsh"

;--------------------------------
; Configuration
!define APPNAME "MPM"
!define COMPANYNAME "tfourj"
!define DESCRIPTION "MQTT Power Manager"
!define HELPURL "https://github.com/tfourj/MPM"
!define UPDATEURL "https://github.com/tfourj/MPM/releases"
!define ABOUTURL "https://github.com/tfourj/MPM"
!define SERVICENAME "MPMService"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
!define SERVICEKEY "SYSTEM\CurrentControlSet\Services\${SERVICENAME}"

Name "${APPNAME} - ${DESCRIPTION}"
OutFile "${APPNAME}-Setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
RequestExecutionLevel admin

!define MUI_ICON "mpm.ico"
!define MUI_UNICON "mpm.ico"

;--------------------------------
; Start Menu page settings
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\${APPNAME}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "${APPNAME}"

Var StartMenuFolder
Var OldInstDir ; folder of an existing installation, empty if none was found

;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_DIRECTORYPAGE_TEXT_TOP "Setup will install ${APPNAME} in the following folder. An existing installation is preselected and upgraded in place. Settings in ProgramData are kept."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES

; Finish page with "Run" checkbox and "Create Desktop Shortcut"
Function finishpageaction
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPNAME}.exe"
FunctionEnd

; Launch through Explorer so the GUI runs unelevated as the logged in user
Function launchapp
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${APPNAME}.exe"'
FunctionEnd

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION launchapp
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Create Desktop Shortcut"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION finishpageaction
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} - ${DESCRIPTION} Setup"
VIAddVersionKey "FileVersion" "${FULLVERSION}"
VIAddVersionKey "ProductVersion" "${FULLVERSION}"
VIAddVersionKey "LegalCopyright" "GPL-3.0 license"

;--------------------------------
; Helpers shared by installer and uninstaller

!macro DefineStopGui un
Function ${un}StopGui
  nsProcess::_FindProcess "${APPNAME}.exe"
  Pop $0
  ${If} $0 == 0
    DetailPrint "Closing ${APPNAME}..."
    nsProcess::_KillProcess "${APPNAME}.exe"
    Pop $0
    Sleep 1000
  ${EndIf}
FunctionEnd
!macroend
!insertmacro DefineStopGui ""
!insertmacro DefineStopGui "un."

; Stops the service so its files can be replaced or removed
!macro DefineStopService un
Function ${un}StopService
  ReadRegStr $0 HKLM "${SERVICEKEY}" "ImagePath"
  ${If} $0 != ""
    DetailPrint "Stopping ${SERVICENAME}..."
    nsExec::ExecToLog '"$SYSDIR\net.exe" stop ${SERVICENAME}'
    Pop $0
  ${EndIf}
  ; Fall back to killing a service process that did not stop in time
  nsProcess::_FindProcess "${SERVICENAME}.exe"
  Pop $0
  ${If} $0 == 0
    nsProcess::_KillProcess "${SERVICENAME}.exe"
    Pop $0
    Sleep 1000
  ${EndIf}
FunctionEnd
!macroend
!insertmacro DefineStopService ""
!insertmacro DefineStopService "un."

; Finds the folder of an existing installation, even when the uninstall entry is missing
Function DetectExistingInstall
  StrCpy $OldInstDir ""
  ReadRegStr $0 HKLM "${UNINSTKEY}" "InstallLocation"
  ${If} $0 == ""
    ReadRegStr $0 HKCU "Software\${APPNAME}" ""
  ${EndIf}
  ${If} $0 == ""
    ; Service ImagePath is usually quoted, e.g. "D:\Programs\MPM\MPMService.exe"
    ReadRegStr $1 HKLM "${SERVICEKEY}" "ImagePath"
    ${If} $1 != ""
      StrCpy $2 $1 1
      ${If} $2 == '"'
        StrCpy $1 $1 "" 1
      ${EndIf}
      StrCpy $2 $1 1 -1
      ${If} $2 == '"'
        StrCpy $1 $1 -1
      ${EndIf}
      ${GetParent} $1 $0
    ${EndIf}
  ${EndIf}
  ${If} $0 != ""
  ${AndIf} ${FileExists} "$0\${APPNAME}.exe"
    StrCpy $OldInstDir $0
  ${EndIf}
FunctionEnd

;--------------------------------
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APPNAME} requires 64-bit Windows." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64

  Call DetectExistingInstall
  ; Preselect the existing folder unless a folder was passed with /D=
  ${If} $INSTDIR == "$PROGRAMFILES64\${APPNAME}"
  ${AndIf} $OldInstDir != ""
    StrCpy $INSTDIR $OldInstDir
  ${EndIf}
FunctionEnd

;--------------------------------
; Sections

; Main Application (always installed)
Section "${APPNAME} application" SecMain
  SectionIn RO ; Required section

  ; Running files are locked, so close the GUI and stop the service first
  Call StopGui
  Call StopService

  SetOutPath "$INSTDIR"
  File /r "${APPNAME}\*.*"

  ; Save install path to registry (legacy)
  WriteRegStr HKCU "Software\${APPNAME}" "" $INSTDIR

  ; Create Start Menu shortcut
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortcut "$SMPROGRAMS\$StartMenuFolder\${APPNAME}.lnk" "$INSTDIR\${APPNAME}.exe"
  !insertmacro MUI_STARTMENU_WRITE_END

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Write Windows uninstall registry entries
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME} - ${DESCRIPTION}"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "${UNINSTKEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${APPNAME}.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${COMPANYNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "HelpLink" "${HELPURL}"
  WriteRegStr HKLM "${UNINSTKEY}" "URLUpdateInfo" "${UPDATEURL}"
  WriteRegStr HKLM "${UNINSTKEY}" "URLInfoAbout" "${ABOUTURL}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${FULLVERSION}"
  WriteRegDWORD HKLM "${UNINSTKEY}" "VersionMajor" ${VERSIONMAJOR}
  WriteRegDWORD HKLM "${UNINSTKEY}" "VersionMinor" ${VERSIONMINOR}
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" $0

  ; Install date as YYYYMMDD
  ${GetTime} "" "L" $0 $1 $2 $3 $4 $5 $6
  WriteRegStr HKLM "${UNINSTKEY}" "InstallDate" "$2$1$0"
SectionEnd

Section "Background service (runs before login)" SecService
  ReadRegStr $0 HKLM "${SERVICEKEY}" "ImagePath"
  ${If} $0 == ""
    DetailPrint "Installing ${SERVICENAME}..."
    nsExec::ExecToLog '"$INSTDIR\${SERVICENAME}.exe" --install'
    Pop $0
  ${Else}
    ; Repoint the existing service instead of recreating it,
    ; recreating fails while the old service is still pending deletion
    DetailPrint "Updating ${SERVICENAME}..."
    nsExec::ExecToLog '"$SYSDIR\sc.exe" config ${SERVICENAME} binPath= "\"$INSTDIR\${SERVICENAME}.exe\"" start= auto'
    Pop $0
    ${If} $0 == 0
      nsExec::ExecToLog '"$SYSDIR\net.exe" start ${SERVICENAME}'
      Pop $0
    ${EndIf}
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION \
      "${SERVICENAME} could not be installed or started (code $0).$\n$\nYou can install it later by running$\n$\"$INSTDIR\${SERVICENAME}.exe$\" --install$\nas Administrator." \
      /SD IDOK
  ${EndIf}
SectionEnd

; Removes a previously installed service when the service component is unchecked
Section "-RemoveUnselectedService"
  ${IfNot} ${SectionIsSelected} ${SecService}
    ReadRegStr $0 HKLM "${SERVICEKEY}" "ImagePath"
    ${If} $0 != ""
      DetailPrint "Removing ${SERVICENAME}..."
      nsExec::ExecToLog '"$SYSDIR\sc.exe" delete ${SERVICENAME}'
      Pop $0
    ${EndIf}
  ${EndIf}
SectionEnd

; Tells the user about an old installation left behind when a different folder was chosen
Section "-OldFolderNotice"
  ${If} $OldInstDir != ""
  ${AndIf} $OldInstDir != $INSTDIR
  ${AndIf} ${FileExists} "$OldInstDir\${APPNAME}.exe"
    MessageBox MB_OK|MB_ICONINFORMATION \
      "${APPNAME} was installed in a new folder.$\n$\nThe previous installation in $OldInstDir was left in place and can be deleted." \
      /SD IDOK
  ${EndIf}
SectionEnd

;--------------------------------
; Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Main ${APPNAME} application (required)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecService} "Windows service that handles MQTT in the background, also before login. Unchecking removes an installed service."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Uninstaller
Function un.onInit
  SetRegView 64
FunctionEnd

Section "Uninstall"
  Call un.StopGui

  ; Remove the service only when it runs from this folder
  ReadRegStr $0 HKLM "${SERVICEKEY}" "ImagePath"
  ${If} $0 == '"$INSTDIR\${SERVICENAME}.exe"'
  ${OrIf} $0 == "$INSTDIR\${SERVICENAME}.exe"
    Call un.StopService
    DetailPrint "Removing ${SERVICENAME}..."
    nsExec::ExecToLog '"$SYSDIR\sc.exe" delete ${SERVICENAME}'
    Pop $0
  ${EndIf}

  ; Remove only installed files, never the whole folder, since it may be a custom shared folder
  !include "uninstall_files.nsh"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  ; Remove Start Menu and desktop shortcuts
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  Delete "$SMPROGRAMS\$StartMenuFolder\${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"
  Delete "$DESKTOP\${APPNAME}.lnk"

  ; Remove registry entries, including the startup entry written by the app
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APPNAME}"
  DeleteRegKey HKCU "Software\${APPNAME}"
  DeleteRegKey HKLM "${UNINSTKEY}"

  ; Settings and logs live in ProgramData and are shared with the service
  ReadEnvStr $0 PROGRAMDATA
  ${If} $0 != ""
  ${AndIf} ${FileExists} "$0\${APPNAME}\*.*"
    MessageBox MB_YESNO|MB_ICONQUESTION "Also delete ${APPNAME} settings and logs in $0\${APPNAME}?" /SD IDNO IDNO keep_settings
      RMDir /r "$0\${APPNAME}"
    keep_settings:
  ${EndIf}
SectionEnd

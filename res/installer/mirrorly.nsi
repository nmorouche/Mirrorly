; Mirrorly - Windows installer (NSIS 3). Built by build_installer.sh:
;   makensis -DVERSION=1.2.3 -DVERNUM=1.2.3.0 -DREPO=owner/name -DEXE=<Mirrorly.exe> -DOUTFILE=<setup.exe> mirrorly.nsi
;
; Installs for the current Windows account only (%LOCALAPPDATA%\Programs\Mirrorly): no administrator
; rights, and the app can update itself later without any prompt.
;   Mirrorly-Setup.exe            normal install (welcome, progress, finish with "Open Mirrorly")
;   Mirrorly-Setup.exe /S         silent install
;   Mirrorly-Setup.exe /S /UPDATE silent install started by the app's updater; starts Mirrorly again
; Optional -DSIGN="<command> %1" signs the uninstaller and the setup while they are built.

Unicode true
Target amd64-unicode  ; Mirrorly is 64-bit only
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "WinMessages.nsh"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mirrorly"
!define UNINSTALLER "Uninstall Mirrorly.exe"

Name "Mirrorly"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Mirrorly"
BrandingText "Mirrorly ${VERSION}"

VIProductVersion "${VERNUM}"
VIFileVersion "${VERNUM}"
VIAddVersionKey "ProductName" "Mirrorly"
VIAddVersionKey "FileDescription" "Mirrorly Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "Mirrorly"
VIAddVersionKey "LegalCopyright" "GPL-3.0"
VIAddVersionKey "OriginalFilename" "Mirrorly-Setup.exe"

!ifdef SIGN
  !uninstfinalize '${SIGN}'
  !finalize '${SIGN}'
!endif

; ------------------------------------------------------------------ look

!define MUI_ICON "../mirrorly.ico"
!define MUI_UNICON "../mirrorly.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "sidebar.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "sidebar.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_ABORTWARNING

; ------------------------------------------------------------------ pages

!define MUI_WELCOMEPAGE_TITLE "Install Mirrorly"
!define MUI_WELCOMEPAGE_TEXT "Mirror your iPhone, iPad or Android phone on this PC, with sound.$\r$\n$\r$\nMirrorly installs for your Windows account only. It doesn't need administrator rights and keeps itself up to date.$\r$\n$\r$\nClick Install to continue."
!define MUI_PAGE_CUSTOMFUNCTION_SHOW WelcomeShow
!insertmacro MUI_PAGE_WELCOME

!define MUI_PAGE_HEADER_TEXT "Installing Mirrorly"
!define MUI_PAGE_HEADER_SUBTEXT "This only takes a few seconds."
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "Mirrorly is installed"
!define MUI_FINISHPAGE_TEXT "You'll find Mirrorly in the Start menu.$\r$\n$\r$\nIf Windows asks about network access, click Allow so your phone can find this PC."
!define MUI_FINISHPAGE_RUN "$INSTDIR\Mirrorly.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Mirrorly"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Add a shortcut to the desktop"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION DesktopShortcut
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function WelcomeShow
  GetDlgItem $0 $HWNDPARENT 1
  SendMessage $0 ${WM_SETTEXT} 0 "STR:&Install"
FunctionEnd

Function DesktopShortcut
  CreateShortcut "$DESKTOP\Mirrorly.lnk" "$INSTDIR\Mirrorly.exe"
FunctionEnd

; Closes a running Mirrorly and waits until it has exited (its single-instance mutex is gone).
!macro CLOSE_MIRRORLY UN
Function ${UN}CloseMirrorly
  FindWindow $0 "MirrorlyWindow"
  ${If} $0 <> 0
    DetailPrint "Closing Mirrorly..."
    SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=3000
  ${EndIf}
  StrCpy $1 0
  ${Do}
    System::Call 'kernel32::OpenMutexW(i 0x00100000, i 0, w "Mirrorly.SingleInstance") p .r2'
    ${If} $2 == 0
      ${Break}
    ${EndIf}
    System::Call 'kernel32::CloseHandle(p r2)'
    Sleep 200
    IntOp $1 $1 + 1
  ${LoopUntil} $1 >= 100
FunctionEnd
!macroend
!insertmacro CLOSE_MIRRORLY ""
!insertmacro CLOSE_MIRRORLY "un."

; ------------------------------------------------------------------ install

Section "Mirrorly"
  Call CloseMirrorly
  SetOutPath "$INSTDIR"
  ; Windows lets a running program be renamed but not overwritten: move the old one aside first
  Delete "$INSTDIR\Mirrorly.exe.old"
  ${If} ${FileExists} "$INSTDIR\Mirrorly.exe"
    Rename "$INSTDIR\Mirrorly.exe" "$INSTDIR\Mirrorly.exe.old"
  ${EndIf}
  File "/oname=Mirrorly.exe" "${EXE}"
  Delete "$INSTDIR\Mirrorly.exe.old"  ; Mirrorly removes it on its next start if this fails
  WriteUninstaller "$INSTDIR\${UNINSTALLER}"

  CreateShortcut "$SMPROGRAMS\Mirrorly.lnk" "$INSTDIR\Mirrorly.exe"

  ; Settings > Apps > Installed apps
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Mirrorly"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Nassim Morouche"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Mirrorly.exe,0"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\${UNINSTALLER}"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\${UNINSTALLER}" /S'
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/${REPO}"
  WriteRegStr HKCU "${UNINST_KEY}" "HelpLink" "https://github.com/${REPO}/issues"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0

  ; started by Mirrorly's updater: open the new version
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    Exec '"$INSTDIR\Mirrorly.exe" --updated'
  ${EndIf}
SectionEnd

; ------------------------------------------------------------------ uninstall

Section "Uninstall"
  Call un.CloseMirrorly
  Delete "$INSTDIR\Mirrorly.exe"
  Delete "$INSTDIR\Mirrorly.exe.old"
  Delete "$INSTDIR\Mirrorly.exe.download"
  Delete "$INSTDIR\Mirrorly.log"
  Delete "$INSTDIR\${UNINSTALLER}"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\Mirrorly.lnk"
  Delete "$DESKTOP\Mirrorly.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  RMDir /r "$LOCALAPPDATA\Mirrorly"  ; the unpacked Android tools
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your Mirrorly settings and key maps?" /SD IDNO IDNO keep
    RMDir /r "$APPDATA\Mirrorly"
  keep:
SectionEnd

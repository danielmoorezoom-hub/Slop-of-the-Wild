; The Windows installer: one exe holding build/dist/Slop of the Wild, which is the program and what it needs to build the game
; code on the player's PC. No game files or images of the game. Build with `make installer`.
; C:\ by default, not Program Files: saves, settings and caches go next to the program, and Program Files doesn't allow that.
; Its look follows the start screen's (runtime/ui/launcher.mm): the same colours, the same art (docs/art, replace the .bmp files).
; On a Windows too old for the dark controls they stay plain, and it all still works.
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
ManifestDPIAware true

!ifndef VERSION
  !error "VERSION: make installer gives it"
!endif
!define NAME "Slop of the Wild"
!define DIST "..\build\dist\Slop of the Wild"
!define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\SlopOfTheWild"
!define BG "0D1A16"
!define TXT "F2EEE0"

Name "${NAME} ${VERSION}"
OutFile "..\build\dist\SlopOfTheWild-${VERSION}-Setup.exe"
InstallDir "C:\Slop of the Wild"
InstallDirRegKey HKCU "${UNINST}" "InstallLocation"
BrandingText " "
XPStyle on

!include "MUI2.nsh"
!define MUI_ICON "..\docs\icon\icon.ico"
!define MUI_UNICON "..\docs\icon\icon.ico"
!define MUI_ABORTWARNING
!define MUI_BGCOLOR "${BG}"
!define MUI_TEXTCOLOR "${TXT}"
!define MUI_WELCOMEFINISHPAGE_BITMAP "..\docs\art\installer-side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "..\docs\art\installer-side.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "..\docs\art\installer-header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "..\docs\art\installer-header.bmp"
!define MUI_INSTFILESPAGE_COLORS "${TXT} ${BG}"
!define MUI_CUSTOMFUNCTION_GUIINIT DarkFrame
!define MUI_CUSTOMFUNCTION_UNGUIINIT un.DarkFrame
!define MUI_WELCOMEPAGE_TITLE "Slop of the Wild ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs the program only. It contains no game files.$\r$\n$\r$\nTo play, you need your own copy of The Legend of Zelda: Breath of the Wild, version 1.9.0 with the update merged, dumped from your own Switch. After installing, the program asks for that folder, and for the downloadable content's folder if you own it, then spends about five minutes setting up.$\r$\n$\r$\nThis needs about 500 MB here, plus about 16 GB more for the game files during setup."
!define MUI_DIRECTORYPAGE_TEXT_TOP "Saves, settings and game files are kept in this folder, so pick one you can write to. Program Files won't work."
!define MUI_FINISHPAGE_TITLE "Installed"
!define MUI_FINISHPAGE_TEXT "Next, the program asks where your copy of the game is. If you own the downloadable content, you add it on that same page."
!define MUI_FINISHPAGE_RUN "$INSTDIR\savagewind.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Slop of the Wild and set up your game files"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Add a shortcut to the desktop"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION DesktopShortcut

!define MUI_PAGE_CUSTOMFUNCTION_SHOW DarkInner
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_SHOW DarkInner
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_CUSTOMFUNCTION_SHOW DarkInstFiles
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_SHOW DarkInner
!insertmacro MUI_PAGE_FINISH
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.DarkInner
!insertmacro MUI_UNPAGE_CONFIRM
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.DarkInstFiles
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; The colours of the window around the pages (dark title bar: Windows 10 2004 on; its colour: Windows 11; dark buttons), and of
; everything inside a page, one control at a time.
!macro DarkFunctions un
Function ${un}DarkButton
  Pop $R1
  System::Call 'uxtheme::SetWindowTheme(p R1, w "DarkMode_Explorer", p 0)'
FunctionEnd

Function ${un}DarkFrame
  System::Call 'dwmapi::DwmSetWindowAttribute(p $HWNDPARENT, i 20, *i 1, i 4)'
  System::Call 'dwmapi::DwmSetWindowAttribute(p $HWNDPARENT, i 35, *i 0x00161A0D, i 4)'
  SetCtlColors $HWNDPARENT ${TXT} ${BG}
  StrCpy $R0 0
  loop:
    FindWindow $R0 "" "" $HWNDPARENT $R0
    StrCmp $R0 0 done
    System::Call 'user32::GetClassName(p R0, t .R2, i 64)'
    StrCmp $R2 "Button" 0 +3
      Push $R0
      Call ${un}DarkButton
    StrCmp $R2 "Static" 0 loop
      SetCtlColors $R0 ${TXT} ${BG}
    Goto loop
  done:
FunctionEnd

; (classic look on check boxes and the group box: a themed one ignores the colours)
Function ${un}DarkInner
  FindWindow $R3 "#32770" "" $HWNDPARENT
  SetCtlColors $R3 ${TXT} ${BG}
  StrCpy $R0 0
  loop:
    FindWindow $R0 "" "" $R3 $R0
    StrCmp $R0 0 done
    System::Call 'user32::GetClassName(p R0, t .R2, i 64)'
    StrCmp $R2 "Button" 0 +2
      System::Call 'uxtheme::SetWindowTheme(p R0, w "", w "")'
    StrCmp $R2 "Edit" 0 +2
      System::Call 'uxtheme::SetWindowTheme(p R0, w "DarkMode_CFD", p 0)'
    SetCtlColors $R0 ${TXT} ${BG}
    Goto loop
  done:
  GetDlgItem $R0 $R3 1001  ; (the directory page's Browse)
  StrCmp $R0 0 +3
    Push $R0
    Call ${un}DarkButton
FunctionEnd

; (the bar in the accent colour, on the page's)
Function ${un}DarkInstFiles
  Call ${un}DarkInner
  GetDlgItem $R0 $R3 1004
  System::Call 'uxtheme::SetWindowTheme(p R0, w "", w "")'
  SendMessage $R0 0x0409 0 0x0054E2BE
  SendMessage $R0 0x2001 0 0x00302218
FunctionEnd
!macroend
!insertmacro DarkFunctions ""
!insertmacro DarkFunctions "un."

Function DesktopShortcut
  CreateShortcut "$DESKTOP\${NAME}.lnk" "$INSTDIR\savagewind.exe"
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${DIST}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\${NAME}"
  CreateShortcut "$SMPROGRAMS\${NAME}\${NAME}.lnk" "$INSTDIR\savagewind.exe"
  CreateShortcut "$SMPROGRAMS\${NAME}\${NAME} Configuration.lnk" "$INSTDIR\Slop of the Wild Configuration.exe"
  CreateShortcut "$SMPROGRAMS\${NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${UNINST}" "DisplayName" "${NAME}"
  WriteRegStr HKCU "${UNINST}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST}" "DisplayIcon" "$INSTDIR\savagewind.exe"
  WriteRegStr HKCU "${UNINST}" "Publisher" "devall96"
  WriteRegStr HKCU "${UNINST}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST}" "NoRepair" 1
SectionEnd

; Takes out what this installed by name, never the whole folder: it may be one the player chose with other things in it.
; Saves, settings and the game files stay, and the message says so.
Section "Uninstall"
  RMDir /r "$INSTDIR\zig"
  RMDir /r "$INSTDIR\swgen"
  RMDir /r "$INSTDIR\gamelib-src"
  RMDir /r "$INSTDIR\colour-luts"
  RMDir /r "$INSTDIR\benchmark"
  Delete "$INSTDIR\savagewind.exe"
  Delete "$INSTDIR\savagewind.dll.a"
  Delete "$INSTDIR\Slop of the Wild Configuration.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.ttf"
  Delete "$INSTDIR\FiraSans-OFL.txt"
  Delete "$INSTDIR\NotoSansThai-OFL.txt"
  Delete "$INSTDIR\FidelityFX-LICENSE.txt"
  Delete "$INSTDIR\icon.png"
  Delete "$INSTDIR\launcher-art.png"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  RMDir /r "$SMPROGRAMS\${NAME}"
  Delete "$DESKTOP\${NAME}.lnk"
  DeleteRegKey HKCU "${UNINST}"
  IfFileExists "$INSTDIR\*.*" 0 +2
    MessageBox MB_OK /SD IDOK "Your saves, settings and game files were kept in $INSTDIR. Delete that folder yourself if you don't want them."
SectionEnd

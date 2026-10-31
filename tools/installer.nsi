; The Windows installer: one exe holding build/dist/Slop of the Wild, which is the program and what it needs to build the game
; code on the player's PC. No game files or images of the game. Build with `make installer`.
; C:\ by default, not Program Files: saves, settings and caches go next to the program, and Program Files doesn't allow that.
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

Name "${NAME} ${VERSION}"
OutFile "..\build\dist\SlopOfTheWild-${VERSION}-Setup.exe"
InstallDir "C:\Slop of the Wild"
InstallDirRegKey HKCU "${UNINST}" "InstallLocation"
BrandingText "${NAME} ${VERSION}"

!include "MUI2.nsh"
!define MUI_ICON "..\docs\icon\icon.ico"
!define MUI_UNICON "..\docs\icon\icon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Slop of the Wild ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs the program only. It contains no game files.$\r$\n$\r$\nTo play, you need your own copy of The Legend of Zelda: Breath of the Wild, version 1.9.0 with the update merged, dumped from your own Switch. After installing, the program asks for that folder and spends about five minutes setting up.$\r$\n$\r$\nThis needs about 500 MB here, plus about 16 GB more for the game files during setup."
!define MUI_DIRECTORYPAGE_TEXT_TOP "Saves, settings and game files are kept in this folder, so pick one you can write to. Program Files won't work."
!define MUI_FINISHPAGE_RUN "$INSTDIR\savagewind.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Slop of the Wild and set up your game files"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Add a shortcut to the desktop"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION DesktopShortcut
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

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
  Delete "$INSTDIR\savagewind.exe"
  Delete "$INSTDIR\savagewind.dll.a"
  Delete "$INSTDIR\Slop of the Wild Configuration.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.ttf"
  Delete "$INSTDIR\FiraSans-OFL.txt"
  Delete "$INSTDIR\NotoSansThai-OFL.txt"
  Delete "$INSTDIR\FidelityFX-LICENSE.txt"
  Delete "$INSTDIR\icon.png"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  RMDir /r "$SMPROGRAMS\${NAME}"
  Delete "$DESKTOP\${NAME}.lnk"
  DeleteRegKey HKCU "${UNINST}"
  IfFileExists "$INSTDIR\*.*" 0 +2
    MessageBox MB_OK "Your saves, settings and game files were kept in $INSTDIR. Delete that folder yourself if you don't want them."
SectionEnd

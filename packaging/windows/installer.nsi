; Installateur MUVideoPlayerPro (NSIS). VERSION est fourni en ligne de
; commande par la CI : makensis /DVERSION=0.1.0 installer.nsi
; Attend un dossier "vst3\MUVideoPlayerPro.vst3\" à côté de ce script
; (copié là par le workflow avant l'appel à makensis).

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!include "MUI2.nsh"

Name "MUVideoPlayerPro v${VERSION}"
OutFile "MUVideoPlayerPro-Setup-v${VERSION}.exe"
InstallDir "$COMMONFILES64\VST3\MUVideoPlayerPro.vst3"
InstallDirRegKey HKLM "Software\MUVideoPlayerPro" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "MUVideoPlayerPro"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "MUVideoPlayerPro VST3 installer"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "MUVideoPlayerPro VST3" SecMain
    SetOutPath "$INSTDIR"
    File /r "vst3\MUVideoPlayerPro.vst3\*.*"

    WriteRegStr HKLM "Software\MUVideoPlayerPro" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MUVideoPlayerPro" \
                "DisplayName" "MUVideoPlayerPro v${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MUVideoPlayerPro" \
                "UninstallString" "$COMMONFILES64\VST3\MUVideoPlayerPro-Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MUVideoPlayerPro" \
                "DisplayVersion" "${VERSION}"

    WriteUninstaller "$COMMONFILES64\VST3\MUVideoPlayerPro-Uninstall.exe"
SectionEnd

Section "Uninstall"
    RMDir /r "$COMMONFILES64\VST3\MUVideoPlayerPro.vst3"
    Delete "$COMMONFILES64\VST3\MUVideoPlayerPro-Uninstall.exe"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MUVideoPlayerPro"
    DeleteRegKey HKLM "Software\MUVideoPlayerPro"
SectionEnd

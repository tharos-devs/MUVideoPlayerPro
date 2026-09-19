; Installateur MUVideoPlayerPro (NSIS). VERSION et FILEVERSION sont fournis
; en ligne de commande par la CI :
;   makensis /DVERSION=0.1.0 /DFILEVERSION=0.1.0.0 installer.nsi
; FILEVERSION est une version purement numérique X.X.X.X (requise par
; VIProductVersion), distincte de VERSION qui peut porter un suffixe
; "-<sha>" en repli.
; Attend un dossier "vst3\MUVideoPlayerPro.vst3\" et un "vc_redist.x64.exe"
; à côté de ce script (copiés/téléchargés là par le workflow avant l'appel à
; makensis).

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!ifndef FILEVERSION
  !define FILEVERSION "0.0.0.0"
!endif

!include "MUI2.nsh"

Name "MUVideoPlayerPro v${VERSION}"
OutFile "MUVideoPlayerPro-Setup-v${VERSION}.exe"
InstallDir "$COMMONFILES64\VST3\MUVideoPlayerPro.vst3"
InstallDirRegKey HKLM "Software\MUVideoPlayerPro" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${FILEVERSION}"
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
    ; Runtime Visual C++ requis par le plugin (vcruntime140.dll, msvcp140.dll)
    ; -- pas fourni par Windows. Sans lui, le plugin échoue à charger et la
    ; plupart des hôtes VST3 l'excluent silencieusement de leur scan.
    ; /install /quiet /norestart : silencieux, ne fait rien si déjà présent.
    SetOutPath "$TEMP"
    File "vc_redist.x64.exe"
    DetailPrint "Installation du runtime Visual C++ (silencieuse)..."
    ExecWait '"$TEMP\vc_redist.x64.exe" /install /quiet /norestart'
    Delete "$TEMP\vc_redist.x64.exe"

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

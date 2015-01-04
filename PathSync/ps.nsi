;Include Modern UI


!define MUI_COMPONENTSPAGE_NODESC

  !include "MUI.nsh"

;--------------------------------
;General

!searchparse /file pathsync.cpp '#define PATHSYNC_VER "v' VER_MAJOR '.' VER_MINOR '"'

SetCompressor lzma



  ;Name and file
  Name "PathSync v${VER_MAJOR}.${VER_MINOR}"
  OutFile "pathsync${VER_MAJOR}${VER_MINOR}-install.exe"

  ;Default installation folder
  InstallDir "$PROGRAMFILES\PathSync"
  
  ;Get installation folder from registry if available
  InstallDirRegKey HKLM "Software\PathSync" ""

;--------------------------------
;Interface Settings

  !define MUI_ABORTWARNING

;--------------------------------
;Pages

  !insertmacro MUI_PAGE_LICENSE "license.txt"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES
  
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES
  
;--------------------------------
;Languages
 
  !insertmacro MUI_LANGUAGE "English"




;--------------------------------
;Installer Sections


Section "Required files"

  SectionIn RO
  SetOutPath "$INSTDIR"


  File release\pathsync.exe
    
  ;Store installation folder
  WriteRegStr HKLM "Software\pathsync" "" $INSTDIR
  
  ;Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  File license.txt
  File whatsnew.txt

SectionEnd

Section "Associate with PSS files"
  WriteRegStr HKCR ".pss" "" "pssfile"
  WriteRegStr HKCR "pssfile" "" "PathSync Settings file"
  WriteRegStr HKCR "pssfile\DefaultIcon" "" "$INSTDIR\pathsync.exe,0"
  WriteRegStr HKCR "pssfile\shell\open\command" "" '"$INSTDIR\pathsync.exe" -loadpss "%1"' 
SectionEnd

Section "Desktop Icon"
  CreateShortcut "$DESKTOP\PathSync.lnk" "$INSTDIR\pathsync.exe"
SectionEnd

Section "Start Menu Shortcuts"

  SetOutPath $SMPROGRAMS\Pathsync
  CreateShortcut "$OUTDIR\PathSync.lnk" "$INSTDIR\pathsync.exe"
  CreateShortcut "$OUTDIR\PathSync License.lnk" "$INSTDIR\license.txt"
  CreateShortcut "$OUTDIR\Whatsnew.txt.lnk" "$INSTDIR\whatsnew.txt"
  CreateShortcut "$OUTDIR\Uninstall PathSync.lnk" "$INSTDIR\uninstall.exe"

  SetOutPath $INSTDIR

SectionEnd

Section "PathSync Source Code"
  SetOutPath $INSTDIR\Source\PathSync
  File pathsync.dsw
  File pathsync.dsp
  File pathsync.cpp
  File fnmatch.cpp
  File fnmatch.h
  File resource.h
  File res.rc
  File ps.nsi
  File icon1.ico
  SetOutPath $INSTDIR\Source\WDL
  File ..\WDL\dirscan.h
  File ..\WDL\ptrlist.h
  File ..\WDL\heapbuf.h
  File ..\WDL\fileread.h
  File ..\WDL\filewrite.h
  File ..\WDL\wdlstring.h
  File ..\WDL\wdltypes.h
  File ..\WDL\win32_utf8.h
  File ..\WDL\win32_utf8.c
  SetOutPath $INSTDIR\Source\WDL\WinGUI
  File ..\WDL\wingui\wndsize.h
  File ..\WDL\wingui\wndsize.cpp 
  File ..\WDL\wingui\virtwnd.h
  File ..\WDL\wingui\virtwnd-skin.h
SectionEnd



;--------------------------------
;Uninstaller Section

Section "Uninstall"

  DeleteRegKey HKLM "Software\PathSync"
  DeleteRegKey HKCR ".pss"
  DeleteRegKey HKCR "pssfile"

  Delete "$INSTDIR\pathsync.exe"

  Delete "$INSTDIR\pathsync.ini"
  Delete "$INSTDIR\license.txt"
  Delete "$INSTDIR\whatsnew.txt"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$SMPROGRAMS\PathSync\*.lnk"
  RMDir $SMPROGRAMS\PathSync
  Delete "$DESKTOP\PathSync.lnk"

  Delete $INSTDIR\Source\PathSync\pathsync.dsw
  Delete $INSTDIR\Source\PathSync\pathsync.dsp
  Delete $INSTDIR\Source\PathSync\pathsync.cpp
  Delete $INSTDIR\Source\PathSync\fnmatch.cpp
  Delete $INSTDIR\Source\PathSync\fnmatch.h
  Delete $INSTDIR\Source\PathSync\ps.nsi
  Delete $INSTDIR\Source\PathSync\resource.h
  Delete $INSTDIR\Source\PathSync\res.rc
  Delete $INSTDIR\Source\PathSync\icon1.ico

  Delete $INSTDIR\Source\WDL\*
  Delete $INSTDIR\Source\WDL\WinGUI\*
  RMDir $INSTDIR\Source\WDL\WinGUI
  RMDir $INSTDIR\Source\WDL
  RMDir $INSTDIR\Source\PathSync
  RMDir $INSTDIR\Source
  
  RMDir "$INSTDIR"

SectionEnd

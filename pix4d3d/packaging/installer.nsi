; 발굴 단면뷰어 — 사용자 단위 설치(관리자 권한 없음). makensis -DSRCDIR=<배포폴더> -DOUTFILE=<출력.exe> installer.nsi
Unicode true
!include "MUI2.nsh"
!define APPNAME "발굴 단면뷰어"
!define EXE "SectionViewer.exe"
!define VERSION "1.2.1"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ExcavSectionViewer"
!ifndef SRCDIR
  !define SRCDIR "..\build-win\deploy\SectionViewer"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\dist\SectionViewer-1.2.1-Setup.exe"
!endif

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\ExcavSectionViewer"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APPNAME} ${VERSION}"

VIProductVersion "1.2.1.0"
VIAddVersionKey /LANG=1042 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1042 "FileDescription" "${APPNAME} 설치"
VIAddVersionKey /LANG=1042 "CompanyName" "${APPNAME}"
VIAddVersionKey /LANG=1042 "LegalCopyright" "© 2026 ${APPNAME}"
VIAddVersionKey /LANG=1042 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1042 "ProductVersion" "${VERSION}"

!define MUI_ICON "assets\app.ico"
!define MUI_UNICON "assets\app.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "assets\wizard.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "assets\wizard.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "assets\header.bmp"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "${APPNAME} 설치"
!define MUI_WELCOMEPAGE_TEXT "3MX 실사 메시를 열어 평면에서 단면선을 긋고$\r$\n10 cm 레벨선이 있는 단면도를 바로 보고$\r$\nDXF · GeoTIFF · 점군으로 내보냅니다.$\r$\n$\r$\n관리자 권한 없이 이 사용자에게만 설치되고,$\r$\n바탕화면과 시작 메뉴에 바로가기가 만들어집니다."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "지금 ${APPNAME} 실행"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Korean"

Section "Install"
  SetShellVarContext current
  nsExec::Exec 'taskkill /F /IM ${EXE}'
  Sleep 300
  SetOutPath "$INSTDIR"
  File /r "${SRCDIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
  CreateShortCut "$SMPROGRAMS\${APPNAME}\읽어보기.lnk" "$INSTDIR\읽어보기.txt"
  CreateShortCut "$SMPROGRAMS\${APPNAME}\제거.lnk" "$INSTDIR\uninstall.exe"
  ; .3mx "연결 프로그램" 목록에 추가(이 사용자만, 기존 기본 연결은 건드리지 않음)
  WriteRegStr HKCU "Software\Classes\Applications\${EXE}\shell\open\command" "" '"$INSTDIR\${EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\.3mx\OpenWithList\${EXE}" "" ""
  WriteRegStr HKCU "${UNKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNKEY}" "Publisher" "${APPNAME}"
  WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  nsExec::Exec 'taskkill /F /IM ${EXE}'
  Sleep 300
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"
  DeleteRegKey HKCU "Software\Classes\Applications\${EXE}"
  DeleteRegKey HKCU "Software\Classes\.3mx\OpenWithList\${EXE}"
  DeleteRegKey HKCU "${UNKEY}"
  ; 설치 폴더는 고정($LOCALAPPDATA\Programs\ExcavSectionViewer, 폴더 선택 화면 없음)이므로 통째로 지워도 안전
  RMDir /r "$INSTDIR"
SectionEnd

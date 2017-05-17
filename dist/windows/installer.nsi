﻿Var uninstallerPath

Section "-hidden"

    ;Search if qBittorrent is already installed.
    FindFirst $0 $1 "$uninstallerPath\uninst.exe"
    FindClose $0
    StrCmp $1 "" done

    ;Run the uninstaller of the previous install.
    DetailPrint $(inst_unist)
    ExecWait '"$uninstallerPath\uninst.exe" /S _?=$uninstallerPath'
    Delete "$uninstallerPath\uninst.exe"
    RMDir "$uninstallerPath"

    done:

SectionEnd


Section $(inst_qbt_req) ;"qBittorrent (required)"

  SectionIn RO

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  ;Create 'translations' directory
  CreateDirectory $INSTDIR\translations

  ; Put file there
  File "qbittorrent.exe"
  File "qbittorrent.pdb"
  File "qt.conf"
  File /oname=translations\qt_ar.qm "translations\qt_ar.qm"
  File /oname=translations\qt_bg.qm "translations\qt_bg.qm"
  File /oname=translations\qt_ca.qm "translations\qt_ca.qm"
  File /oname=translations\qt_cs.qm "translations\qt_cs.qm"
  File /oname=translations\qt_da.qm "translations\qt_da.qm"
  File /oname=translations\qt_de.qm "translations\qt_de.qm"
  File /oname=translations\qt_es.qm "translations\qt_es.qm"
  File /oname=translations\qt_eu.qm "translations\qt_eu.qm"
  File /oname=translations\qt_fa.qm "translations\qt_fa.qm"
  File /oname=translations\qt_fi.qm "translations\qt_fi.qm"
  File /oname=translations\qt_fr.qm "translations\qt_fr.qm"
  File /oname=translations\qt_gl.qm "translations\qt_gl.qm"
  File /oname=translations\qt_he.qm "translations\qt_he.qm"
  File /oname=translations\qt_hu.qm "translations\qt_hu.qm"
  File /oname=translations\qt_it.qm "translations\qt_it.qm"
  File /oname=translations\qt_ja.qm "translations\qt_ja.qm"
  File /oname=translations\qt_ko.qm "translations\qt_ko.qm"
  File /oname=translations\qt_lt.qm "translations\qt_lt.qm"
  File /oname=translations\qt_nl.qm "translations\qt_nl.qm"
  File /oname=translations\qt_pl.qm "translations\qt_pl.qm"
  File /oname=translations\qt_pt.qm "translations\qt_pt.qm"
  File /oname=translations\qt_pt_BR.qm "translations\qt_pt_BR.qm"
  File /oname=translations\qt_ru.qm "translations\qt_ru.qm"
  File /oname=translations\qt_sk.qm "translations\qt_sk.qm"
  File /oname=translations\qt_sl.qm "translations\qt_sl.qm"
  File /oname=translations\qt_sv.qm "translations\qt_sv.qm"
  File /oname=translations\qt_tr.qm "translations\qt_tr.qm"
  File /oname=translations\qt_uk.qm "translations\qt_uk.qm"
  File /oname=translations\qt_zh_CN.qm "translations\qt_zh_CN.qm"
  File /oname=translations\qt_zh_TW.qm "translations\qt_zh_TW.qm"
  File /oname=translations\qtbase_ca.qm "translations\qtbase_ca.qm"
  File /oname=translations\qtbase_cs.qm "translations\qtbase_cs.qm"
  File /oname=translations\qtbase_de.qm "translations\qtbase_de.qm"
  File /oname=translations\qtbase_fi.qm "translations\qtbase_fi.qm"
  File /oname=translations\qtbase_fr.qm "translations\qtbase_fr.qm"
  File /oname=translations\qtbase_he.qm "translations\qtbase_he.qm"
  File /oname=translations\qtbase_hu.qm "translations\qtbase_hu.qm"
  File /oname=translations\qtbase_it.qm "translations\qtbase_it.qm"
  File /oname=translations\qtbase_ja.qm "translations\qtbase_ja.qm"
  File /oname=translations\qtbase_ko.qm "translations\qtbase_ko.qm"
  File /oname=translations\qtbase_lv.qm "translations\qtbase_lv.qm"
  File /oname=translations\qtbase_pl.qm "translations\qtbase_pl.qm"
  File /oname=translations\qtbase_ru.qm "translations\qtbase_ru.qm"
  File /oname=translations\qtbase_sk.qm "translations\qtbase_sk.qm"
  File /oname=translations\qtbase_uk.qm "translations\qtbase_uk.qm"

  ; Write the installation path into the registry
  WriteRegStr HKLM "Software\qBittorrent" "InstallLocation" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "DisplayName" "qBittorrent ${PROG_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "UninstallString" '"$INSTDIR\uninst.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "DisplayIcon" '"$INSTDIR\qbittorrent.exe",0'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "Publisher" "The qBittorrent project"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "URLInfoAbout" "http://www.qbittorrent.org"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "DisplayVersion" "${PROG_VERSION}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "NoRepair" 1
  WriteUninstaller "uninst.exe"
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\qBittorrent" "EstimatedSize" "$0"

  ; qBittorrent ProgID
  WriteRegStr HKLM "Software\Classes\qBittorrent" "" "qBittorrent Torrent File"
  WriteRegStr HKLM "Software\Classes\qBittorrent" "FriendlyTypeName" "qBittorrent Torrent File"
  WriteRegStr HKLM "Software\Classes\qBittorrent\shell" "" "open"
  WriteRegStr HKLM "Software\Classes\qBittorrent\shell\open\command" "" '"$INSTDIR\qbittorrent.exe" "%1"'
  WriteRegStr HKLM "Software\Classes\qBittorrent\DefaultIcon" "" '"$INSTDIR\qbittorrent.exe",1'

SectionEnd

; Optional section (can be disabled by the user)
Section /o $(inst_dekstop) ;"Create Desktop Shortcut"

  CreateShortCut "$DESKTOP\qBittorrent.lnk" "$INSTDIR\qbittorrent.exe"

SectionEnd

Section $(inst_startmenu) ;"Create Start Menu Shortcut"

  CreateDirectory "$SMPROGRAMS\qBittorrent"
  CreateShortCut "$SMPROGRAMS\qBittorrent\qBittorrent.lnk" "$INSTDIR\qbittorrent.exe"
  CreateShortCut "$SMPROGRAMS\qBittorrent\Uninstall.lnk" "$INSTDIR\uninst.exe"

SectionEnd

Section $(inst_torrent) ;"Open .torrent files with qBittorrent"

  ReadRegStr $0 HKLM "Software\Classes\.torrent" ""

  StrCmp $0 "qBittorrent" clear_errors 0
  ;Check if empty string
  StrCmp $0 "" clear_errors 0
  ;Write old value to OpenWithProgIds
  WriteRegStr HKLM "Software\Classes\.torrent\OpenWithProgIds" $0 ""

  clear_errors:
  ClearErrors

  WriteRegStr HKLM "Software\Classes\.torrent" "" "qBittorrent"
  WriteRegStr HKLM "Software\Classes\.torrent" "Content Type" "application/x-bittorrent"

  !insertmacro UAC_AsUser_Call Function inst_torrent_user ${UAC_SYNCREGISTERS}|${UAC_SYNCOUTDIR}|${UAC_SYNCINSTDIR}

  System::Call 'Shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i ${SHCNF_IDLIST}, i 0, i 0)'

SectionEnd

Function inst_torrent_user

  ReadRegStr $0 HKCU "Software\Classes\.torrent" ""

  StrCmp $0 "qBittorrent" clear_errors 0
  ;Check if empty string
  StrCmp $0 "" clear_errors 0
  ;Write old value to OpenWithProgIds
  WriteRegStr HKCU "Software\Classes\.torrent\OpenWithProgIds" $0 ""

  clear_errors:
  ClearErrors

  WriteRegStr HKCU "Software\Classes\.torrent" "" "qBittorrent"
  WriteRegStr HKCU "Software\Classes\.torrent" "Content Type" "application/x-bittorrent"

FunctionEnd

Section $(inst_magnet) ;"Open magnet links with qBittorrent"

  WriteRegStr HKLM "Software\Classes\magnet" "" "URL:Magnet link"
  WriteRegStr HKLM "Software\Classes\magnet" "Content Type" "application/x-magnet"
  WriteRegStr HKLM "Software\Classes\magnet" "URL Protocol" ""
  WriteRegStr HKLM "Software\Classes\magnet\DefaultIcon" "" '"$INSTDIR\qbittorrent.exe",1'
  WriteRegStr HKLM "Software\Classes\magnet\shell" "" "open"
  WriteRegStr HKLM "Software\Classes\magnet\shell\open\command" "" '"$INSTDIR\qbittorrent.exe" "%1"'

  !insertmacro UAC_AsUser_Call Function inst_magnet_user ${UAC_SYNCREGISTERS}|${UAC_SYNCOUTDIR}|${UAC_SYNCINSTDIR}

  System::Call 'Shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i ${SHCNF_IDLIST}, i 0, i 0)'

SectionEnd

Function inst_magnet_user

  WriteRegStr HKCU "Software\Classes\magnet" "" "URL:Magnet link"
  WriteRegStr HKCU "Software\Classes\magnet" "Content Type" "application/x-magnet"
  WriteRegStr HKCU "Software\Classes\magnet" "URL Protocol" ""
  WriteRegStr HKCU "Software\Classes\magnet\DefaultIcon" "" '"$INSTDIR\qbittorrent.exe",1'
  WriteRegStr HKCU "Software\Classes\magnet\shell" "" "open"
  WriteRegStr HKCU "Software\Classes\magnet\shell\open\command" "" '"$INSTDIR\qbittorrent.exe" "%1"'

FunctionEnd

Section $(inst_firewall)

  DetailPrint $(inst_firewallinfo)
  nsisFirewallW::AddAuthorizedApplication "$INSTDIR\qbittorrent.exe" "qBittorrent"

SectionEnd

;--------------------------------

Function .onInit

  !insertmacro Init "installer"
  !insertmacro MUI_LANGDLL_DISPLAY

  !ifdef APP64BIT
    ${IfNot} ${RunningX64}
      MessageBox MB_OK|MB_ICONEXCLAMATION $(inst_requires_64bit)
      Abort
    ${EndIf}
  !endif

  ;Search if qBittorrent is already installed.
  FindFirst $0 $1 "$INSTDIR\uninst.exe"
  FindClose $0
  StrCmp $1 "" done

  ;Copy old value to var so we can call the correct uninstaller
  StrCpy $uninstallerPath $INSTDIR

  ;Inform the user
  MessageBox MB_OKCANCEL|MB_ICONINFORMATION $(inst_uninstall_question) /SD IDOK IDOK done
  Quit

  done:

FunctionEnd

Function check_instance

  check:
  FindProcDLL::FindProc "qbittorrent.exe"
  StrCmp $R0 "1" 0 notfound
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION $(inst_warning) IDRETRY check IDCANCEL done

  done:
  Abort

  notfound:

FunctionEnd

Function PageFinishRun

  !insertmacro UAC_AsUser_ExecShell "" "$INSTDIR\qbittorrent.exe" "" "" ""

FunctionEnd

Function .onInstSuccess
  SetErrorLevel 0
FunctionEnd

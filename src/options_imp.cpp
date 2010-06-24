/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */
#include <QTextStream>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QSystemTrayIcon>
#include <QApplication>
#include <QSettings>
#include <QDialogButtonBox>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QStyleFactory>

#include <libtorrent/version.hpp>
#include <time.h>
#include <stdlib.h>

#include "options_imp.h"
#include "preferences.h"
#include "misc.h"
#include "advancedsettings.h"
#include "scannedfoldersmodel.h"

// Constructor
options_imp::options_imp(QWidget *parent):QDialog(parent){
  qDebug("-> Constructing Options");
  setAttribute(Qt::WA_DeleteOnClose);
  setModal(true);

  QString savePath;
  setupUi(this);
  hsplitter->setCollapsible(0, false);
  hsplitter->setCollapsible(1, false);
  // Get apply button in button box
  QList<QAbstractButton *> buttons = buttonBox->buttons();
  foreach(QAbstractButton *button, buttons){
    if(buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole){
      applyButton = button;
      break;
    }
  }

  scanFoldersView->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
  scanFoldersView->setModel(ScanFoldersModel::instance());
  connect(ScanFoldersModel::instance(), SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(enableApplyButton()));
  connect(scanFoldersView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(handleScanFolderViewSelectionChanged()));

  connect(buttonBox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(applySettings(QAbstractButton*)));
  comboStyle->addItems(QStyleFactory::keys());
  // Languages supported
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/united_kingdom.png"))), QString::fromUtf8("English"));
  locales << "en_GB";
  comboI18n->setCurrentIndex(0);
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/france.png"))), QString::fromUtf8("Français"));
  locales << "fr_FR";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/germany.png"))), QString::fromUtf8("Deutsch"));
  locales << "de_DE";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/hungary.png"))), QString::fromUtf8("Magyar"));
  locales << "hu_HU";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/italy.png"))), QString::fromUtf8("Italiano"));
  locales << "it_IT";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/netherlands.png"))), QString::fromUtf8("Nederlands"));
  locales << "nl_NL";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/spain.png"))), QString::fromUtf8("Español"));
  locales << "es_ES";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/spain_catalunya.png"))), QString::fromUtf8("Català"));
  locales << "ca_ES";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/portugal.png"))), QString::fromUtf8("Português"));
  locales << "pt_PT";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/brazil.png"))), QString::fromUtf8("Português brasileiro"));
  locales << "pt_BR";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/poland.png"))), QString::fromUtf8("Polski"));
  locales << "pl_PL";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/czech.png"))), QString::fromUtf8("Čeština"));
  locales << "cs_CZ";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/slovakia.png"))), QString::fromUtf8("Slovenčina"));
  locales << "sk_SK";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/croatia.png"))), QString::fromUtf8("Hrvatski"));
  locales << "hr_HR";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/serbia.png"))), QString::fromUtf8("Српски"));
  locales << "sr_CS";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/romania.png"))), QString::fromUtf8("Română"));
  locales << "ro_RO";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/turkey.png"))), QString::fromUtf8("Türkçe"));
  locales << "tr_TR";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/saoudi_arabia.png"))), QString::fromUtf8("عربي"));
  locales << "ar_SA";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/greece.png"))), QString::fromUtf8("Ελληνικά"));
  locales << "el_GR";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/sweden.png"))), QString::fromUtf8("Svenska"));
  locales << "sv_SE";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/finland.png"))), QString::fromUtf8("Suomi"));
  locales << "fi_FI";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/norway.png"))), QString::fromUtf8("Norsk"));
  locales << "nb_NO";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/denmark.png"))), QString::fromUtf8("Dansk"));
  locales << "da_DK";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/bulgaria.png"))), QString::fromUtf8("Български"));
  locales << "bg_BG";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/ukraine.png"))), QString::fromUtf8("Українська"));
  locales << "uk_UA";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/russia.png"))), QString::fromUtf8("Русский"));
  locales << "ru_RU";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/japan.png"))), QString::fromUtf8("日本語"));
  locales << "ja_JP";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/china.png"))), QString::fromUtf8("中文 (简体)"));
  locales << "zh_CN";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/taiwan.png"))), QString::fromUtf8("中文 (繁體)"));
  locales << "zh_TW";
  comboI18n->addItem((QIcon(QString::fromUtf8(":/Icons/flags/south_korea.png"))), QString::fromUtf8("한글"));
  locales << "ko_KR";


  // Load week days (scheduler)
  for(uint i=1; i<=7; ++i) {
#ifdef QT_4_5
    schedule_days->addItem(QDate::longDayName(i, QDate::StandaloneFormat));
#else
    schedule_days->addItem(QDate::longDayName(i));
#endif
  }

  // Load options
  loadOptions();
  // Disable systray integration if it is not supported by the system
#ifdef Q_WS_MAC
  if(1){
#else
  if(!QSystemTrayIcon::isSystemTrayAvailable()){
#endif
    checkNoSystray->setChecked(true);
    checkNoSystray->setEnabled(false);
  }
  // Connect signals / slots
  // General tab
  connect(checkNoSystray, SIGNAL(toggled(bool)), this, SLOT(setSystrayOptionsState(bool)));
  // Downloads tab
  connect(checkTempFolder, SIGNAL(toggled(bool)), this, SLOT(enableTempPathInput(bool)));
  connect(checkExportDir, SIGNAL(toggled(bool)), this, SLOT(enableTorrentExport(bool)));
  // Connection tab
  connect(checkUploadLimit, SIGNAL(toggled(bool)), this, SLOT(enableUploadLimit(bool)));
  connect(checkDownloadLimit,  SIGNAL(toggled(bool)), this, SLOT(enableDownloadLimit(bool)));
  connect(check_schedule, SIGNAL(toggled(bool)), this, SLOT(enableSchedulerFields(bool)));
  // Bittorrent tab
  connect(checkMaxConnecs,  SIGNAL(toggled(bool)), this, SLOT(enableMaxConnecsLimit(bool)));
  connect(checkMaxConnecsPerTorrent,  SIGNAL(toggled(bool)), this, SLOT(enableMaxConnecsLimitPerTorrent(bool)));
  connect(checkMaxUploadsPerTorrent,  SIGNAL(toggled(bool)), this, SLOT(enableMaxUploadsLimitPerTorrent(bool)));
  connect(checkRatioLimit,  SIGNAL(toggled(bool)), this, SLOT(enableShareRatio(bool)));
  connect(checkRatioRemove,  SIGNAL(toggled(bool)), this, SLOT(enableDeleteRatio(bool)));
  connect(checkDHT, SIGNAL(toggled(bool)), this, SLOT(enableDHTSettings(bool)));
  connect(checkDifferentDHTPort, SIGNAL(toggled(bool)), this, SLOT(enableDHTPortSettings(bool)));
  connect(comboPeerID, SIGNAL(currentIndexChanged(int)), this, SLOT(enableSpoofingSettings(int)));
  // Proxy tab
  connect(comboProxyType_http, SIGNAL(currentIndexChanged(int)),this, SLOT(enableHTTPProxy(int)));
  connect(checkProxyAuth_http,  SIGNAL(toggled(bool)), this, SLOT(enableHTTPProxyAuth(bool)));
  connect(comboProxyType, SIGNAL(currentIndexChanged(int)),this, SLOT(enablePeerProxy(int)));
  connect(checkProxyAuth,  SIGNAL(toggled(bool)), this, SLOT(enablePeerProxyAuth(bool)));
  // Misc tab
  connect(checkIPFilter, SIGNAL(toggled(bool)), this, SLOT(enableFilter(bool)));
  connect(checkEnableRSS, SIGNAL(toggled(bool)), this, SLOT(enableRSS(bool)));
  connect(checkEnableQueueing, SIGNAL(toggled(bool)), this, SLOT(enableQueueingSystem(bool)));
  // Web UI tab
  connect(checkWebUi,  SIGNAL(toggled(bool)), this, SLOT(enableWebUi(bool)));

  // Apply button is activated when a value is changed
  // General tab
  connect(comboI18n, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(comboStyle, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(checkConfirmExit, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkSpeedInTitle, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkAltRowColors, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkNoSystray, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkCloseToSystray, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkMinimizeToSysTray, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkStartMinimized, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkSystrayBalloons, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkDisplayToolbar, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkNoSplash, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  // Downloads tab
  connect(textSavePath, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkAppendLabel, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkAppendqB, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkPreallocateAll, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkAdditionDialog, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkStartPaused, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkExportDir, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(textExportDir, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(actionTorrentDlOnDblClBox, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(actionTorrentFnOnDblClBox, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(checkTempFolder, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(addScanFolderButton, SIGNAL(clicked()), this, SLOT(enableApplyButton()));
  connect(removeScanFolderButton, SIGNAL(clicked()), this, SLOT(enableApplyButton()));
  // Connection tab
  connect(spinPort, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkUPnP, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkNATPMP, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkUploadLimit, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkDownloadLimit, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(spinUploadLimit, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinDownloadLimit, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinUploadLimitAlt, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinDownloadLimitAlt, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(check_schedule, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(schedule_from, SIGNAL(timeChanged(QTime)), this, SLOT(enableApplyButton()));
  connect(schedule_to, SIGNAL(timeChanged(QTime)), this, SLOT(enableApplyButton()));
  connect(schedule_days, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  // Bittorrent tab
  connect(checkMaxConnecs, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkMaxConnecsPerTorrent, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkMaxUploadsPerTorrent, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(spinMaxConnec, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinMaxConnecPerTorrent, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinMaxUploadsPerTorrent, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkDHT, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkPeX, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkDifferentDHTPort, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(spinDHTPort, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkLSD, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(comboPeerID, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(client_version, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(client_build, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(comboEncryption, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(checkRatioLimit, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkRatioRemove, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(spinRatio, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinMaxRatio, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  // Proxy tab
  connect(comboProxyType_http, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(textProxyIP_http, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinProxyPort_http, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkProxyAuth_http, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(textProxyUsername_http, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(textProxyPassword_http, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(comboProxyType, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
  connect(textProxyIP, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinProxyPort, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkProxyAuth, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(textProxyUsername, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(textProxyPassword, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  // Misc tab
  connect(checkIPFilter, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(textFilterPath, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinRSSRefresh, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinRSSMaxArticlesPerFeed, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(checkEnableRSS, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(checkEnableQueueing, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(spinMaxActiveDownloads, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinMaxActiveUploads, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  connect(spinMaxActiveTorrents, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
  // Web UI tab
  connect(checkWebUi, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
  connect(spinWebUiPort, SIGNAL(valueChanged(int)), this, SLOT(enableApplyButton()));
  connect(textWebUiUsername, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  connect(textWebUiPassword, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
  // Disable apply Button
  applyButton->setEnabled(false);
#ifdef Q_WS_MAC
  if(1) {
#else
  if(!QSystemTrayIcon::supportsMessages()){
#endif
    // Mac OS X doesn't support it yet
    checkSystrayBalloons->setChecked(false);
    checkSystrayBalloons->setEnabled(false);
  }
  // Tab selection mecanism
  connect(tabSelection, SIGNAL(currentItemChanged(QListWidgetItem *, QListWidgetItem *)), this, SLOT(changePage(QListWidgetItem *, QListWidgetItem*)));
#if LIBTORRENT_VERSION_MINOR < 15
  checkAppendqB->setVisible(false);
#endif
  // Load Advanced settings
  QVBoxLayout *adv_layout = new QVBoxLayout();
  advancedSettings = new AdvancedSettings();
  adv_layout->addWidget(advancedSettings);
  scrollArea_advanced->setLayout(adv_layout);
  connect(advancedSettings, SIGNAL(settingsChanged()), this, SLOT(enableApplyButton()));
  // Adapt size
  show();
  loadWindowState();
}

// Main destructor
options_imp::~options_imp(){
  qDebug("-> destructing Options");
  foreach (const QString &path, addedScanDirs)
    ScanFoldersModel::instance()->removePath(path);
  delete scrollArea_advanced->layout();
  delete advancedSettings;
}

void options_imp::changePage(QListWidgetItem *current, QListWidgetItem *previous) {
  if (!current)
    current = previous;
  tabOption->setCurrentIndex(tabSelection->row(current));
}

void options_imp::useStyle() {
  QApplication::setStyle(QStyleFactory::create(comboStyle->itemText(comboStyle->currentIndex())));
}

void options_imp::loadWindowState() {
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  resize(settings.value(QString::fromUtf8("Preferences/State/size"), sizeFittingScreen()).toSize());
  QPoint p = settings.value(QString::fromUtf8("Preferences/State/pos"), QPoint()).toPoint();
  if(!p.isNull())
    move(p);
  // Load slider size
  const QStringList &sizes_str = settings.value("Preferences/State/hSplitterSizes", QStringList()).toStringList();
  // Splitter size
  QList<int> sizes;
  if(sizes_str.size() == 2) {
    sizes << sizes_str.first().toInt();
    sizes << sizes_str.last().toInt();
  } else {
    sizes << 130;
    sizes << hsplitter->width()-130;
  }
  hsplitter->setSizes(sizes);
}

void options_imp::saveWindowState() const {
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  settings.setValue(QString::fromUtf8("Preferences/State/size"), size());
  settings.setValue(QString::fromUtf8("Preferences/State/pos"), pos());
  // Splitter size
  QStringList sizes_str;
  sizes_str << QString::number(hsplitter->sizes().first());
  sizes_str << QString::number(hsplitter->sizes().last());
  settings.setValue(QString::fromUtf8("Preferences/State/hSplitterSizes"), sizes_str);
}

QSize options_imp::sizeFittingScreen() {
  int scrn = 0;
  QWidget *w = this->topLevelWidget();

  if(w)
    scrn = QApplication::desktop()->screenNumber(w);
  else if(QApplication::desktop()->isVirtualDesktop())
    scrn = QApplication::desktop()->screenNumber(QCursor::pos());
  else
    scrn = QApplication::desktop()->screenNumber(this);

  QRect desk(QApplication::desktop()->availableGeometry(scrn));
  if(width() > desk.width() || height() > desk.height()) {
    if(desk.width() > 0 && desk.height() > 0)
      return QSize(desk.width(), desk.height());
  }
  return size();
}

void options_imp::saveOptions(){
  applyButton->setEnabled(false);
  QSettings settings("qBittorrent", "qBittorrent");
  // Apply style
  useStyle();
  settings.beginGroup("Preferences");
  // General preferences
  settings.beginGroup("General");
  settings.setValue(QString::fromUtf8("Locale"), getLocale());
  settings.setValue(QString::fromUtf8("Style"), getStyle());
  settings.setValue(QString::fromUtf8("ExitConfirm"), confirmOnExit());
  settings.setValue(QString::fromUtf8("SpeedInTitleBar"), speedInTitleBar());
  settings.setValue(QString::fromUtf8("AlternatingRowColors"), checkAltRowColors->isChecked());
  settings.setValue(QString::fromUtf8("SystrayEnabled"), systrayIntegration());
  settings.setValue(QString::fromUtf8("CloseToTray"), closeToTray());
  settings.setValue(QString::fromUtf8("MinimizeToTray"), minimizeToTray());
  settings.setValue(QString::fromUtf8("StartMinimized"), startMinimized());
  settings.setValue(QString::fromUtf8("NotificationBaloons"), OSDEnabled());
  settings.setValue(QString::fromUtf8("ToolbarDisplayed"), isToolbarDisplayed());
  settings.setValue(QString::fromUtf8("NoSplashScreen"), isSlashScreenDisabled());
  // End General preferences
  settings.endGroup();
  // Downloads preferences
  settings.beginGroup("Downloads");
  QString save_path = getSavePath();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
  save_path = save_path.replace("\\", "/");
#endif
  settings.setValue(QString::fromUtf8("SavePath"), save_path);
  settings.setValue(QString::fromUtf8("TempPathEnabled"), isTempPathEnabled());
  QString temp_path = getTempPath();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
  temp_path = temp_path.replace("\\", "/");
#endif
  settings.setValue(QString::fromUtf8("TempPath"), temp_path);
  settings.setValue(QString::fromUtf8("AppendLabel"), checkAppendLabel->isChecked());
#if LIBTORRENT_VERSION_MINOR > 14
  settings.setValue(QString::fromUtf8("UseIncompleteExtension"), checkAppendqB->isChecked());
#endif
  settings.setValue(QString::fromUtf8("PreAllocation"), preAllocateAllFiles());
  settings.setValue(QString::fromUtf8("AdditionDialog"), useAdditionDialog());
  settings.setValue(QString::fromUtf8("StartInPause"), addTorrentsInPause());
  ScanFoldersModel::instance()->makePersistent(settings);
  addedScanDirs.clear();
  QString export_dir = getExportDir();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
  export_dir = export_dir.replace("\\", "/");
#endif
  Preferences::setExportDir(export_dir);
  settings.setValue(QString::fromUtf8("DblClOnTorDl"), getActionOnDblClOnTorrentDl());
  settings.setValue(QString::fromUtf8("DblClOnTorFn"), getActionOnDblClOnTorrentFn());
  // End Downloads preferences
  settings.endGroup();
  // Connection preferences
  settings.beginGroup("Connection");
  settings.setValue(QString::fromUtf8("PortRangeMin"), getPort());
  settings.setValue(QString::fromUtf8("UPnP"), isUPnPEnabled());
  settings.setValue(QString::fromUtf8("NAT-PMP"), isNATPMPEnabled());
  settings.setValue(QString::fromUtf8("GlobalDLLimit"), getGlobalBandwidthLimits().first);
  settings.setValue(QString::fromUtf8("GlobalUPLimit"), getGlobalBandwidthLimits().second);
  Preferences::setAltGlobalDownloadLimit(spinDownloadLimitAlt->value());
  Preferences::setAltGlobalUploadLimit(spinUploadLimitAlt->value());
  Preferences::setSchedulerEnabled(check_schedule->isChecked());
  Preferences::setSchedulerStartTime(schedule_from->time());
  Preferences::setSchedulerEndTime(schedule_to->time());
  Preferences::setSchedulerDays((scheduler_days)schedule_days->currentIndex());
  settings.setValue(QString::fromUtf8("ProxyType"), getPeerProxyType());
  //if(isProxyEnabled()) {
  settings.beginGroup("Proxy");
  // Proxy is enabled, save settings
  settings.setValue(QString::fromUtf8("IP"), getPeerProxyIp());
  settings.setValue(QString::fromUtf8("Port"), getPeerProxyPort());
  settings.setValue(QString::fromUtf8("Authentication"), isPeerProxyAuthEnabled());
  //if(isProxyAuthEnabled()) {
  // Credentials
  settings.setValue(QString::fromUtf8("Username"), getPeerProxyUsername());
  settings.setValue(QString::fromUtf8("Password"), getPeerProxyPassword());
  //}
  settings.endGroup(); // End Proxy
  //}
  settings.setValue(QString::fromUtf8("HTTPProxyType"), getHTTPProxyType());
  //if(isHTTPProxyEnabled()) {
  settings.beginGroup("HTTPProxy");
  // Proxy is enabled, save settings
  settings.setValue(QString::fromUtf8("IP"), getHTTPProxyIp());
  settings.setValue(QString::fromUtf8("Port"), getHTTPProxyPort());
  settings.setValue(QString::fromUtf8("Authentication"), isHTTPProxyAuthEnabled());
  //if(isHTTPProxyAuthEnabled()) {
  // Credentials
  settings.setValue(QString::fromUtf8("Username"), getHTTPProxyUsername());
  settings.setValue(QString::fromUtf8("Password"), getHTTPProxyPassword());
  //}
  settings.endGroup(); // End HTTPProxy
  //}
  // End Connection preferences
  settings.endGroup();
  // Bittorrent preferences
  settings.beginGroup("Bittorrent");
  settings.setValue(QString::fromUtf8("MaxConnecs"), getMaxConnecs());
  settings.setValue(QString::fromUtf8("MaxConnecsPerTorrent"), getMaxConnecsPerTorrent());
  settings.setValue(QString::fromUtf8("MaxUploadsPerTorrent"), getMaxUploadsPerTorrent());
  settings.setValue(QString::fromUtf8("DHT"), isDHTEnabled());
  settings.setValue(QString::fromUtf8("PeX"), checkPeX->isChecked());
  settings.setValue(QString::fromUtf8("sameDHTPortAsBT"), isDHTPortSameAsBT());
  settings.setValue(QString::fromUtf8("DHTPort"), getDHTPort());
  settings.setValue(QString::fromUtf8("LSD"), isLSDEnabled());
  // Peer ID usurpation
  switch(comboPeerID->currentIndex()) {
  case 3: // KTorrent
    Preferences::setPeerID("KT");
    Preferences::setClientVersion(client_version->text());
    break;
  case 2: // uTorrent
    Preferences::setPeerID("UT");
    Preferences::setClientVersion(client_version->text());
    Preferences::setClientBuild(client_build->text());
    break;
  case 1: // Vuze
    Preferences::setPeerID("AZ");
    Preferences::setClientVersion(client_version->text());
    break;
  default: //qBittorrent
    Preferences::setPeerID("qB");
  }
  settings.setValue(QString::fromUtf8("Encryption"), getEncryptionSetting());
  settings.setValue(QString::fromUtf8("DesiredRatio"), getDesiredRatio());
  settings.setValue(QString::fromUtf8("MaxRatio"), getDeleteRatio());
  // End Bittorrent preferences
  settings.endGroup();
  // Misc preferences
  // * IPFilter
  settings.beginGroup("IPFilter");
  settings.setValue(QString::fromUtf8("Enabled"), isFilteringEnabled());
  if(isFilteringEnabled()){
    QString filter_path = textFilterPath->text();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    filter_path = filter_path.replace("\\", "/");
#endif
    settings.setValue(QString::fromUtf8("File"), filter_path);
  }
  // End IPFilter preferences
  settings.endGroup();
  // RSS
  settings.beginGroup("RSS");
  settings.setValue(QString::fromUtf8("RSSEnabled"), isRSSEnabled());
  settings.setValue(QString::fromUtf8("RSSRefresh"), spinRSSRefresh->value());
  settings.setValue(QString::fromUtf8("RSSMaxArticlesPerFeed"), spinRSSMaxArticlesPerFeed->value());
  // End RSS preferences
  settings.endGroup();
  // Queueing system
  settings.beginGroup("Queueing");
  settings.setValue(QString::fromUtf8("QueueingEnabled"), isQueueingSystemEnabled());
  settings.setValue(QString::fromUtf8("MaxActiveDownloads"), spinMaxActiveDownloads->value());
  settings.setValue(QString::fromUtf8("MaxActiveUploads"), spinMaxActiveUploads->value());
  settings.setValue(QString::fromUtf8("MaxActiveTorrents"), spinMaxActiveTorrents->value());
  // End Queueing system preferences
  settings.endGroup();
  // Web UI
  settings.beginGroup("WebUI");
  settings.setValue("Enabled", isWebUiEnabled());
  if(isWebUiEnabled())
  {
    settings.setValue("Port", webUiPort());
    settings.setValue("Username", webUiUsername());
    // FIXME: Check that the password is valid (not empty at least)
    Preferences::setWebUiPassword(webUiPassword());
  }
  // End Web UI
  settings.endGroup();
  // End preferences
  settings.endGroup();

  // Save advanced settings
  advancedSettings->saveAdvancedSettings();
}

bool options_imp::isFilteringEnabled() const{
  return checkIPFilter->isChecked();
}

int options_imp::getPeerProxyType() const{
  switch(comboProxyType->currentIndex()) {
  case 1:
    return SOCKS4;
    break;
  case 2:
    if(isPeerProxyAuthEnabled()){
      return SOCKS5_PW;
    }
    return SOCKS5;
  case 3:
    if(isPeerProxyAuthEnabled()){
      return HTTP_PW;
    }
    return HTTP;
  default:
    return -1;
  }
}

int options_imp::getHTTPProxyType() const {
  switch(comboProxyType_http->currentIndex()) {
  case 1: {
      if(isHTTPProxyAuthEnabled()){
        return HTTP_PW;
      }
      return HTTP;
    }
  case 2: {
      if(isHTTPProxyAuthEnabled()) {
        return SOCKS5_PW;
      }
      return SOCKS5;
    }
  default:
    return -1; // Disabled
  }
}

QString options_imp::getStyle() const{
  return comboStyle->itemText(comboStyle->currentIndex());
}

void options_imp::setStyle(QString style) {
  int index = comboStyle->findText(style, Qt::MatchFixedString);
  if(index > 0)
    comboStyle->setCurrentIndex(index);
}

bool options_imp::isHTTPProxyAuthEnabled() const{
  return checkProxyAuth_http->isChecked();
}

void options_imp::loadOptions(){
  int intValue;
  float floatValue;
  QString strValue;
  // General preferences
  setLocale(Preferences::getLocale());
  setStyle(Preferences::getStyle());
  checkConfirmExit->setChecked(Preferences::confirmOnExit());
  checkSpeedInTitle->setChecked(Preferences::speedInTitleBar());
  checkAltRowColors->setChecked(Preferences::useAlternatingRowColors());
  checkNoSystray->setChecked(!Preferences::systrayIntegration());
  checkDisplayToolbar->setChecked(Preferences::isToolbarDisplayed());
  checkNoSplash->setChecked(Preferences::isSlashScreenDisabled());
  if(checkNoSystray->isChecked()) {
    disableSystrayOptions();
  } else {
    enableSystrayOptions();
    checkCloseToSystray->setChecked(Preferences::closeToTray());
    checkMinimizeToSysTray->setChecked(Preferences::minimizeToTray());
    checkStartMinimized->setChecked(Preferences::startMinimized());
    checkSystrayBalloons->setChecked(Preferences::OSDEnabled());
  }
  // End General preferences
  // Downloads preferences
  QString save_path = Preferences::getSavePath();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
  save_path = save_path.replace("/", "\\");
#endif
  textSavePath->setText(save_path);
  if(Preferences::isTempPathEnabled()) {
    // enable
    checkTempFolder->setChecked(true);
    enableTempPathInput(checkTempFolder->isChecked());
  } else {
    checkTempFolder->setChecked(false);
    enableTempPathInput(checkTempFolder->isChecked());
  }
  QString temp_path = Preferences::getTempPath();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
  temp_path = temp_path.replace("/", "\\");
#endif
  textTempPath->setText(temp_path);
  checkAppendLabel->setChecked(Preferences::appendTorrentLabel());
#if LIBTORRENT_VERSION_MINOR > 14
  checkAppendqB->setChecked(Preferences::useIncompleteFilesExtension());
#endif
  checkPreallocateAll->setChecked(Preferences::preAllocateAllFiles());
  checkAdditionDialog->setChecked(Preferences::useAdditionDialog());
  checkStartPaused->setChecked(Preferences::addTorrentsInPause());

  strValue = Preferences::getExportDir();
  if(strValue.isEmpty()) {
    // Disable
    checkExportDir->setChecked(false);
    enableTorrentExport(checkExportDir->isChecked());
  } else {
    // enable
    checkExportDir->setChecked(true);
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    strValue = strValue.replace("/", "\\");
#endif
    textExportDir->setText(strValue);
    enableTorrentExport(checkExportDir->isChecked());
  }

  intValue = Preferences::getActionOnDblClOnTorrentDl();
  if(intValue >= actionTorrentDlOnDblClBox->count())
    intValue = 0;
  actionTorrentDlOnDblClBox->setCurrentIndex(intValue);
  intValue = Preferences::getActionOnDblClOnTorrentFn();
  if(intValue >= actionTorrentFnOnDblClBox->count())
    intValue = 1;
  actionTorrentFnOnDblClBox->setCurrentIndex(intValue);
  // End Downloads preferences
  // Connection preferences
  spinPort->setValue(Preferences::getSessionPort());
  checkUPnP->setChecked(Preferences::isUPnPEnabled());
  checkNATPMP->setChecked(Preferences::isNATPMPEnabled());
  intValue = Preferences::getGlobalDownloadLimit();
  if(intValue > 0) {
    // Enabled
    checkDownloadLimit->setChecked(true);
    spinDownloadLimit->setEnabled(true);
    spinDownloadLimit->setValue(intValue);
  } else {
    // Disabled
    checkDownloadLimit->setChecked(false);
    spinDownloadLimit->setEnabled(false);
  }
  intValue = Preferences::getGlobalUploadLimit();
  if(intValue != -1) {
    // Enabled
    checkUploadLimit->setChecked(true);
    spinUploadLimit->setEnabled(true);
    spinUploadLimit->setValue(intValue);
  } else {
    // Disabled
    checkUploadLimit->setChecked(false);
    spinUploadLimit->setEnabled(false);
  }
  spinUploadLimitAlt->setValue(Preferences::getAltGlobalUploadLimit());
  spinDownloadLimitAlt->setValue(Preferences::getAltGlobalDownloadLimit());
  // Scheduler
  check_schedule->setChecked(Preferences::isSchedulerEnabled());
  enableSchedulerFields(check_schedule->isChecked());
  schedule_from->setTime(Preferences::getSchedulerStartTime());
  schedule_to->setTime(Preferences::getSchedulerEndTime());
  schedule_days->setCurrentIndex((int)Preferences::getSchedulerDays());

  intValue = Preferences::getPeerProxyType();
  switch(intValue) {
  case SOCKS4:
    comboProxyType->setCurrentIndex(1);
    break;
  case SOCKS5:
  case SOCKS5_PW:
    comboProxyType->setCurrentIndex(2);
    break;
  case HTTP:
  case HTTP_PW:
    comboProxyType->setCurrentIndex(3);
    break;
  default:
    comboProxyType->setCurrentIndex(0);
  }
  enablePeerProxy(comboProxyType->currentIndex());
  //if(isProxyEnabled()) {
  // Proxy is enabled, save settings
  textProxyIP->setText(Preferences::getPeerProxyIp());
  spinProxyPort->setValue(Preferences::getPeerProxyPort());
  checkProxyAuth->setChecked(Preferences::isPeerProxyAuthEnabled());
  textProxyUsername->setText(Preferences::getPeerProxyUsername());
  textProxyPassword->setText(Preferences::getPeerProxyPassword());
  enablePeerProxyAuth(checkProxyAuth->isChecked());
  //}
  intValue = Preferences::getHTTPProxyType();
  switch(intValue) {
  case HTTP:
  case HTTP_PW:
    comboProxyType_http->setCurrentIndex(1);
    break;
  case SOCKS5:
  case SOCKS5_PW:
    comboProxyType_http->setCurrentIndex(2);
    break;
  default:
    comboProxyType_http->setCurrentIndex(0);
  }
  enableHTTPProxy(comboProxyType_http->currentIndex());
  textProxyUsername_http->setText(Preferences::getHTTPProxyUsername());
  textProxyPassword_http->setText(Preferences::getHTTPProxyPassword());
  textProxyIP_http->setText(Preferences::getHTTPProxyIp());
  spinProxyPort_http->setValue(Preferences::getHTTPProxyPort());
  checkProxyAuth_http->setChecked(Preferences::isHTTPProxyAuthEnabled());
  enableHTTPProxyAuth(checkProxyAuth_http->isChecked());
  // End HTTPProxy
  // End Connection preferences
  // Bittorrent preferences
  intValue = Preferences::getMaxConnecs();
  if(intValue > 0) {
    // enable
    checkMaxConnecs->setChecked(true);
    spinMaxConnec->setEnabled(true);
    spinMaxConnec->setValue(intValue);
  } else {
    // disable
    checkMaxConnecs->setChecked(false);
    spinMaxConnec->setEnabled(false);
  }
  intValue = Preferences::getMaxConnecsPerTorrent();
  if(intValue > 0) {
    // enable
    checkMaxConnecsPerTorrent->setChecked(true);
    spinMaxConnecPerTorrent->setEnabled(true);
    spinMaxConnecPerTorrent->setValue(intValue);
  } else {
    // disable
    checkMaxConnecsPerTorrent->setChecked(false);
    spinMaxConnecPerTorrent->setEnabled(false);
  }
  intValue = Preferences::getMaxUploadsPerTorrent();
  if(intValue > 0) {
    // enable
    checkMaxUploadsPerTorrent->setChecked(true);
    spinMaxUploadsPerTorrent->setEnabled(true);
    spinMaxUploadsPerTorrent->setValue(intValue);
  } else {
    // disable
    checkMaxUploadsPerTorrent->setChecked(false);
    spinMaxUploadsPerTorrent->setEnabled(false);
  }
  checkDHT->setChecked(Preferences::isDHTEnabled());
  enableDHTSettings(checkDHT->isChecked());
  checkDifferentDHTPort->setChecked(!Preferences::isDHTPortSameAsBT());
  enableDHTPortSettings(checkDifferentDHTPort->isChecked());
  spinDHTPort->setValue(Preferences::getDHTPort());
  checkPeX->setChecked(Preferences::isPeXEnabled());
  checkLSD->setChecked(Preferences::isLSDEnabled());
  // Peer ID usurpation
  QString peer_id = Preferences::getPeerID();
  if(peer_id == "UT") {
    // uTorrent
    comboPeerID->setCurrentIndex(2);
    enableSpoofingSettings(2);
    client_version->setText(Preferences::getClientVersion());
    client_build->setText(Preferences::getClientBuild());
  } else {
    if(peer_id == "AZ") {
      // Vuze
      comboPeerID->setCurrentIndex(1);
      enableSpoofingSettings(1);
      client_version->setText(Preferences::getClientVersion());
    } else {
      if(peer_id == "KT") {
        comboPeerID->setCurrentIndex(3);
        enableSpoofingSettings(3);
        client_version->setText(Preferences::getClientVersion());
      } else {
        // qBittorrent
        comboPeerID->setCurrentIndex(0);
        enableSpoofingSettings(0);
      }
    }
  }
  comboEncryption->setCurrentIndex(Preferences::getEncryptionSetting());
  floatValue = Preferences::getDesiredRatio();
  if(floatValue >= 1.) {
    // Enable
    checkRatioLimit->setChecked(true);
    spinRatio->setEnabled(true);
    spinRatio->setValue(floatValue);
  } else {
    // Disable
    checkRatioLimit->setChecked(false);
    spinRatio->setEnabled(false);
  }
  floatValue = Preferences::getDeleteRatio();
  if(floatValue >= 1.) {
    // Enable
    checkRatioRemove->setChecked(true);
    spinMaxRatio->setEnabled(true);
    spinMaxRatio->setValue(floatValue);
  } else {
    // Disable
    checkRatioRemove->setChecked(false);
    spinMaxRatio->setEnabled(false);
  }
  // End Bittorrent preferences
  // Misc preferences
  // * IP Filter
  checkIPFilter->setChecked(Preferences::isFilteringEnabled());
  enableFilter(checkIPFilter->isChecked());
  textFilterPath->setText(Preferences::getFilter());
  // End IP Filter
  // * RSS
  checkEnableRSS->setChecked(Preferences::isRSSEnabled());
  enableRSS(checkEnableRSS->isChecked());
  spinRSSRefresh->setValue(Preferences::getRSSRefreshInterval());
  spinRSSMaxArticlesPerFeed->setValue(Preferences::getRSSMaxArticlesPerFeed());
  // End RSS preferences
  // Queueing system preferences
  checkEnableQueueing->setChecked(Preferences::isQueueingSystemEnabled());
  enableQueueingSystem(checkEnableQueueing->isChecked());
  spinMaxActiveDownloads->setValue(Preferences::getMaxActiveDownloads());
  spinMaxActiveUploads->setValue(Preferences::getMaxActiveUploads());
  spinMaxActiveTorrents->setValue(Preferences::getMaxActiveTorrents());
  // End Queueing system preferences
  // Web UI
  checkWebUi->setChecked(Preferences::isWebUiEnabled());
  enableWebUi(checkWebUi->isChecked());
  spinWebUiPort->setValue(Preferences::getWebUiPort());
  textWebUiUsername->setText(Preferences::getWebUiUsername());
  textWebUiPassword->setText(Preferences::getWebUiPassword());
  // End Web UI
  // Random stuff
  srand(time(0));
}

// return min & max ports
// [min, max]
int options_imp::getPort() const{
  return spinPort->value();
}

void options_imp::enableSpoofingSettings(int index) {
  switch(index) {
  case 0: // qBittorrent
    resetPeerVersion_button->setEnabled(false);
    version_label->setEnabled(false);
    client_version->setEnabled(false);
    client_version->clear();
    build_label->setEnabled(false);
    client_build->setEnabled(false);
    client_build->clear();
    break;
  case 1: // Vuze
    resetPeerVersion_button->setEnabled(true);
    version_label->setEnabled(true);
    client_version->setEnabled(true);
    client_version->setText(Preferences::getDefaultClientVersion("AZ"));
    build_label->setEnabled(false);
    client_build->setEnabled(false);
    client_build->clear();
    break;
  case 2: // uTorrent
    resetPeerVersion_button->setEnabled(true);
    version_label->setEnabled(true);
    client_version->setEnabled(true);
    client_version->setText(Preferences::getDefaultClientVersion("UT"));
    build_label->setEnabled(true);
    client_build->setEnabled(true);
    client_build->setText(Preferences::getDefaultClientBuild("UT"));
    break;
  case 3: // KTorrent
    resetPeerVersion_button->setEnabled(true);
    version_label->setEnabled(true);
    client_version->setEnabled(true);
    client_version->setText(Preferences::getDefaultClientVersion("KT"));
    build_label->setEnabled(false);
    client_build->setEnabled(false);
    client_build->clear();
    break;
  }
}

void options_imp::on_resetPeerVersion_button_clicked() {
  switch(comboPeerID->currentIndex()) {
  case 1: // Vuze
    client_version->setText(Preferences::getDefaultClientVersion("AZ"));
    break;
  case 3: // KTorrent
    client_version->setText(Preferences::getDefaultClientVersion("KT"));
    break;
  case 2: // uTorrent
    client_version->setText(Preferences::getDefaultClientVersion("UT"));
    client_build->setText(Preferences::getDefaultClientBuild("UT"));
    break;
  }
}

void options_imp::on_randomButton_clicked() {
  // Range [1024: 65535]
  spinPort->setValue(rand() % 64512 + 1024);
}

int options_imp::getEncryptionSetting() const{
  return comboEncryption->currentIndex();
}

int options_imp::getMaxActiveDownloads() const {
  return spinMaxActiveDownloads->value();
}

int options_imp::getMaxActiveUploads() const {
  return spinMaxActiveUploads->value();
}

int options_imp::getMaxActiveTorrents() const {
  return spinMaxActiveTorrents->value();
}

bool options_imp::minimizeToTray() const{
  if(checkNoSystray->isChecked()) return false;
  return checkMinimizeToSysTray->isChecked();
}

bool options_imp::closeToTray() const{
  if(checkNoSystray->isChecked()) return false;
  return checkCloseToSystray->isChecked();
}

bool options_imp::confirmOnExit() const{
  return checkConfirmExit->isChecked();
}

bool options_imp::isQueueingSystemEnabled() const {
  return checkEnableQueueing->isChecked();
}

bool options_imp::isDHTEnabled() const{
  return checkDHT->isChecked();
}

bool options_imp::isRSSEnabled() const{
  return checkEnableRSS->isChecked();
}

bool options_imp::isLSDEnabled() const{
  return checkLSD->isChecked();
}

bool options_imp::isUPnPEnabled() const{
  return checkUPnP->isChecked();
}

bool options_imp::isNATPMPEnabled() const{
  return checkNATPMP->isChecked();
}

// Return Download & Upload limits in kbps
// [download,upload]
QPair<int,int> options_imp::getGlobalBandwidthLimits() const{
  int DL = -1, UP = -1;
  if(checkDownloadLimit->isChecked()){
    DL = spinDownloadLimit->value();
  }
  if(checkUploadLimit->isChecked()){
    UP = spinUploadLimit->value();
  }
  return qMakePair(DL, UP);
}

bool options_imp::OSDEnabled() const {
  if(checkNoSystray->isChecked()) return false;
  return checkSystrayBalloons->isChecked();
}

bool options_imp::startMinimized() const {
  if(checkStartMinimized->isChecked()) return true;
  return checkStartMinimized->isChecked();
}

bool options_imp::systrayIntegration() const{
#ifdef Q_WS_MAC
  return false;
#endif
  if (!QSystemTrayIcon::isSystemTrayAvailable()) return false;
  return (!checkNoSystray->isChecked());
}

int options_imp::getDHTPort() const {
  return spinDHTPort->value();
}

// Return Share ratio
float options_imp::getDesiredRatio() const{
  if(checkRatioLimit->isChecked()){
    return spinRatio->value();
  }
  return -1;
}

// Return Share ratio
float options_imp::getDeleteRatio() const{
  if(checkRatioRemove->isChecked()){
    return spinMaxRatio->value();
  }
  return -1;
}

// Return Save Path
QString options_imp::getSavePath() const{
  if(textSavePath->text().trimmed().isEmpty()){
    QString save_path = Preferences::getSavePath();
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    save_path = save_path.replace("/", "\\");
#endif
    textSavePath->setText(save_path);
  }
  return misc::expandPath(textSavePath->text());
}

QString options_imp::getTempPath() const {
  return misc::expandPath(textTempPath->text());
}

bool options_imp::isTempPathEnabled() const {
  return checkTempFolder->isChecked();
}

// Return max connections number
int options_imp::getMaxConnecs() const{
  if(!checkMaxConnecs->isChecked()){
    return -1;
  }else{
    return spinMaxConnec->value();
  }
}

int options_imp::getMaxConnecsPerTorrent() const{
  if(!checkMaxConnecsPerTorrent->isChecked()){
    return -1;
  }else{
    return spinMaxConnecPerTorrent->value();
  }
}

int options_imp::getMaxUploadsPerTorrent() const{
  if(!checkMaxUploadsPerTorrent->isChecked()){
    return -1;
  }else{
    return spinMaxUploadsPerTorrent->value();
  }
}

void options_imp::on_buttonBox_accepted(){
  if(applyButton->isEnabled()){
    saveOptions();
    applyButton->setEnabled(false);
    this->hide();
    emit status_changed();
  }
  saveWindowState();
  accept();
}

void options_imp::applySettings(QAbstractButton* button) {
  if(button == applyButton){
    saveOptions();
    emit status_changed();
  }
}

void options_imp::closeEvent(QCloseEvent *e){
  setAttribute(Qt::WA_DeleteOnClose);
  e->accept();
}

void options_imp::on_buttonBox_rejected(){
  setAttribute(Qt::WA_DeleteOnClose);
  reject();
}

void options_imp::enableDownloadLimit(bool checked){
  if(checked){
    spinDownloadLimit->setEnabled(true);
  }else{
    spinDownloadLimit->setEnabled(false);
  }
}

void options_imp::enableTempPathInput(bool checked){
  if(checked){
    textTempPath->setEnabled(true);
    browseTempDirButton->setEnabled(true);
  }else{
    textTempPath->setEnabled(false);
    browseTempDirButton->setEnabled(false);
  }
}

bool options_imp::useAdditionDialog() const{
  return checkAdditionDialog->isChecked();
}

void options_imp::enableSchedulerFields(bool checked) {
  schedule_from->setEnabled(checked);
  schedule_to->setEnabled(checked);
  schedule_days->setEnabled(checked);
}

void options_imp::enableMaxConnecsLimit(bool checked){
  spinMaxConnec->setEnabled(checked);
}

void options_imp::enableQueueingSystem(bool checked) {
  spinMaxActiveDownloads->setEnabled(checked);
  spinMaxActiveUploads->setEnabled(checked);
  label_max_active_dl->setEnabled(checked);
  label_max_active_up->setEnabled(checked);
  maxActiveTorrents_lbl->setEnabled(checked);
  spinMaxActiveTorrents->setEnabled(checked);
}

void options_imp::enableMaxConnecsLimitPerTorrent(bool checked){
  spinMaxConnecPerTorrent->setEnabled(checked);
}

void options_imp::enableSystrayOptions() {
  checkCloseToSystray->setEnabled(true);
  checkMinimizeToSysTray->setEnabled(true);
  checkSystrayBalloons->setEnabled(true);
}

void options_imp::disableSystrayOptions() {
  checkCloseToSystray->setEnabled(false);
  checkMinimizeToSysTray->setEnabled(false);
  checkSystrayBalloons->setEnabled(false);
}

void options_imp::setSystrayOptionsState(bool checked) {
  if(checked) {
    disableSystrayOptions();
  } else {
    enableSystrayOptions();
  }
}

void options_imp::enableMaxUploadsLimitPerTorrent(bool checked){
  if(checked){
    spinMaxUploadsPerTorrent->setEnabled(true);
  }else{
    spinMaxUploadsPerTorrent->setEnabled(false);
  }
}

void options_imp::enableFilter(bool checked){
  lblFilterPath->setEnabled(checked);
  textFilterPath->setEnabled(checked);
  browseFilterButton->setEnabled(checked);
}

void options_imp::enableRSS(bool checked) {
  groupRSSSettings->setEnabled(checked);
}

void options_imp::enableUploadLimit(bool checked){
  spinUploadLimit->setEnabled(checked);
}

void options_imp::enableApplyButton(){
  applyButton->setEnabled(true);
}

void options_imp::enableShareRatio(bool checked){
  spinRatio->setEnabled(checked);
}

void options_imp::enableDHTPortSettings(bool checked) {
  spinDHTPort->setEnabled(checked);
  dh_port_lbl->setEnabled(checked);
}

void options_imp::enableDHTSettings(bool checked) {
  if(checked){
    checkDifferentDHTPort->setEnabled(true);
    enableDHTPortSettings(checkDifferentDHTPort->isChecked());
  }else{
    checkDifferentDHTPort->setEnabled(false);
    enableDHTPortSettings(false);
  }
}


void options_imp::enableDeleteRatio(bool checked){
  spinMaxRatio->setEnabled(checked);
}

void options_imp::enablePeerProxy(int index){
  if(index){
    //enable
    lblProxyIP->setEnabled(true);
    textProxyIP->setEnabled(true);
    lblProxyPort->setEnabled(true);
    spinProxyPort->setEnabled(true);
    if(index > 1) {
      checkProxyAuth->setEnabled(true);
    } else {
      checkProxyAuth->setEnabled(false);
      checkProxyAuth->setChecked(false);
    }
  }else{
    //disable
    lblProxyIP->setEnabled(false);
    textProxyIP->setEnabled(false);
    lblProxyPort->setEnabled(false);
    spinProxyPort->setEnabled(false);
    checkProxyAuth->setEnabled(false);
    checkProxyAuth->setChecked(false);
  }
}

void options_imp::enableHTTPProxy(int index){
  bool enable = (index > 0);
  lblProxyIP_http->setEnabled(enable);
  textProxyIP_http->setEnabled(enable);
  lblProxyPort_http->setEnabled(enable);
  spinProxyPort_http->setEnabled(enable);
  checkProxyAuth_http->setEnabled(enable);

  if(!enable)
    checkProxyAuth_http->setChecked(false);
}

void options_imp::enablePeerProxyAuth(bool checked){
  lblProxyUsername->setEnabled(checked);
  lblProxyPassword->setEnabled(checked);
  textProxyUsername->setEnabled(checked);
  textProxyPassword->setEnabled(checked);
}

void options_imp::enableHTTPProxyAuth(bool checked){
  lblProxyUsername_http->setEnabled(checked);
  lblProxyPassword_http->setEnabled(checked);
  textProxyUsername_http->setEnabled(checked);
  textProxyPassword_http->setEnabled(checked);
}

void options_imp::enableTorrentExport(bool checked) {
  textExportDir->setEnabled(checked);
  browseExportDirButton->setEnabled(checked);
}

bool options_imp::isSlashScreenDisabled() const {
  return checkNoSplash->isChecked();
}

bool options_imp::speedInTitleBar() const {
  return checkSpeedInTitle->isChecked();
}

bool options_imp::preAllocateAllFiles() const {
  return checkPreallocateAll->isChecked();
}

bool options_imp::addTorrentsInPause() const {
  return checkStartPaused->isChecked();
}

bool options_imp::isDHTPortSameAsBT() const {
  return !checkDifferentDHTPort->isChecked();
}

// Proxy settings
bool options_imp::isPeerProxyEnabled() const{
  return comboProxyType->currentIndex();
}

bool options_imp::isHTTPProxyEnabled() const {
  return comboProxyType_http->currentIndex();
}

bool options_imp::isPeerProxyAuthEnabled() const{
  return checkProxyAuth->isChecked();
}

QString options_imp::getPeerProxyIp() const{
  QString ip = textProxyIP->text();
  ip = ip.trimmed();
  return ip;
}

QString options_imp::getHTTPProxyIp() const{
  QString ip = textProxyIP_http->text();
  ip = ip.trimmed();
  return ip;
}

unsigned short options_imp::getPeerProxyPort() const{
  return spinProxyPort->value();
}

unsigned short options_imp::getHTTPProxyPort() const{
  return spinProxyPort_http->value();
}

QString options_imp::getPeerProxyUsername() const{
  QString username = textProxyUsername->text();
  username = username.trimmed();
  return username;
}

QString options_imp::getHTTPProxyUsername() const{
  QString username = textProxyUsername_http->text();
  username = username.trimmed();
  return username;
}

QString options_imp::getPeerProxyPassword() const{
  QString password = textProxyPassword->text();
  password = password.trimmed();
  return password;
}

QString options_imp::getHTTPProxyPassword() const{
  QString password = textProxyPassword_http->text();
  password = password.trimmed();
  return password;
}

// Locale Settings
QString options_imp::getLocale() const{
  return locales.at(comboI18n->currentIndex());
}

void options_imp::setLocale(QString locale){
  int indexLocales=locales.indexOf(QRegExp(locale));
  if(indexLocales != -1){
    comboI18n->setCurrentIndex(indexLocales);
  }
}

QString options_imp::getExportDir() const {
  if(checkExportDir->isChecked()){
    return misc::expandPath(textExportDir->text());
  }else{
    return QString::null;
  }
}

// Return action on double-click on a downloading torrent set in options
int options_imp::getActionOnDblClOnTorrentDl() const {
  if(actionTorrentDlOnDblClBox->currentIndex()<1)
    return 0;
  return actionTorrentDlOnDblClBox->currentIndex();
}

// Return action on double-click on a finished torrent set in options
int options_imp::getActionOnDblClOnTorrentFn() const {
  if(actionTorrentFnOnDblClBox->currentIndex()<1)
    return 0;
  return actionTorrentFnOnDblClBox->currentIndex();
}

void options_imp::on_addScanFolderButton_clicked() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Add directory to scan"));
  if (!dir.isEmpty()) {
    const ScanFoldersModel::PathStatus status = ScanFoldersModel::instance()->addPath(dir, false);
    QString error;
    switch (status) {
    case ScanFoldersModel::AlreadyInList:
      error = tr("Folder is already being watched.").arg(dir);
      break;
    case ScanFoldersModel::DoesNotExist:
      error = tr("Folder does not exist.");
      break;
    case ScanFoldersModel::CannotRead:
      error = tr("Folder is not readable.");
      break;
    default:
      addedScanDirs << dir;
      enableApplyButton();
    }

    if (!error.isEmpty()) {
      QMessageBox::warning(this, tr("Failure"), tr("Failed to add Scan Folder '%1': %2").arg(dir).arg(error));
    }
  }
}

void options_imp::on_removeScanFolderButton_clicked() {
  const QModelIndexList &selected
      = scanFoldersView->selectionModel()->selectedIndexes();
  if (selected.isEmpty())
    return;
  Q_ASSERT(selected.count() == ScanFoldersModel::instance()->columnCount());
  ScanFoldersModel::instance()->removePath(selected.first().row());
}

void options_imp::handleScanFolderViewSelectionChanged() {
  removeScanFolderButton->setEnabled(!scanFoldersView->selectionModel()->selectedIndexes().isEmpty());
}

void options_imp::on_browseExportDirButton_clicked() {
  QString export_path = misc::expandPath(textExportDir->text());
  QDir exportDir(export_path);
  QString dir;
  if(!export_path.isEmpty() && exportDir.exists()) {
    dir = QFileDialog::getExistingDirectory(this, tr("Choose export directory"), exportDir.absolutePath());
  } else {
    dir = QFileDialog::getExistingDirectory(this, tr("Choose export directory"), QDir::homePath());
  }
  if(!dir.isNull()){
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    dir = dir.replace("/", "\\");
#endif
    textExportDir->setText(dir);
  }
}

void options_imp::on_browseFilterButton_clicked() {
  QString filter_path = misc::expandPath(textFilterPath->text());
  QDir filterDir(filter_path);
  QString ipfilter;
  if(!filter_path.isEmpty() && filterDir.exists()) {
    ipfilter = QFileDialog::getOpenFileName(this, tr("Choose an ip filter file"), filterDir.absolutePath(), tr("Filters")+QString(" (*.dat *.p2p *.p2b)"));
  } else {
    ipfilter = QFileDialog::getOpenFileName(this, tr("Choose an ip filter file"), QDir::homePath(), tr("Filters")+QString(" (*.dat *.p2p *.p2b)"));
  }
  if(!ipfilter.isNull()){
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    ipfilter = ipfilter.replace("/", "\\");
#endif
    textFilterPath->setText(ipfilter);
  }
}

// Display dialog to choose save dir
void options_imp::on_browseSaveDirButton_clicked(){
  QString save_path = misc::expandPath(textSavePath->text());
  QDir saveDir(save_path);
  QString dir;
  if(!save_path.isEmpty() && saveDir.exists()) {
    dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), saveDir.absolutePath());
  } else {
    dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), QDir::homePath());
  }
  if(!dir.isNull()){
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    dir = dir.replace("/", "\\");
#endif
    textSavePath->setText(dir);
  }
}

void options_imp::on_browseTempDirButton_clicked(){
  QString temp_path = misc::expandPath(textTempPath->text());
  QDir tempDir(temp_path);
  QString dir;
  if(!temp_path.isEmpty() && tempDir.exists()) {
    dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), tempDir.absolutePath());
  } else {
    dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), QDir::homePath());
  }
  if(!dir.isNull()){
#if defined(Q_WS_WIN) || defined(Q_OS_OS2)
    dir = dir.replace("/", "\\");
#endif
    textTempPath->setText(dir);
  }
}

// Return Filter object to apply to BT session
QString options_imp::getFilter() const{
  return textFilterPath->text();
}

bool options_imp::isToolbarDisplayed() const {
  return checkDisplayToolbar->isChecked();
}

// Web UI

void options_imp::enableWebUi(bool checkBoxValue){
  groupWebUiServer->setEnabled(checkBoxValue);
  groupWebUiAuth->setEnabled(checkBoxValue);
}

bool options_imp::isWebUiEnabled() const
{
  return checkWebUi->isChecked();
}

quint16 options_imp::webUiPort() const
{
  return spinWebUiPort->value();
}

QString options_imp::webUiUsername() const
{
  return textWebUiUsername->text();
}

QString options_imp::webUiPassword() const
{
  return textWebUiPassword->text();
}

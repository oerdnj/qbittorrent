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

#include "optionsdlg.h"

#include <cstdlib>

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTranslator>

#ifndef QT_NO_OPENSSL
#include <QSslCertificate>
#include <QSslKey>
#endif

#include "app/application.h"
#include "base/bittorrent/session.h"
#include "base/net/dnsupdater.h"
#include "base/preferences.h"
#include "base/scanfoldersmodel.h"
#include "base/torrentfileguard.h"
#include "base/unicodestrings.h"
#include "base/utils/fs.h"
#include "addnewtorrentdialog.h"
#include "advancedsettings.h"
#include "guiiconprovider.h"
#include "scanfoldersdelegate.h"

#include "ui_optionsdlg.h"

// Constructor
OptionsDialog::OptionsDialog(QWidget *parent)
    : QDialog(parent)
    , m_refreshingIpFilter(false)
    , m_ui(new Ui::OptionsDialog)
{
    qDebug("-> Constructing Options");
    m_ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);

    // Icons
    m_ui->tabSelection->item(TAB_UI)->setIcon(GuiIconProvider::instance()->getIcon("preferences-desktop"));
    m_ui->tabSelection->item(TAB_BITTORRENT)->setIcon(GuiIconProvider::instance()->getIcon("preferences-system-network"));
    m_ui->tabSelection->item(TAB_CONNECTION)->setIcon(GuiIconProvider::instance()->getIcon("network-wired"));
    m_ui->tabSelection->item(TAB_DOWNLOADS)->setIcon(GuiIconProvider::instance()->getIcon("download"));
    m_ui->tabSelection->item(TAB_SPEED)->setIcon(GuiIconProvider::instance()->getIcon("chronometer"));
#ifndef DISABLE_WEBUI
    m_ui->tabSelection->item(TAB_WEBUI)->setIcon(GuiIconProvider::instance()->getIcon("network-server"));
#else
    m_ui->tabSelection->item(TAB_WEBUI)->setHidden(true);
#endif
    m_ui->tabSelection->item(TAB_ADVANCED)->setIcon(GuiIconProvider::instance()->getIcon("preferences-other"));
    for (int i = 0; i < m_ui->tabSelection->count(); ++i) {
        m_ui->tabSelection->item(i)->setSizeHint(QSize(std::numeric_limits<int>::max(), 64));  // uniform size for all icons
    }

    m_ui->IpFilterRefreshBtn->setIcon(GuiIconProvider::instance()->getIcon("view-refresh"));

    m_ui->deleteTorrentWarningIcon->setPixmap(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(16, 16));
    m_ui->deleteTorrentWarningIcon->hide();
    m_ui->deleteTorrentWarningLabel->hide();
    m_ui->deleteTorrentWarningLabel->setToolTip(QLatin1String("<html><body><p>") +
        tr("By enabling these options, you can <strong>irrevocably lose</strong> your .torrent files!") +
        QLatin1String("</p><p>") +
        tr("When these options are enabled, qBittorent will <strong>delete</strong> .torrent files "
        "after they were successfully (the first option) or not (the second option) added to its "
        "download queue. This will be applied <strong>not only</strong> to the files opened via "
        "&ldquo;Add torrent&rdquo; menu action but to those opened via <strong>file type association</strong> as well") +
        QLatin1String("</p><p>") +
        tr("If you enable the second option (&ldquo;Also when addition is cancelled&rdquo;) the "
        ".torrent file <strong>will be deleted</strong> even if you press &ldquo;<strong>Cancel</strong>&rdquo; in "
        "the &ldquo;Add torrent&rdquo; dialog") +
        QLatin1String("</p></body></html>"));

    m_ui->hsplitter->setCollapsible(0, false);
    m_ui->hsplitter->setCollapsible(1, false);
    // Get apply button in button box
    QList<QAbstractButton *> buttons = m_ui->buttonBox->buttons();
    foreach (QAbstractButton *button, buttons) {
        if (m_ui->buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) {
            applyButton = button;
            break;
        }
    }

#ifndef QBT_USES_QT5
    m_ui->scanFoldersView->header()->setResizeMode(QHeaderView::ResizeToContents);
#else
    m_ui->scanFoldersView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#endif
    m_ui->scanFoldersView->setModel(ScanFoldersModel::instance());
    m_ui->scanFoldersView->setItemDelegate(new ScanFoldersDelegate(this, m_ui->scanFoldersView));
    connect(ScanFoldersModel::instance(), SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(enableApplyButton()));
    connect(m_ui->scanFoldersView->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this, SLOT(handleScanFolderViewSelectionChanged()));

    connect(m_ui->buttonBox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(applySettings(QAbstractButton*)));
    // Languages supported
    initializeLanguageCombo();

    // Load week days (scheduler)
    for (uint i = 1; i <= 7; ++i)
        m_ui->schedule_days->addItem(QDate::longDayName(i, QDate::StandaloneFormat));

    // Load options
    loadOptions();
    // Disable systray integration if it is not supported by the system
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        m_ui->checkShowSystray->setChecked(false);
        m_ui->checkShowSystray->setEnabled(false);
        m_ui->label_trayIconStyle->setVisible(false);
        m_ui->comboTrayIcon->setVisible(false);
    }

#if defined(QT_NO_OPENSSL)
    m_ui->checkWebUiHttps->setVisible(false);
#endif

#ifndef Q_OS_WIN
    m_ui->checkStartup->setVisible(false);
#endif

#if !(defined(Q_OS_WIN) || defined(Q_OS_MAC))
    m_ui->groupFileAssociation->setVisible(false);
#endif

    // Connect signals / slots
    connect(m_ui->comboProxyType, SIGNAL(currentIndexChanged(int)), this, SLOT(enableProxy(int)));
    connect(m_ui->checkRandomPort, SIGNAL(toggled(bool)), m_ui->spinPort, SLOT(setDisabled(bool)));

    // Apply button is activated when a value is changed
    // General tab
    connect(m_ui->comboI18n, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->confirmDeletion, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkAltRowColors, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkHideZero, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkHideZero, SIGNAL(toggled(bool)), m_ui->comboHideZero, SLOT(setEnabled(bool)));
    connect(m_ui->comboHideZero, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkShowSystray, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkCloseToSystray, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkMinimizeToSysTray, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkStartMinimized, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
#ifdef Q_OS_WIN
    connect(m_ui->checkStartup, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
#endif
    connect(m_ui->checkShowSplash, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkProgramExitConfirm, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkProgramAutoExitConfirm, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkPreventFromSuspend, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboTrayIcon, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
#if (defined(Q_OS_UNIX) && !defined(Q_OS_MAC)) && !defined(QT_DBUS_LIB)
    m_ui->checkPreventFromSuspend->setDisabled(true);
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    connect(m_ui->checkAssociateTorrents, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkAssociateMagnetLinks, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
#endif
    connect(m_ui->checkFileLog, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->textFileLogPath, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkFileLogBackup, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkFileLogBackup, SIGNAL(toggled(bool)), m_ui->spinFileLogSize, SLOT(setEnabled(bool)));
    connect(m_ui->checkFileLogDelete, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkFileLogDelete, SIGNAL(toggled(bool)), m_ui->spinFileLogAge, SLOT(setEnabled(bool)));
    connect(m_ui->checkFileLogDelete, SIGNAL(toggled(bool)), m_ui->comboFileLogAgeType, SLOT(setEnabled(bool)));
    connect(m_ui->spinFileLogSize, SIGNAL(valueChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinFileLogAge, SIGNAL(valueChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboFileLogAgeType, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    // Downloads tab
    connect(m_ui->textSavePath, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkUseSubcategories, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboSavingMode, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboTorrentCategoryChanged, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboCategoryDefaultPathChanged, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboCategoryChanged, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->textTempPath, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkAppendqB, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkPreallocateAll, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkAdditionDialog, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkAdditionDialogFront, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkStartPaused, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->deleteTorrentBox, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->deleteCancelledTorrentBox, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkExportDir, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkExportDir, SIGNAL(toggled(bool)), m_ui->textExportDir, SLOT(setEnabled(bool)));
    connect(m_ui->checkExportDir, SIGNAL(toggled(bool)), m_ui->browseExportDirButton, SLOT(setEnabled(bool)));
    connect(m_ui->checkExportDirFin, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkExportDirFin, SIGNAL(toggled(bool)), m_ui->textExportDirFin, SLOT(setEnabled(bool)));
    connect(m_ui->checkExportDirFin, SIGNAL(toggled(bool)), m_ui->browseExportDirFinButton, SLOT(setEnabled(bool)));
    connect(m_ui->textExportDir, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->textExportDirFin, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->actionTorrentDlOnDblClBox, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->actionTorrentFnOnDblClBox, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkTempFolder, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkTempFolder, SIGNAL(toggled(bool)), m_ui->textTempPath, SLOT(setEnabled(bool)));
    connect(m_ui->checkTempFolder, SIGNAL(toggled(bool)), m_ui->browseTempDirButton, SLOT(setEnabled(bool)));
    connect(m_ui->addScanFolderButton, SIGNAL(clicked()), this, SLOT(enableApplyButton()));
    connect(m_ui->removeScanFolderButton, SIGNAL(clicked()), this, SLOT(enableApplyButton()));
    connect(m_ui->groupMailNotification, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->dest_email_txt, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->smtp_server_txt, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkSmtpSSL, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->groupMailNotifAuth, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->mailNotifUsername, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->mailNotifPassword, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->autoRunBox, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->autoRun_txt, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));

    const QString autoRunStr = QString::fromUtf8("%1\n    %2\n    %3\n    %4\n    %5\n    %6\n    %7\n    %8\n    %9\n    %10\n%11")
                               .arg(tr("Supported parameters (case sensitive):"))
                               .arg(tr("%N: Torrent name"))
                               .arg(tr("%L: Category"))
                               .arg(tr("%F: Content path (same as root path for multifile torrent)"))
                               .arg(tr("%R: Root path (first torrent subdirectory path)"))
                               .arg(tr("%D: Save path"))
                               .arg(tr("%C: Number of files"))
                               .arg(tr("%Z: Torrent size (bytes)"))
                               .arg(tr("%T: Current tracker"))
                               .arg(tr("%I: Info hash"))
                               .arg(tr("Tip: Encapsulate parameter with quotation marks to avoid text being cut off at whitespace (e.g., \"%N\")"));
    m_ui->autoRun_param->setText(autoRunStr);

    // Connection tab
    connect(m_ui->spinPort, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkRandomPort, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkUPnP, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkUploadLimit, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkDownloadLimit, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkUploadLimitAlt, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkDownloadLimitAlt, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinUploadLimit, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinDownloadLimit, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinUploadLimitAlt, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinDownloadLimitAlt, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->check_schedule, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->schedule_from, SIGNAL(timeChanged(QTime)), this, SLOT(enableApplyButton()));
    connect(m_ui->schedule_to, SIGNAL(timeChanged(QTime)), this, SLOT(enableApplyButton()));
    connect(m_ui->schedule_days, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkuTP, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->checkuTP, SIGNAL(toggled(bool)), m_ui->checkLimituTPConnections, SLOT(setEnabled(bool)));
    connect(m_ui->checkLimituTPConnections, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->checkLimitTransportOverhead, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->checkLimitLocalPeerRate, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    // Bittorrent tab
    connect(m_ui->checkMaxConnecs, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkMaxConnecsPerTorrent, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkMaxUploads, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkMaxUploadsPerTorrent, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxConnec, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxConnecPerTorrent, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxUploads, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxUploadsPerTorrent, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkDHT, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkAnonymousMode, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkPeX, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkLSD, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboEncryption, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkMaxRatio, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxRatio, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->comboRatioLimitAct, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    // Proxy tab
    connect(m_ui->comboProxyType, SIGNAL(currentIndexChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->textProxyIP, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinProxyPort, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkProxyPeerConnecs, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->checkForceProxy, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->isProxyOnlyForTorrents, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->checkProxyAuth, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->textProxyUsername, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->textProxyPassword, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    // Misc tab
    connect(m_ui->checkIPFilter, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkIpFilterTrackers, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->textFilterPath, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkEnableQueueing, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxActiveDownloads, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxActiveUploads, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinMaxActiveTorrents, SIGNAL(valueChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkIgnoreSlowTorrentsForQueueing, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkEnableAddTrackers, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->textTrackers, SIGNAL(textChanged()), this, SLOT(enableApplyButton()));
#ifndef DISABLE_WEBUI
    // Web UI tab
    connect(m_ui->checkWebUi, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->spinWebUiPort, SIGNAL(valueChanged(int)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkWebUIUPnP, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->checkWebUiHttps, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->btnWebUiKey, SIGNAL(clicked()), SLOT(enableApplyButton()));
    connect(m_ui->btnWebUiCrt, SIGNAL(clicked()), SLOT(enableApplyButton()));
    connect(m_ui->textWebUiUsername, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->textWebUiPassword, SIGNAL(textChanged(QString)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkBypassLocalAuth, SIGNAL(toggled(bool)), this, SLOT(enableApplyButton()));
    connect(m_ui->checkDynDNS, SIGNAL(toggled(bool)), SLOT(enableApplyButton()));
    connect(m_ui->comboDNSService, SIGNAL(currentIndexChanged(int)), SLOT(enableApplyButton()));
    connect(m_ui->domainNameTxt, SIGNAL(textChanged(QString)), SLOT(enableApplyButton()));
    connect(m_ui->DNSUsernameTxt, SIGNAL(textChanged(QString)), SLOT(enableApplyButton()));
    connect(m_ui->DNSPasswordTxt, SIGNAL(textChanged(QString)), SLOT(enableApplyButton()));
#endif
    // Disable apply Button
    applyButton->setEnabled(false);
    // Tab selection mechanism
    connect(m_ui->tabSelection, SIGNAL(currentItemChanged(QListWidgetItem *, QListWidgetItem *)), this, SLOT(changePage(QListWidgetItem *, QListWidgetItem*)));
    // Load Advanced settings
    advancedSettings = new AdvancedSettings(m_ui->tabAdvancedPage);
    m_ui->advPageLayout->addWidget(advancedSettings);
    connect(advancedSettings, SIGNAL(settingsChanged()), this, SLOT(enableApplyButton()));

    // Adapt size
    show();
    loadWindowState();
}

void OptionsDialog::initializeLanguageCombo()
{
    // List language files
    const QDir langDir(":/lang");
    const QStringList langFiles = langDir.entryList(QStringList("qbittorrent_*.qm"), QDir::Files);
    foreach (const QString langFile, langFiles) {
        QString localeStr = langFile.mid(12); // remove "qbittorrent_"
        localeStr.chop(3); // Remove ".qm"
        QString languageName;
        if (localeStr.startsWith("eo", Qt::CaseInsensitive)) {
            // QLocale doesn't work with that locale. Esperanto isn't a "real" language.
            languageName = QString::fromUtf8(C_LOCALE_ESPERANTO);
        }
        else {
            QLocale locale(localeStr);
            languageName = languageToLocalizedString(locale);
        }
        m_ui->comboI18n->addItem(/*QIcon(":/icons/flags/"+country+".png"), */ languageName, localeStr);
        qDebug() << "Supported locale:" << localeStr;
    }
}

// Main destructor
OptionsDialog::~OptionsDialog()
{
    qDebug("-> destructing Options");
    foreach (const QString &path, addedScanDirs)
        ScanFoldersModel::instance()->removePath(path);
    ScanFoldersModel::instance()->configure(); // reloads "removed" paths
    delete m_ui;
}

void OptionsDialog::changePage(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current)
        current = previous;
    m_ui->tabOption->setCurrentIndex(m_ui->tabSelection->row(current));
}

void OptionsDialog::loadWindowState()
{
    const Preferences* const pref = Preferences::instance();
    resize(pref->getPrefSize(sizeFittingScreen()));
    QPoint p = pref->getPrefPos();
    QRect scr_rect = qApp->desktop()->screenGeometry();
    if (!p.isNull() && scr_rect.contains(p))
        move(p);
    // Load slider size
    const QStringList sizes_str = pref->getPrefHSplitterSizes();
    // Splitter size
    QList<int> sizes;
    if (sizes_str.size() == 2) {
        sizes << sizes_str.first().toInt();
        sizes << sizes_str.last().toInt();
    }
    else {
        sizes << 116;
        sizes << m_ui->hsplitter->width() - 116;
    }
    m_ui->hsplitter->setSizes(sizes);
}

void OptionsDialog::saveWindowState() const
{
    Preferences* const pref = Preferences::instance();
    pref->setPrefSize(size());
    pref->setPrefPos(pos());
    // Splitter size
    QStringList sizes_str;
    sizes_str << QString::number(m_ui->hsplitter->sizes().first());
    sizes_str << QString::number(m_ui->hsplitter->sizes().last());
    pref->setPrefHSplitterSizes(sizes_str);
}

QSize OptionsDialog::sizeFittingScreen() const
{
    int scrn = 0;
    QWidget *w = this->topLevelWidget();

    if (w)
        scrn = QApplication::desktop()->screenNumber(w);
    else if (QApplication::desktop()->isVirtualDesktop())
        scrn = QApplication::desktop()->screenNumber(QCursor::pos());
    else
        scrn = QApplication::desktop()->screenNumber(this);

    QRect desk(QApplication::desktop()->availableGeometry(scrn));
    if (width() > desk.width() || height() > desk.height())
        if (desk.width() > 0 && desk.height() > 0)
            return QSize(desk.width(), desk.height());
    return size();
}

void OptionsDialog::saveOptions()
{
    applyButton->setEnabled(false);
    Preferences* const pref = Preferences::instance();
    // Load the translation
    QString locale = getLocale();
    if (pref->getLocale() != locale) {
        QTranslator *translator = new QTranslator;
        if (translator->load(QString::fromUtf8(":/lang/qbittorrent_") + locale))
            qDebug("%s locale recognized, using translation.", qPrintable(locale));
        else
            qDebug("%s locale unrecognized, using default (en).", qPrintable(locale));
        qApp->installTranslator(translator);
    }

    // General preferences
    pref->setLocale(locale);
    pref->setConfirmTorrentDeletion(m_ui->confirmDeletion->isChecked());
    pref->setAlternatingRowColors(m_ui->checkAltRowColors->isChecked());
    pref->setHideZeroValues(m_ui->checkHideZero->isChecked());
    pref->setHideZeroComboValues(m_ui->comboHideZero->currentIndex());
    pref->setSystrayIntegration(systrayIntegration());
    pref->setTrayIconStyle(TrayIcon::Style(m_ui->comboTrayIcon->currentIndex()));
    pref->setCloseToTray(closeToTray());
    pref->setMinimizeToTray(minimizeToTray());
    pref->setStartMinimized(startMinimized());
    pref->setSplashScreenDisabled(isSlashScreenDisabled());
    pref->setConfirmOnExit(m_ui->checkProgramExitConfirm->isChecked());
    pref->setDontConfirmAutoExit(!m_ui->checkProgramAutoExitConfirm->isChecked());
    pref->setPreventFromSuspend(preventFromSuspend());
#ifdef Q_OS_WIN
    pref->setWinStartup(WinStartup());
    // Windows: file association settings
    Preferences::setTorrentFileAssoc(m_ui->checkAssociateTorrents->isChecked());
    Preferences::setMagnetLinkAssoc(m_ui->checkAssociateMagnetLinks->isChecked());
#endif
#ifdef Q_OS_MAC
    if (m_ui->checkAssociateTorrents->isChecked()) {
        Preferences::setTorrentFileAssoc();
        m_ui->checkAssociateTorrents->setChecked(Preferences::isTorrentFileAssocSet());
        m_ui->checkAssociateTorrents->setEnabled(!m_ui->checkAssociateTorrents->isChecked());
    }
    if (m_ui->checkAssociateMagnetLinks->isChecked()) {
        Preferences::setMagnetLinkAssoc();
        m_ui->checkAssociateMagnetLinks->setChecked(Preferences::isMagnetLinkAssocSet());
        m_ui->checkAssociateMagnetLinks->setEnabled(!m_ui->checkAssociateMagnetLinks->isChecked());
    }
#endif
    Application * const app = static_cast<Application*>(QCoreApplication::instance());
    app->setFileLoggerPath(Utils::Fs::fromNativePath(m_ui->textFileLogPath->text()));
    app->setFileLoggerBackup(m_ui->checkFileLogBackup->isChecked());
    app->setFileLoggerMaxSize(m_ui->spinFileLogSize->value());
    app->setFileLoggerAge(m_ui->spinFileLogAge->value());
    app->setFileLoggerAgeType(m_ui->comboFileLogAgeType->currentIndex());
    app->setFileLoggerDeleteOld(m_ui->checkFileLogDelete->isChecked());
    app->setFileLoggerEnabled(m_ui->checkFileLog->isChecked());
    // End General preferences

    auto session = BitTorrent::Session::instance();

    // Downloads preferences
    session->setDefaultSavePath(Utils::Fs::expandPathAbs(m_ui->textSavePath->text()));
    session->setSubcategoriesEnabled(m_ui->checkUseSubcategories->isChecked());
    session->setAutoTMMDisabledByDefault(m_ui->comboSavingMode->currentIndex() == 0);
    session->setDisableAutoTMMWhenCategoryChanged(m_ui->comboTorrentCategoryChanged->currentIndex() == 1);
    session->setDisableAutoTMMWhenCategorySavePathChanged(m_ui->comboCategoryChanged->currentIndex() == 1);
    session->setDisableAutoTMMWhenDefaultSavePathChanged(m_ui->comboCategoryDefaultPathChanged->currentIndex() == 1);
    session->setTempPathEnabled(m_ui->checkTempFolder->isChecked());
    session->setTempPath(Utils::Fs::expandPathAbs(m_ui->textTempPath->text()));
    pref->useIncompleteFilesExtension(m_ui->checkAppendqB->isChecked());
    pref->preAllocateAllFiles(preAllocateAllFiles());
    AddNewTorrentDialog::setEnabled(useAdditionDialog());
    AddNewTorrentDialog::setTopLevel(m_ui->checkAdditionDialogFront->isChecked());
    session->setAddTorrentPaused(addTorrentsInPause());
    ScanFoldersModel::instance()->removeFromFSWatcher(removedScanDirs);
    ScanFoldersModel::instance()->addToFSWatcher(addedScanDirs);
    ScanFoldersModel::instance()->makePersistent();
    removedScanDirs.clear();
    addedScanDirs.clear();
    pref->setTorrentExportDir(getTorrentExportDir());
    pref->setFinishedTorrentExportDir(getFinishedTorrentExportDir());
    pref->setMailNotificationEnabled(m_ui->groupMailNotification->isChecked());
    pref->setMailNotificationEmail(m_ui->dest_email_txt->text());
    pref->setMailNotificationSMTP(m_ui->smtp_server_txt->text());
    pref->setMailNotificationSMTPSSL(m_ui->checkSmtpSSL->isChecked());
    pref->setMailNotificationSMTPAuth(m_ui->groupMailNotifAuth->isChecked());
    pref->setMailNotificationSMTPUsername(m_ui->mailNotifUsername->text());
    pref->setMailNotificationSMTPPassword(m_ui->mailNotifPassword->text());
    pref->setAutoRunEnabled(m_ui->autoRunBox->isChecked());
    pref->setAutoRunProgram(m_ui->autoRun_txt->text().trimmed());
    pref->setActionOnDblClOnTorrentDl(getActionOnDblClOnTorrentDl());
    pref->setActionOnDblClOnTorrentFn(getActionOnDblClOnTorrentFn());
    TorrentFileGuard::setAutoDeleteMode(!m_ui->deleteTorrentBox->isChecked() ? TorrentFileGuard::Never
                             : !m_ui->deleteCancelledTorrentBox->isChecked() ? TorrentFileGuard::IfAdded
                             : TorrentFileGuard::Always);
    // End Downloads preferences

    // Connection preferences
    pref->setSessionPort(getPort());
    pref->setRandomPort(m_ui->checkRandomPort->isChecked());
    pref->setUPnPEnabled(isUPnPEnabled());
    const QPair<int, int> down_up_limit = getGlobalBandwidthLimits();
    pref->setGlobalDownloadLimit(down_up_limit.first);
    pref->setGlobalUploadLimit(down_up_limit.second);
    pref->setuTPEnabled(m_ui->checkuTP->isChecked());
    pref->setuTPRateLimited(m_ui->checkLimituTPConnections->isChecked());
    pref->includeOverheadInLimits(m_ui->checkLimitTransportOverhead->isChecked());
    pref->setIgnoreLimitsOnLAN(!m_ui->checkLimitLocalPeerRate->isChecked());
    const QPair<int, int> alt_down_up_limit = getAltGlobalBandwidthLimits();
    pref->setAltGlobalDownloadLimit(alt_down_up_limit.first);
    pref->setAltGlobalUploadLimit(alt_down_up_limit.second);
    pref->setSchedulerEnabled(m_ui->check_schedule->isChecked());
    pref->setSchedulerStartTime(m_ui->schedule_from->time());
    pref->setSchedulerEndTime(m_ui->schedule_to->time());
    pref->setSchedulerDays((scheduler_days)m_ui->schedule_days->currentIndex());
    pref->setProxyType(getProxyType());
    pref->setProxyIp(getProxyIp());
    pref->setProxyPort(getProxyPort());
    pref->setProxyPeerConnections(m_ui->checkProxyPeerConnecs->isChecked());
    pref->setForceProxy(m_ui->checkForceProxy->isChecked());
    pref->setProxyOnlyForTorrents(m_ui->isProxyOnlyForTorrents->isChecked());
    pref->setProxyAuthEnabled(isProxyAuthEnabled());
    pref->setProxyUsername(getProxyUsername());
    pref->setProxyPassword(getProxyPassword());
    // End Connection preferences
    // Bittorrent preferences
    pref->setMaxConnecs(getMaxConnecs());
    pref->setMaxConnecsPerTorrent(getMaxConnecsPerTorrent());
    pref->setMaxUploads(getMaxUploads());
    pref->setMaxUploadsPerTorrent(getMaxUploadsPerTorrent());
    pref->setDHTEnabled(isDHTEnabled());
    pref->setPeXEnabled(m_ui->checkPeX->isChecked());
    pref->setLSDEnabled(isLSDEnabled());
    pref->setEncryptionSetting(getEncryptionSetting());
    pref->enableAnonymousMode(m_ui->checkAnonymousMode->isChecked());
    pref->setAddTrackersEnabled(m_ui->checkEnableAddTrackers->isChecked());
    pref->setTrackersList(m_ui->textTrackers->toPlainText());
    pref->setGlobalMaxRatio(getMaxRatio());
    session->setMaxRatioAction(static_cast<MaxRatioAction>(m_ui->comboRatioLimitAct->currentIndex()));
    // End Bittorrent preferences

    // Misc preferences
    // * IPFilter
    pref->setFilteringEnabled(isFilteringEnabled());
    pref->setFilteringTrackerEnabled(m_ui->checkIpFilterTrackers->isChecked());
    pref->setFilter(m_ui->textFilterPath->text());
    // End IPFilter preferences
    // Queueing system
    pref->setQueueingSystemEnabled(isQueueingSystemEnabled());
    pref->setMaxActiveDownloads(m_ui->spinMaxActiveDownloads->value());
    pref->setMaxActiveUploads(m_ui->spinMaxActiveUploads->value());
    pref->setMaxActiveTorrents(m_ui->spinMaxActiveTorrents->value());
    pref->setIgnoreSlowTorrentsForQueueing(m_ui->checkIgnoreSlowTorrentsForQueueing->isChecked());
    // End Queueing system preferences
    // Web UI
    pref->setWebUiEnabled(isWebUiEnabled());
    if (isWebUiEnabled()) {
        pref->setWebUiPort(webUiPort());
        pref->setUPnPForWebUIPort(m_ui->checkWebUIUPnP->isChecked());
        pref->setWebUiHttpsEnabled(m_ui->checkWebUiHttps->isChecked());
        if (m_ui->checkWebUiHttps->isChecked()) {
            pref->setWebUiHttpsCertificate(m_sslCert);
            pref->setWebUiHttpsKey(m_sslKey);
        }
        pref->setWebUiUsername(webUiUsername());
        pref->setWebUiPassword(webUiPassword());
        pref->setWebUiLocalAuthEnabled(!m_ui->checkBypassLocalAuth->isChecked());
        // DynDNS
        pref->setDynDNSEnabled(m_ui->checkDynDNS->isChecked());
        pref->setDynDNSService(m_ui->comboDNSService->currentIndex());
        pref->setDynDomainName(m_ui->domainNameTxt->text());
        pref->setDynDNSUsername(m_ui->DNSUsernameTxt->text());
        pref->setDynDNSPassword(m_ui->DNSPasswordTxt->text());
    }
    // End Web UI
    // End preferences
    // Save advanced settings
    advancedSettings->saveAdvancedSettings();
    // Assume that user changed multiple settings
    // so it's best to save immediately
    pref->apply();
}

bool OptionsDialog::isFilteringEnabled() const
{
    return m_ui->checkIPFilter->isChecked();
}

int OptionsDialog::getProxyType() const
{
    switch (m_ui->comboProxyType->currentIndex()) {
    case 1:
        return Proxy::SOCKS4;
        break;
    case 2:
        if (isProxyAuthEnabled())
            return Proxy::SOCKS5_PW;
        return Proxy::SOCKS5;
    case 3:
        if (isProxyAuthEnabled())
            return Proxy::HTTP_PW;
        return Proxy::HTTP;
    default:
        return -1;
    }
}

void OptionsDialog::loadOptions()
{
    int intValue;
    qreal floatValue;
    QString strValue;
    bool fileLogBackup = true;
    bool fileLogDelete = true;
    const Preferences* const pref = Preferences::instance();

    // General preferences
    setLocale(pref->getLocale());
    m_ui->confirmDeletion->setChecked(pref->confirmTorrentDeletion());
    m_ui->checkAltRowColors->setChecked(pref->useAlternatingRowColors());
    m_ui->checkHideZero->setChecked(pref->getHideZeroValues());
    m_ui->comboHideZero->setEnabled(m_ui->checkHideZero->isChecked());
    m_ui->comboHideZero->setCurrentIndex(pref->getHideZeroComboValues());

    m_ui->checkShowSplash->setChecked(!pref->isSplashScreenDisabled());
    m_ui->checkStartMinimized->setChecked(pref->startMinimized());
    m_ui->checkProgramExitConfirm->setChecked(pref->confirmOnExit());
    m_ui->checkProgramAutoExitConfirm->setChecked(!pref->dontConfirmAutoExit());

    m_ui->checkShowSystray->setChecked(pref->systrayIntegration());
    if (m_ui->checkShowSystray->isChecked()) {
        m_ui->checkMinimizeToSysTray->setChecked(pref->minimizeToTray());
        m_ui->checkCloseToSystray->setChecked(pref->closeToTray());
        m_ui->comboTrayIcon->setCurrentIndex(pref->trayIconStyle());
    }

    m_ui->checkPreventFromSuspend->setChecked(pref->preventFromSuspend());

#ifdef Q_OS_WIN
    m_ui->checkStartup->setChecked(pref->WinStartup());
    m_ui->checkAssociateTorrents->setChecked(Preferences::isTorrentFileAssocSet());
    m_ui->checkAssociateMagnetLinks->setChecked(Preferences::isMagnetLinkAssocSet());
#endif
#ifdef Q_OS_MAC
    m_ui->checkAssociateTorrents->setChecked(Preferences::isTorrentFileAssocSet());
    m_ui->checkAssociateTorrents->setEnabled(!m_ui->checkAssociateTorrents->isChecked());
    m_ui->checkAssociateMagnetLinks->setChecked(Preferences::isMagnetLinkAssocSet());
    m_ui->checkAssociateMagnetLinks->setEnabled(!m_ui->checkAssociateMagnetLinks->isChecked());
#endif

    const Application * const app = static_cast<Application*>(QCoreApplication::instance());
    m_ui->checkFileLog->setChecked(app->isFileLoggerEnabled());
    m_ui->textFileLogPath->setText(Utils::Fs::toNativePath(app->fileLoggerPath()));
    fileLogBackup = app->isFileLoggerBackup();
    m_ui->checkFileLogBackup->setChecked(fileLogBackup);
    m_ui->spinFileLogSize->setEnabled(fileLogBackup);
    fileLogDelete = app->isFileLoggerDeleteOld();
    m_ui->checkFileLogDelete->setChecked(fileLogDelete);
    m_ui->spinFileLogAge->setEnabled(fileLogDelete);
    m_ui->comboFileLogAgeType->setEnabled(fileLogDelete);
    m_ui->spinFileLogSize->setValue(app->fileLoggerMaxSize());
    m_ui->spinFileLogAge->setValue(app->fileLoggerAge());
    m_ui->comboFileLogAgeType->setCurrentIndex(app->fileLoggerAgeType());
    // End General preferences

    auto session = BitTorrent::Session::instance();

    // Downloads preferences
    m_ui->checkAdditionDialog->setChecked(AddNewTorrentDialog::isEnabled());
    m_ui->checkAdditionDialogFront->setChecked(AddNewTorrentDialog::isTopLevel());
    m_ui->checkStartPaused->setChecked(session->isAddTorrentPaused());
    const TorrentFileGuard::AutoDeleteMode autoDeleteMode = TorrentFileGuard::autoDeleteMode();
    m_ui->deleteTorrentBox->setChecked(autoDeleteMode != TorrentFileGuard::Never);
    m_ui->deleteCancelledTorrentBox->setChecked(autoDeleteMode == TorrentFileGuard::Always);

    m_ui->textSavePath->setText(Utils::Fs::toNativePath(session->defaultSavePath()));
    m_ui->checkUseSubcategories->setChecked(session->isSubcategoriesEnabled());
    m_ui->comboSavingMode->setCurrentIndex(!session->isAutoTMMDisabledByDefault());
    m_ui->comboTorrentCategoryChanged->setCurrentIndex(session->isDisableAutoTMMWhenCategoryChanged());
    m_ui->comboCategoryChanged->setCurrentIndex(session->isDisableAutoTMMWhenCategorySavePathChanged());
    m_ui->comboCategoryDefaultPathChanged->setCurrentIndex(session->isDisableAutoTMMWhenDefaultSavePathChanged());
    m_ui->checkTempFolder->setChecked(session->isTempPathEnabled());
    m_ui->textTempPath->setEnabled(m_ui->checkTempFolder->isChecked());
    m_ui->browseTempDirButton->setEnabled(m_ui->checkTempFolder->isChecked());
    m_ui->textTempPath->setText(Utils::Fs::toNativePath(session->tempPath()));
    m_ui->checkAppendqB->setChecked(pref->useIncompleteFilesExtension());
    m_ui->checkPreallocateAll->setChecked(pref->preAllocateAllFiles());

    strValue = Utils::Fs::toNativePath(pref->getTorrentExportDir());
    if (strValue.isEmpty()) {
        // Disable
        m_ui->checkExportDir->setChecked(false);
        m_ui->textExportDir->setEnabled(false);
        m_ui->browseExportDirButton->setEnabled(false);
    }
    else {
        // Enable
        m_ui->checkExportDir->setChecked(true);
        m_ui->textExportDir->setEnabled(true);
        m_ui->browseExportDirButton->setEnabled(true);
        m_ui->textExportDir->setText(strValue);
    }

    strValue = Utils::Fs::toNativePath(pref->getFinishedTorrentExportDir());
    if (strValue.isEmpty()) {
        // Disable
        m_ui->checkExportDirFin->setChecked(false);
        m_ui->textExportDirFin->setEnabled(false);
        m_ui->browseExportDirFinButton->setEnabled(false);
    }
    else {
        // Enable
        m_ui->checkExportDirFin->setChecked(true);
        m_ui->textExportDirFin->setEnabled(true);
        m_ui->browseExportDirFinButton->setEnabled(true);
        m_ui->textExportDirFin->setText(strValue);
    }

    m_ui->groupMailNotification->setChecked(pref->isMailNotificationEnabled());
    m_ui->dest_email_txt->setText(pref->getMailNotificationEmail());
    m_ui->smtp_server_txt->setText(pref->getMailNotificationSMTP());
    m_ui->checkSmtpSSL->setChecked(pref->getMailNotificationSMTPSSL());
    m_ui->groupMailNotifAuth->setChecked(pref->getMailNotificationSMTPAuth());
    m_ui->mailNotifUsername->setText(pref->getMailNotificationSMTPUsername());
    m_ui->mailNotifPassword->setText(pref->getMailNotificationSMTPPassword());

    m_ui->autoRunBox->setChecked(pref->isAutoRunEnabled());
    m_ui->autoRun_txt->setText(pref->getAutoRunProgram());
    intValue = pref->getActionOnDblClOnTorrentDl();
    if (intValue >= m_ui->actionTorrentDlOnDblClBox->count())
        intValue = 0;
    m_ui->actionTorrentDlOnDblClBox->setCurrentIndex(intValue);
    intValue = pref->getActionOnDblClOnTorrentFn();
    if (intValue >= m_ui->actionTorrentFnOnDblClBox->count())
        intValue = 1;
    m_ui->actionTorrentFnOnDblClBox->setCurrentIndex(intValue);
    // End Downloads preferences

    // Connection preferences
    m_ui->checkUPnP->setChecked(pref->isUPnPEnabled());
    m_ui->checkRandomPort->setChecked(pref->useRandomPort());
    m_ui->spinPort->setValue(pref->getSessionPort());
    m_ui->spinPort->setDisabled(m_ui->checkRandomPort->isChecked());

    intValue = pref->getMaxConnecs();
    if (intValue > 0) {
        // enable
        m_ui->checkMaxConnecs->setChecked(true);
        m_ui->spinMaxConnec->setEnabled(true);
        m_ui->spinMaxConnec->setValue(intValue);
    }
    else {
        // disable
        m_ui->checkMaxConnecs->setChecked(false);
        m_ui->spinMaxConnec->setEnabled(false);
    }
    intValue = pref->getMaxConnecsPerTorrent();
    if (intValue > 0) {
        // enable
        m_ui->checkMaxConnecsPerTorrent->setChecked(true);
        m_ui->spinMaxConnecPerTorrent->setEnabled(true);
        m_ui->spinMaxConnecPerTorrent->setValue(intValue);
    }
    else {
        // disable
        m_ui->checkMaxConnecsPerTorrent->setChecked(false);
        m_ui->spinMaxConnecPerTorrent->setEnabled(false);
    }
    intValue = pref->getMaxUploads();
    if (intValue > 0) {
        // enable
        m_ui->checkMaxUploads->setChecked(true);
        m_ui->spinMaxUploads->setEnabled(true);
        m_ui->spinMaxUploads->setValue(intValue);
    }
    else {
        // disable
        m_ui->checkMaxUploads->setChecked(false);
        m_ui->spinMaxUploads->setEnabled(false);
    }
    intValue = pref->getMaxUploadsPerTorrent();
    if (intValue > 0) {
        // enable
        m_ui->checkMaxUploadsPerTorrent->setChecked(true);
        m_ui->spinMaxUploadsPerTorrent->setEnabled(true);
        m_ui->spinMaxUploadsPerTorrent->setValue(intValue);
    }
    else {
        // disable
        m_ui->checkMaxUploadsPerTorrent->setChecked(false);
        m_ui->spinMaxUploadsPerTorrent->setEnabled(false);
    }

    intValue = pref->getProxyType();
    switch (intValue) {
    case Proxy::SOCKS4:
        m_ui->comboProxyType->setCurrentIndex(1);
        break;
    case Proxy::SOCKS5:
    case Proxy::SOCKS5_PW:
        m_ui->comboProxyType->setCurrentIndex(2);
        break;
    case Proxy::HTTP:
    case Proxy::HTTP_PW:
        m_ui->comboProxyType->setCurrentIndex(3);
        break;
    default:
        m_ui->comboProxyType->setCurrentIndex(0);
    }
    enableProxy(m_ui->comboProxyType->currentIndex());
    m_ui->textProxyIP->setText(pref->getProxyIp());
    m_ui->spinProxyPort->setValue(pref->getProxyPort());
    m_ui->checkProxyPeerConnecs->setChecked(pref->proxyPeerConnections());
    m_ui->checkForceProxy->setChecked(pref->getForceProxy());
    m_ui->isProxyOnlyForTorrents->setChecked(pref->isProxyOnlyForTorrents());
    m_ui->checkProxyAuth->setChecked(pref->isProxyAuthEnabled());
    m_ui->textProxyUsername->setText(pref->getProxyUsername());
    m_ui->textProxyPassword->setText(pref->getProxyPassword());

    m_ui->checkIPFilter->setChecked(pref->isFilteringEnabled());
    m_ui->checkIpFilterTrackers->setChecked(pref->isFilteringTrackerEnabled());
    m_ui->textFilterPath->setText(Utils::Fs::toNativePath(pref->getFilter()));
    // End Connection preferences

    // Speed preferences
    intValue = pref->getGlobalDownloadLimit();
    if (intValue > 0) {
        // Enabled
        m_ui->checkDownloadLimit->setChecked(true);
        m_ui->spinDownloadLimit->setEnabled(true);
        m_ui->spinDownloadLimit->setValue(intValue);
    }
    else {
        // Disabled
        m_ui->checkDownloadLimit->setChecked(false);
        m_ui->spinDownloadLimit->setEnabled(false);
    }
    intValue = pref->getGlobalUploadLimit();
    if (intValue != -1) {
        // Enabled
        m_ui->checkUploadLimit->setChecked(true);
        m_ui->spinUploadLimit->setEnabled(true);
        m_ui->spinUploadLimit->setValue(intValue);
    }
    else {
        // Disabled
        m_ui->checkUploadLimit->setChecked(false);
        m_ui->spinUploadLimit->setEnabled(false);
    }

    intValue = pref->getAltGlobalDownloadLimit();
    if (intValue > 0) {
        // Enabled
        m_ui->checkDownloadLimitAlt->setChecked(true);
        m_ui->spinDownloadLimitAlt->setEnabled(true);
        m_ui->spinDownloadLimitAlt->setValue(intValue);
    }
    else {
        // Disabled
        m_ui->checkDownloadLimitAlt->setChecked(false);
        m_ui->spinDownloadLimitAlt->setEnabled(false);
    }
    intValue = pref->getAltGlobalUploadLimit();
    if (intValue != -1) {
        // Enabled
        m_ui->checkUploadLimitAlt->setChecked(true);
        m_ui->spinUploadLimitAlt->setEnabled(true);
        m_ui->spinUploadLimitAlt->setValue(intValue);
    }
    else {
        // Disabled
        m_ui->checkUploadLimitAlt->setChecked(false);
        m_ui->spinUploadLimitAlt->setEnabled(false);
    }

    m_ui->checkuTP->setChecked(pref->isuTPEnabled());
    m_ui->checkLimituTPConnections->setEnabled(m_ui->checkuTP->isChecked());
    m_ui->checkLimituTPConnections->setChecked(pref->isuTPRateLimited());
    m_ui->checkLimitTransportOverhead->setChecked(pref->includeOverheadInLimits());
    m_ui->checkLimitLocalPeerRate->setChecked(!pref->getIgnoreLimitsOnLAN());

    m_ui->check_schedule->setChecked(pref->isSchedulerEnabled());
    m_ui->schedule_from->setTime(pref->getSchedulerStartTime());
    m_ui->schedule_to->setTime(pref->getSchedulerEndTime());
    m_ui->schedule_days->setCurrentIndex((int)pref->getSchedulerDays());
    // End Speed preferences

    // Bittorrent preferences
    m_ui->checkDHT->setChecked(pref->isDHTEnabled());
    m_ui->checkPeX->setChecked(pref->isPeXEnabled());
    m_ui->checkLSD->setChecked(pref->isLSDEnabled());
    m_ui->comboEncryption->setCurrentIndex(pref->getEncryptionSetting());
    m_ui->checkAnonymousMode->setChecked(pref->isAnonymousModeEnabled());
    m_ui->checkEnableAddTrackers->setChecked(pref->isAddTrackersEnabled());
    m_ui->textTrackers->setPlainText(pref->getTrackersList());

    m_ui->checkEnableQueueing->setChecked(pref->isQueueingSystemEnabled());
    m_ui->spinMaxActiveDownloads->setValue(pref->getMaxActiveDownloads());
    m_ui->spinMaxActiveUploads->setValue(pref->getMaxActiveUploads());
    m_ui->spinMaxActiveTorrents->setValue(pref->getMaxActiveTorrents());
    m_ui->checkIgnoreSlowTorrentsForQueueing->setChecked(pref->ignoreSlowTorrentsForQueueing());

    floatValue = pref->getGlobalMaxRatio();
    if (floatValue >= 0.) {
        // Enable
        m_ui->checkMaxRatio->setChecked(true);
        m_ui->spinMaxRatio->setEnabled(true);
        m_ui->comboRatioLimitAct->setEnabled(true);
        m_ui->spinMaxRatio->setValue(floatValue);
    }
    else {
        // Disable
        m_ui->checkMaxRatio->setChecked(false);
        m_ui->spinMaxRatio->setEnabled(false);
        m_ui->comboRatioLimitAct->setEnabled(false);
    }
    m_ui->comboRatioLimitAct->setCurrentIndex(session->maxRatioAction());
    // End Bittorrent preferences

    // Web UI preferences
    m_ui->checkWebUi->setChecked(pref->isWebUiEnabled());
    m_ui->spinWebUiPort->setValue(pref->getWebUiPort());
    m_ui->checkWebUIUPnP->setChecked(pref->useUPnPForWebUIPort());
    m_ui->checkWebUiHttps->setChecked(pref->isWebUiHttpsEnabled());
    setSslCertificate(pref->getWebUiHttpsCertificate(), false);
    setSslKey(pref->getWebUiHttpsKey(), false);
    m_ui->textWebUiUsername->setText(pref->getWebUiUsername());
    m_ui->textWebUiPassword->setText(pref->getWebUiPassword());
    m_ui->checkBypassLocalAuth->setChecked(!pref->isWebUiLocalAuthEnabled());

    m_ui->checkDynDNS->setChecked(pref->isDynDNSEnabled());
    m_ui->comboDNSService->setCurrentIndex((int)pref->getDynDNSService());
    m_ui->domainNameTxt->setText(pref->getDynDomainName());
    m_ui->DNSUsernameTxt->setText(pref->getDynDNSUsername());
    m_ui->DNSPasswordTxt->setText(pref->getDynDNSPassword());
    // End Web UI preferences
}

// return min & max ports
// [min, max]
int OptionsDialog::getPort() const
{
    return m_ui->spinPort->value();
}

void OptionsDialog::on_randomButton_clicked()
{
    // Range [1024: 65535]
    m_ui->spinPort->setValue(rand() % 64512 + 1024);
}

int OptionsDialog::getEncryptionSetting() const
{
    return m_ui->comboEncryption->currentIndex();
}

int OptionsDialog::getMaxActiveDownloads() const
{
    return m_ui->spinMaxActiveDownloads->value();
}

int OptionsDialog::getMaxActiveUploads() const
{
    return m_ui->spinMaxActiveUploads->value();
}

int OptionsDialog::getMaxActiveTorrents() const
{
    return m_ui->spinMaxActiveTorrents->value();
}

bool OptionsDialog::minimizeToTray() const
{
    if (!m_ui->checkShowSystray->isChecked()) return false;
    return m_ui->checkMinimizeToSysTray->isChecked();
}

bool OptionsDialog::closeToTray() const
{
    if (!m_ui->checkShowSystray->isChecked()) return false;
    return m_ui->checkCloseToSystray->isChecked();
}

bool OptionsDialog::isQueueingSystemEnabled() const
{
    return m_ui->checkEnableQueueing->isChecked();
}

bool OptionsDialog::isDHTEnabled() const
{
    return m_ui->checkDHT->isChecked();
}

bool OptionsDialog::isLSDEnabled() const
{
    return m_ui->checkLSD->isChecked();
}

bool OptionsDialog::isUPnPEnabled() const
{
    return m_ui->checkUPnP->isChecked();
}

// Return Download & Upload limits in kbps
// [download,upload]
QPair<int, int> OptionsDialog::getGlobalBandwidthLimits() const
{
    int DL = -1, UP = -1;
    if (m_ui->checkDownloadLimit->isChecked())
        DL = m_ui->spinDownloadLimit->value();
    if (m_ui->checkUploadLimit->isChecked())
        UP = m_ui->spinUploadLimit->value();
    return qMakePair(DL, UP);
}

// Return alternate Download & Upload limits in kbps
// [download,upload]
QPair<int, int> OptionsDialog::getAltGlobalBandwidthLimits() const
{
    int DL = -1, UP = -1;
    if (m_ui->checkDownloadLimitAlt->isChecked())
        DL = m_ui->spinDownloadLimitAlt->value();
    if (m_ui->checkUploadLimitAlt->isChecked())
        UP = m_ui->spinUploadLimitAlt->value();
    return qMakePair(DL, UP);
}

bool OptionsDialog::startMinimized() const
{
    return m_ui->checkStartMinimized->isChecked();
}

bool OptionsDialog::systrayIntegration() const
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return false;
    return m_ui->checkShowSystray->isChecked();
}

// Return Share ratio
qreal OptionsDialog::getMaxRatio() const
{
    if (m_ui->checkMaxRatio->isChecked())
        return m_ui->spinMaxRatio->value();
    return -1;
}

// Return max connections number
int OptionsDialog::getMaxConnecs() const
{
    if (!m_ui->checkMaxConnecs->isChecked())
        return -1;
    else
        return m_ui->spinMaxConnec->value();
}

int OptionsDialog::getMaxConnecsPerTorrent() const
{
    if (!m_ui->checkMaxConnecsPerTorrent->isChecked())
        return -1;
    else
        return m_ui->spinMaxConnecPerTorrent->value();
}

int OptionsDialog::getMaxUploads() const
{
    if (!m_ui->checkMaxUploads->isChecked())
        return -1;
    else
        return m_ui->spinMaxUploads->value();
}

int OptionsDialog::getMaxUploadsPerTorrent() const
{
    if (!m_ui->checkMaxUploadsPerTorrent->isChecked())
        return -1;
    else
        return m_ui->spinMaxUploadsPerTorrent->value();
}

void OptionsDialog::on_buttonBox_accepted()
{
    if (applyButton->isEnabled()) {
        if (!schedTimesOk()) {
            m_ui->tabSelection->setCurrentRow(TAB_SPEED);
            return;
        }
        if (!webUIAuthenticationOk()) {
            m_ui->tabSelection->setCurrentRow(TAB_WEBUI);
            return;
        }
        applyButton->setEnabled(false);
        this->hide();
        saveOptions();
    }
    saveWindowState();
    accept();
}

void OptionsDialog::applySettings(QAbstractButton* button)
{
    if (button == applyButton) {
        if (!schedTimesOk()) {
            m_ui->tabSelection->setCurrentRow(TAB_SPEED);
            return;
        }
        if (!webUIAuthenticationOk()) {
            m_ui->tabSelection->setCurrentRow(TAB_WEBUI);
            return;
        }
        saveOptions();
    }
}

void OptionsDialog::closeEvent(QCloseEvent *e)
{
    setAttribute(Qt::WA_DeleteOnClose);
    e->accept();
}

void OptionsDialog::on_buttonBox_rejected()
{
    setAttribute(Qt::WA_DeleteOnClose);
    reject();
}

bool OptionsDialog::useAdditionDialog() const
{
    return m_ui->checkAdditionDialog->isChecked();
}

void OptionsDialog::enableApplyButton()
{
    applyButton->setEnabled(true);
}

void OptionsDialog::enableProxy(int index)
{
    if (index) {
        //enable
        m_ui->lblProxyIP->setEnabled(true);
        m_ui->textProxyIP->setEnabled(true);
        m_ui->lblProxyPort->setEnabled(true);
        m_ui->spinProxyPort->setEnabled(true);
        m_ui->checkProxyPeerConnecs->setEnabled(true);
        m_ui->checkForceProxy->setEnabled(true);
        m_ui->isProxyOnlyForTorrents->setEnabled(true);
        if (index > 1) {
            m_ui->checkProxyAuth->setEnabled(true);
        }
        else {
            m_ui->checkProxyAuth->setEnabled(false);
            m_ui->checkProxyAuth->setChecked(false);
        }
    }
    else {
        //disable
        m_ui->lblProxyIP->setEnabled(false);
        m_ui->textProxyIP->setEnabled(false);
        m_ui->lblProxyPort->setEnabled(false);
        m_ui->spinProxyPort->setEnabled(false);
        m_ui->checkProxyPeerConnecs->setEnabled(false);
        m_ui->checkForceProxy->setEnabled(false);
        m_ui->isProxyOnlyForTorrents->setEnabled(false);
        m_ui->checkProxyAuth->setEnabled(false);
        m_ui->checkProxyAuth->setChecked(false);
    }
}

bool OptionsDialog::isSlashScreenDisabled() const
{
    return !m_ui->checkShowSplash->isChecked();
}

#ifdef Q_OS_WIN
bool OptionsDialog::WinStartup() const
{
    return m_ui->checkStartup->isChecked();
}
#endif

bool OptionsDialog::preventFromSuspend() const
{
    return m_ui->checkPreventFromSuspend->isChecked();
}

bool OptionsDialog::preAllocateAllFiles() const
{
    return m_ui->checkPreallocateAll->isChecked();
}

bool OptionsDialog::addTorrentsInPause() const
{
    return m_ui->checkStartPaused->isChecked();
}

// Proxy settings
bool OptionsDialog::isProxyEnabled() const
{
    return m_ui->comboProxyType->currentIndex();
}

bool OptionsDialog::isProxyAuthEnabled() const
{
    return m_ui->checkProxyAuth->isChecked();
}

QString OptionsDialog::getProxyIp() const
{
    return m_ui->textProxyIP->text().trimmed();
}

unsigned short OptionsDialog::getProxyPort() const
{
    return m_ui->spinProxyPort->value();
}

QString OptionsDialog::getProxyUsername() const
{
    QString username = m_ui->textProxyUsername->text().trimmed();
    return username;
}

QString OptionsDialog::getProxyPassword() const
{
    QString password = m_ui->textProxyPassword->text();
    password = password.trimmed();
    return password;
}

// Locale Settings
QString OptionsDialog::getLocale() const
{
    return m_ui->comboI18n->itemData(m_ui->comboI18n->currentIndex(), Qt::UserRole).toString();
}

void OptionsDialog::setLocale(const QString &localeStr)
{
    QString name;
    if (localeStr.startsWith("eo", Qt::CaseInsensitive)) {
        name = "eo";
    }
    else {
        QLocale locale(localeStr);
        name = locale.name();
    }
    // Attempt to find exact match
    int index = m_ui->comboI18n->findData(name, Qt::UserRole);
    if (index < 0) {
        //Attempt to find a language match without a country
        int pos = name.indexOf('_');
        if (pos > -1) {
            QString lang = name.left(pos);
            index = m_ui->comboI18n->findData(lang, Qt::UserRole);
        }
    }
    if (index < 0) {
        // Unrecognized, use US English
        index = m_ui->comboI18n->findData(QLocale("en").name(), Qt::UserRole);
        Q_ASSERT(index >= 0);
    }
    m_ui->comboI18n->setCurrentIndex(index);
}

QString OptionsDialog::getTorrentExportDir() const
{
    if (m_ui->checkExportDir->isChecked())
        return Utils::Fs::expandPathAbs(m_ui->textExportDir->text());
    return QString();
}

QString OptionsDialog::getFinishedTorrentExportDir() const
{
    if (m_ui->checkExportDirFin->isChecked())
        return Utils::Fs::expandPathAbs(m_ui->textExportDirFin->text());
    return QString();
}

// Return action on double-click on a downloading torrent set in options
int OptionsDialog::getActionOnDblClOnTorrentDl() const
{
    if (m_ui->actionTorrentDlOnDblClBox->currentIndex() < 1)
        return 0;
    return m_ui->actionTorrentDlOnDblClBox->currentIndex();
}

// Return action on double-click on a finished torrent set in options
int OptionsDialog::getActionOnDblClOnTorrentFn() const
{
    if (m_ui->actionTorrentFnOnDblClBox->currentIndex() < 1)
        return 0;
    return m_ui->actionTorrentFnOnDblClBox->currentIndex();
}

void OptionsDialog::on_addScanFolderButton_clicked()
{
    Preferences* const pref = Preferences::instance();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select folder to monitor"),
                                                          Utils::Fs::toNativePath(Utils::Fs::folderName(pref->getScanDirsLastPath())));
    if (!dir.isEmpty()) {
        const ScanFoldersModel::PathStatus status = ScanFoldersModel::instance()->addPath(dir, ScanFoldersModel::DEFAULT_LOCATION, QString(), false);
        QString error;
        switch (status) {
        case ScanFoldersModel::AlreadyInList:
            error = tr("Folder is already being monitored:");
            break;
        case ScanFoldersModel::DoesNotExist:
            error = tr("Folder does not exist:");
            break;
        case ScanFoldersModel::CannotRead:
            error = tr("Folder is not readable:");
            break;
        default:
            pref->setScanDirsLastPath(dir);
            addedScanDirs << dir;
            for (int i = 0; i < ScanFoldersModel::instance()->columnCount(); ++i)
                m_ui->scanFoldersView->resizeColumnToContents(i);
            enableApplyButton();
        }

        if (!error.isEmpty())
            QMessageBox::critical(this, tr("Adding entry failed"), QString("%1\n%2").arg(error).arg(dir));
    }
}

void OptionsDialog::on_removeScanFolderButton_clicked()
{
    const QModelIndexList selected
        = m_ui->scanFoldersView->selectionModel()->selectedIndexes();
    if (selected.isEmpty())
        return;
    Q_ASSERT(selected.count() == ScanFoldersModel::instance()->columnCount());
    foreach (const QModelIndex &index, selected) {
        if (index.column() == ScanFoldersModel::WATCH)
            removedScanDirs << index.data().toString();
    }
    ScanFoldersModel::instance()->removePath(selected.first().row(), false);
}

void OptionsDialog::handleScanFolderViewSelectionChanged()
{
    m_ui->removeScanFolderButton->setEnabled(!m_ui->scanFoldersView->selectionModel()->selectedIndexes().isEmpty());
}

QString OptionsDialog::askForExportDir(const QString& currentExportPath)
{
    QDir currentExportDir(Utils::Fs::expandPathAbs(currentExportPath));
    QString dir;
    if (!currentExportPath.isEmpty() && currentExportDir.exists())
        dir = QFileDialog::getExistingDirectory(this, tr("Choose export directory"), currentExportDir.absolutePath());
    else
        dir = QFileDialog::getExistingDirectory(this, tr("Choose export directory"), QDir::homePath());
    return dir;
}

void OptionsDialog::on_browseFileLogDir_clicked()
{
    const QString path = Utils::Fs::expandPathAbs(Utils::Fs::fromNativePath(m_ui->textFileLogPath->text()));
    QDir pathDir(path);
    QString dir;
    if (!path.isEmpty() && pathDir.exists())
        dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), pathDir.absolutePath());
    else
        dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), QDir::homePath());
    if (!dir.isNull())
        m_ui->textFileLogPath->setText(Utils::Fs::toNativePath(dir));
}

void OptionsDialog::on_browseExportDirButton_clicked()
{
    const QString newExportDir = askForExportDir(m_ui->textExportDir->text());
    if (!newExportDir.isNull())
        m_ui->textExportDir->setText(Utils::Fs::toNativePath(newExportDir));
}

void OptionsDialog::on_browseExportDirFinButton_clicked()
{
    const QString newExportDir = askForExportDir(m_ui->textExportDirFin->text());
    if (!newExportDir.isNull())
        m_ui->textExportDirFin->setText(Utils::Fs::toNativePath(newExportDir));
}

void OptionsDialog::on_browseFilterButton_clicked()
{
    QDir lastDir(Utils::Fs::fromNativePath(m_ui->textFilterPath->text()));
    QString lastPath = lastDir.exists() ? lastDir.absolutePath() : QDir::homePath();
    QString newPath = QFileDialog::getOpenFileName(this, tr("Choose an IP filter file"), lastPath, tr("All supported filters") + QString(" (*.dat *.p2p *.p2b);;.dat (*.dat);;.p2p (*.p2p);;.p2b (*.p2b)"));
    if (!newPath.isEmpty())
        m_ui->textFilterPath->setText(Utils::Fs::toNativePath(newPath));
}

// Display dialog to choose save dir
void OptionsDialog::on_browseSaveDirButton_clicked()
{
    const QString save_path = Utils::Fs::expandPathAbs(m_ui->textSavePath->text());
    QDir saveDir(save_path);
    QString dir;
    if (!save_path.isEmpty() && saveDir.exists())
        dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), saveDir.absolutePath());
    else
        dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), QDir::homePath());
    if (!dir.isNull())
        m_ui->textSavePath->setText(Utils::Fs::toNativePath(dir));
}

void OptionsDialog::on_browseTempDirButton_clicked()
{
    const QString temp_path = Utils::Fs::expandPathAbs(m_ui->textTempPath->text());
    QDir tempDir(temp_path);
    QString dir;
    if (!temp_path.isEmpty() && tempDir.exists())
        dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), tempDir.absolutePath());
    else
        dir = QFileDialog::getExistingDirectory(this, tr("Choose a save directory"), QDir::homePath());
    if (!dir.isNull())
        m_ui->textTempPath->setText(Utils::Fs::toNativePath(dir));
}

// Return Filter object to apply to BT session
QString OptionsDialog::getFilter() const
{
    return Utils::Fs::fromNativePath(m_ui->textFilterPath->text());
}

// Web UI

bool OptionsDialog::isWebUiEnabled() const
{
    return m_ui->checkWebUi->isChecked();
}

quint16 OptionsDialog::webUiPort() const
{
    return m_ui->spinWebUiPort->value();
}

QString OptionsDialog::webUiUsername() const
{
    return m_ui->textWebUiUsername->text();
}

QString OptionsDialog::webUiPassword() const
{
    return m_ui->textWebUiPassword->text();
}

void OptionsDialog::showConnectionTab()
{
    m_ui->tabSelection->setCurrentRow(TAB_CONNECTION);
}

void OptionsDialog::on_btnWebUiCrt_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, QString(), QString(), tr("SSL Certificate") + QString(" (*.crt *.pem)"));
    if (filename.isNull())
        return;
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        setSslCertificate(file.readAll());
        file.close();
    }
}

void OptionsDialog::on_btnWebUiKey_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this, QString(), QString(), tr("SSL Key") + QString(" (*.key *.pem)"));
    if (filename.isNull())
        return;
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        setSslKey(file.readAll());
        file.close();
    }
}

void OptionsDialog::on_registerDNSBtn_clicked()
{
    QDesktopServices::openUrl(Net::DNSUpdater::getRegistrationUrl(m_ui->comboDNSService->currentIndex()));
}

void OptionsDialog::on_IpFilterRefreshBtn_clicked()
{
    if (m_refreshingIpFilter) return;
    m_refreshingIpFilter = true;
    // Updating program preferences
    Preferences* const pref = Preferences::instance();
    pref->setFilteringEnabled(true);
    pref->setFilter(getFilter());
    // Force refresh
    connect(BitTorrent::Session::instance(), SIGNAL(ipFilterParsed(bool, int)), SLOT(handleIPFilterParsed(bool, int)));
    setCursor(QCursor(Qt::WaitCursor));
    BitTorrent::Session::instance()->enableIPFilter(getFilter(), true);
}

void OptionsDialog::handleIPFilterParsed(bool error, int ruleCount)
{
    setCursor(QCursor(Qt::ArrowCursor));
    if (error)
        QMessageBox::warning(this, tr("Parsing error"), tr("Failed to parse the provided IP filter"));
    else
        QMessageBox::information(this, tr("Successfully refreshed"), tr("Successfully parsed the provided IP filter: %1 rules were applied.", "%1 is a number").arg(ruleCount));
    m_refreshingIpFilter = false;
    disconnect(BitTorrent::Session::instance(), SIGNAL(ipFilterParsed(bool, int)), this, SLOT(handleIPFilterParsed(bool, int)));
}

QString OptionsDialog::languageToLocalizedString(const QLocale &locale)
{
    switch (locale.language()) {
    case QLocale::English: {
        if (locale.country() == QLocale::Australia)
            return QString::fromUtf8(C_LOCALE_ENGLISH_AUSTRALIA);
        else if (locale.country() == QLocale::UnitedKingdom)
            return QString::fromUtf8(C_LOCALE_ENGLISH_UNITEDKINGDOM);
        return QString::fromUtf8(C_LOCALE_ENGLISH);
    }
    case QLocale::French: return QString::fromUtf8(C_LOCALE_FRENCH);
    case QLocale::German: return QString::fromUtf8(C_LOCALE_GERMAN);
    case QLocale::Hungarian: return QString::fromUtf8(C_LOCALE_HUNGARIAN);
    case QLocale::Indonesian: return QString::fromUtf8(C_LOCALE_INDONESIAN);
    case QLocale::Italian: return QString::fromUtf8(C_LOCALE_ITALIAN);
    case QLocale::Dutch: return QString::fromUtf8(C_LOCALE_DUTCH);
    case QLocale::Spanish: return QString::fromUtf8(C_LOCALE_SPANISH);
    case QLocale::Catalan: return QString::fromUtf8(C_LOCALE_CATALAN);
    case QLocale::Galician: return QString::fromUtf8(C_LOCALE_GALICIAN);
    case QLocale::Portuguese: {
        if (locale.country() == QLocale::Brazil)
            return QString::fromUtf8(C_LOCALE_PORTUGUESE_BRAZIL);
        return QString::fromUtf8(C_LOCALE_PORTUGUESE);
    }
    case QLocale::Polish: return QString::fromUtf8(C_LOCALE_POLISH);
    case QLocale::Lithuanian: return QString::fromUtf8(C_LOCALE_LITHUANIAN);
    case QLocale::Czech: return QString::fromUtf8(C_LOCALE_CZECH);
    case QLocale::Slovak: return QString::fromUtf8(C_LOCALE_SLOVAK);
    case QLocale::Slovenian: return QString::fromUtf8(C_LOCALE_SLOVENIAN);
    case QLocale::Serbian: return QString::fromUtf8(C_LOCALE_SERBIAN);
    case QLocale::Croatian: return QString::fromUtf8(C_LOCALE_CROATIAN);
    case QLocale::Armenian: return QString::fromUtf8(C_LOCALE_ARMENIAN);
    case QLocale::Romanian: return QString::fromUtf8(C_LOCALE_ROMANIAN);
    case QLocale::Turkish: return QString::fromUtf8(C_LOCALE_TURKISH);
    case QLocale::Greek: return QString::fromUtf8(C_LOCALE_GREEK);
    case QLocale::Swedish: return QString::fromUtf8(C_LOCALE_SWEDISH);
    case QLocale::Finnish: return QString::fromUtf8(C_LOCALE_FINNISH);
    case QLocale::Norwegian: return QString::fromUtf8(C_LOCALE_NORWEGIAN);
    case QLocale::Danish: return QString::fromUtf8(C_LOCALE_DANISH);
    case QLocale::Bulgarian: return QString::fromUtf8(C_LOCALE_BULGARIAN);
    case QLocale::Ukrainian: return QString::fromUtf8(C_LOCALE_UKRAINIAN);
    case QLocale::Russian: return QString::fromUtf8(C_LOCALE_RUSSIAN);
    case QLocale::Japanese: return QString::fromUtf8(C_LOCALE_JAPANESE);
    case QLocale::Hebrew: return QString::fromUtf8(C_LOCALE_HEBREW);
    case QLocale::Hindi: return QString::fromUtf8(C_LOCALE_HINDI);
    case QLocale::Arabic: return QString::fromUtf8(C_LOCALE_ARABIC);
    case QLocale::Georgian: return QString::fromUtf8(C_LOCALE_GEORGIAN);
    case QLocale::Byelorussian: return QString::fromUtf8(C_LOCALE_BYELORUSSIAN);
    case QLocale::Basque: return QString::fromUtf8(C_LOCALE_BASQUE);
    case QLocale::Vietnamese: return QString::fromUtf8(C_LOCALE_VIETNAMESE);
    case QLocale::Chinese: {
        switch (locale.country()) {
        case QLocale::China:
            return QString::fromUtf8(C_LOCALE_CHINESE_SIMPLIFIED);
        case QLocale::HongKong:
            return QString::fromUtf8(C_LOCALE_CHINESE_TRADITIONAL_HK);
        default:
            return QString::fromUtf8(C_LOCALE_CHINESE_TRADITIONAL_TW);

        }
    }
    case QLocale::Korean: return QString::fromUtf8(C_LOCALE_KOREAN);
    default: {
        // Fallback to English
        const QString eng_lang = QLocale::languageToString(locale.language());
        qWarning() << "Unrecognized language name: " << eng_lang;
        return eng_lang;
    }
    }
}

void OptionsDialog::setSslKey(const QByteArray &key, bool interactive)
{
#ifndef QT_NO_OPENSSL
    if (!key.isEmpty() && !QSslKey(key, QSsl::Rsa).isNull()) {
        m_ui->lblSslKeyStatus->setPixmap(QPixmap(":/icons/oxygen/security-high.png").scaledToHeight(20, Qt::SmoothTransformation));
        m_sslKey = key;
    }
    else {
        m_ui->lblSslKeyStatus->setPixmap(QPixmap(":/icons/oxygen/security-low.png").scaledToHeight(20, Qt::SmoothTransformation));
        m_sslKey.clear();
        if (interactive)
            QMessageBox::warning(this, tr("Invalid key"), tr("This is not a valid SSL key."));
    }
#endif
}

void OptionsDialog::setSslCertificate(const QByteArray &cert, bool interactive)
{
#ifndef QT_NO_OPENSSL
    if (!cert.isEmpty() && !QSslCertificate(cert).isNull()) {
        m_ui->lblSslCertStatus->setPixmap(QPixmap(":/icons/oxygen/security-high.png").scaledToHeight(20, Qt::SmoothTransformation));
        m_sslCert = cert;
    }
    else {
        m_ui->lblSslCertStatus->setPixmap(QPixmap(":/icons/oxygen/security-low.png").scaledToHeight(20, Qt::SmoothTransformation));
        m_sslCert.clear();
        if (interactive)
            QMessageBox::warning(this, tr("Invalid certificate"), tr("This is not a valid SSL certificate."));
    }
#endif
}

bool OptionsDialog::schedTimesOk()
{
    if (m_ui->schedule_from->time() == m_ui->schedule_to->time()) {
        QMessageBox::warning(this, tr("Time Error"), tr("The start time and the end time can't be the same."));
        return false;
    }
    return true;
}

bool OptionsDialog::webUIAuthenticationOk()
{
    if (webUiUsername().length() < 3) {
        QMessageBox::warning(this, tr("Length Error"), tr("The Web UI username must be at least 3 characters long."));
        return false;
    }
    if (webUiPassword().length() < 6) {
        QMessageBox::warning(this, tr("Length Error"), tr("The Web UI password must be at least 6 characters long."));
        return false;
    }
    return true;
}

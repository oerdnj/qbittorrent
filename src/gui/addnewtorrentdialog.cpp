/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2012  Christophe Dumez
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

#include <QDebug>
#include <QString>
#include <QFile>
#include <QUrl>
#include <QMenu>
#include <QFileDialog>

#include "base/settingsstorage.h"
#include "base/net/downloadmanager.h"
#include "base/net/downloadhandler.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/magneturi.h"
#include "base/bittorrent/torrentinfo.h"
#include "base/bittorrent/torrenthandle.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "base/torrentfileguard.h"
#include "base/unicodestrings.h"
#include "guiiconprovider.h"
#include "autoexpandabledialog.h"
#include "messageboxraised.h"
#include "proplistdelegate.h"
#include "torrentcontentmodel.h"
#include "torrentcontentfiltermodel.h"
#include "ui_addnewtorrentdialog.h"
#include "addnewtorrentdialog.h"

#define SETTINGS_KEY(name) "AddNewTorrentDialog/" name
const QString KEY_ENABLED = SETTINGS_KEY("Enabled");
const QString KEY_DEFAULTSAVEPATH = SETTINGS_KEY("DefaultSavePath");
const QString KEY_DEFAULTCATEGORY = SETTINGS_KEY("DefaultCategory");
const QString KEY_TREEHEADERSTATE = SETTINGS_KEY("TreeHeaderState");
const QString KEY_WIDTH = SETTINGS_KEY("Width");
const QString KEY_EXPANDED = SETTINGS_KEY("Expanded");
const QString KEY_POSITION = SETTINGS_KEY("Position");
const QString KEY_TOPLEVEL = SETTINGS_KEY("TopLevel");
const QString KEY_SAVEPATHHISTORY = SETTINGS_KEY("SavePathHistory");

namespace
{
    //just a shortcut
    inline SettingsStorage *settings() { return  SettingsStorage::instance(); }
}

AddNewTorrentDialog::AddNewTorrentDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AddNewTorrentDialog)
    , m_contentModel(0)
    , m_contentDelegate(0)
    , m_hasMetadata(false)
    , m_oldIndex(0)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    ui->lblMetaLoading->setVisible(false);
    ui->progMetaLoading->setVisible(false);

    auto session = BitTorrent::Session::instance();

    ui->startTorrentCheckBox->setChecked(!session->isAddTorrentPaused());
    ui->comboTTM->blockSignals(true); //the TreeView size isn't correct if the slot does it job at this point
    ui->comboTTM->setCurrentIndex(!session->isAutoTMMDisabledByDefault());
    ui->comboTTM->blockSignals(false);
    populateSavePathComboBox();
    connect(ui->savePathComboBox, SIGNAL(currentIndexChanged(int)), SLOT(onSavePathChanged(int)));
    connect(ui->browseButton, SIGNAL(clicked()), SLOT(browseButton_clicked()));
    ui->defaultSavePathCheckBox->setVisible(false); // Default path is selected by default

    ui->doNotDeleteTorrentCheckBox->setVisible(TorrentFileGuard::autoDeleteMode() != TorrentFileGuard::Never);

    // Load categories
    QStringList categories = session->categories();
    std::sort(categories.begin(), categories.end(), Utils::String::naturalCompareCaseInsensitive);
    QString defaultCategory = settings()->loadValue(KEY_DEFAULTCATEGORY).toString();

    if (!defaultCategory.isEmpty())
        ui->categoryComboBox->addItem(defaultCategory);
    ui->categoryComboBox->addItem("");

    foreach (const QString &category, categories)
        if (category != defaultCategory)
            ui->categoryComboBox->addItem(category);

    ui->categoryComboBox->model()->sort(0);
    ui->contentTreeView->header()->setSortIndicator(0, Qt::AscendingOrder);
    loadState();
    // Signal / slots
    connect(ui->adv_button, SIGNAL(clicked(bool)), SLOT(showAdvancedSettings(bool)));
    connect(ui->doNotDeleteTorrentCheckBox, SIGNAL(clicked(bool)), SLOT(doNotDeleteTorrentClicked(bool)));
    editHotkey = new QShortcut(QKeySequence("F2"), ui->contentTreeView, 0, 0, Qt::WidgetShortcut);
    connect(editHotkey, SIGNAL(activated()), SLOT(renameSelectedFile()));
    connect(ui->contentTreeView, SIGNAL(doubleClicked(QModelIndex)), SLOT(renameSelectedFile()));

    ui->buttonBox->button(QDialogButtonBox::Ok)->setFocus();
}

AddNewTorrentDialog::~AddNewTorrentDialog()
{
    saveState();
    delete ui;
    if (m_contentModel)
        delete m_contentModel;
    delete editHotkey;
}

bool AddNewTorrentDialog::isEnabled()
{
    return SettingsStorage::instance()->loadValue(KEY_ENABLED, true).toBool();
}

void AddNewTorrentDialog::setEnabled(bool value)
{
    SettingsStorage::instance()->storeValue(KEY_ENABLED, value);
}

bool AddNewTorrentDialog::isTopLevel()
{
    return SettingsStorage::instance()->loadValue(KEY_TOPLEVEL, true).toBool();
}

void AddNewTorrentDialog::setTopLevel(bool value)
{
    SettingsStorage::instance()->storeValue(KEY_TOPLEVEL, value);
}

void AddNewTorrentDialog::loadState()
{
    m_headerState = settings()->loadValue(KEY_TREEHEADERSTATE).toByteArray();
    int width = settings()->loadValue(KEY_WIDTH, -1).toInt();
    if (width >= 0) {
        QRect geo = geometry();
        geo.setWidth(width);
        setGeometry(geo);
    }
    ui->adv_button->setChecked(settings()->loadValue(KEY_EXPANDED).toBool());
}

void AddNewTorrentDialog::saveState()
{
    if (m_contentModel)
        settings()->storeValue(KEY_TREEHEADERSTATE, ui->contentTreeView->header()->saveState());
    settings()->storeValue(KEY_POSITION, pos().y());
    settings()->storeValue(KEY_WIDTH, width());
    settings()->storeValue(KEY_EXPANDED, ui->adv_button->isChecked());
}

void AddNewTorrentDialog::show(QString source, QWidget *parent)
{
    AddNewTorrentDialog *dlg = new AddNewTorrentDialog(parent);

    if (Utils::Misc::isUrl(source)) {
        // Launch downloader
        Net::DownloadHandler *handler = Net::DownloadManager::instance()->downloadUrl(source, true, 10485760 /* 10MB */, true);
        connect(handler, SIGNAL(downloadFinished(QString, QString)), dlg, SLOT(handleDownloadFinished(QString, QString)));
        connect(handler, SIGNAL(downloadFailed(QString, QString)), dlg, SLOT(handleDownloadFailed(QString, QString)));
        connect(handler, SIGNAL(redirectedToMagnet(QString, QString)), dlg, SLOT(handleRedirectedToMagnet(QString, QString)));
    }
    else {
        bool ok = false;
        BitTorrent::MagnetUri magnetUri(source);
        if (magnetUri.isValid())
            ok = dlg->loadMagnet(magnetUri);
        else
            ok = dlg->loadTorrent(source);

        if (ok)
            dlg->open();
        else
            delete dlg;
    }
}

bool AddNewTorrentDialog::loadTorrent(const QString &torrentPath)
{
    if (torrentPath.startsWith("file://", Qt::CaseInsensitive))
        m_filePath = QUrl::fromEncoded(torrentPath.toLocal8Bit()).toLocalFile();
    else
        m_filePath = torrentPath;

    if (!QFile::exists(m_filePath)) {
        MessageBoxRaised::critical(0, tr("I/O Error"), tr("The torrent file '%1' does not exist.").arg(Utils::Fs::toNativePath(m_filePath)));
        return false;
    }

    QFileInfo fileinfo(m_filePath);
    if (!fileinfo.isReadable()) {
        MessageBoxRaised::critical(0, tr("I/O Error"), tr("The torrent file '%1' cannot be read from the disk. Probably you don't have enough permissions.").arg(Utils::Fs::toNativePath(m_filePath)));
        return false;
    }

    m_hasMetadata = true;
    QString error;
    m_torrentInfo = BitTorrent::TorrentInfo::loadFromFile(m_filePath, error);
    if (!m_torrentInfo.isValid()) {
        MessageBoxRaised::critical(0, tr("Invalid torrent"), tr("Failed to load the torrent: %1.\nError: %2", "Don't remove the '\n' characters. They insert a newline.").arg(Utils::Fs::toNativePath(m_filePath)).arg(error));
        return false;
    }

    m_torrentGuard.reset(new TorrentFileGuard(m_filePath));
    m_hash = m_torrentInfo.hash();

    // Prevent showing the dialog if download is already present
    if (BitTorrent::Session::instance()->isKnownTorrent(m_hash)) {
        BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(m_hash);
        if (torrent) {
            if (torrent->isPrivate() || m_torrentInfo.isPrivate()) {
                MessageBoxRaised::critical(0, tr("Already in download list"), tr("Torrent is already in download list. Trackers weren't merged because it is a private torrent."), QMessageBox::Ok);
            }
            else {
                torrent->addTrackers(m_torrentInfo.trackers());
                torrent->addUrlSeeds(m_torrentInfo.urlSeeds());
                MessageBoxRaised::information(0, tr("Already in download list"), tr("Torrent is already in download list. Trackers were merged."), QMessageBox::Ok);
            }
        }
        else {
            MessageBoxRaised::critical(0, tr("Cannot add torrent"), tr("Cannot add this torrent. Perhaps it is already in adding state."), QMessageBox::Ok);
        }
        return false;
    }

    ui->lblhash->setText(m_hash);
    setupTreeview();
    TMMChanged(ui->comboTTM->currentIndex());
    return true;
}

bool AddNewTorrentDialog::loadMagnet(const BitTorrent::MagnetUri &magnetUri)
{
    if (!magnetUri.isValid()) {
        MessageBoxRaised::critical(0, tr("Invalid magnet link"), tr("This magnet link was not recognized"));
        return false;
    }

    m_torrentGuard.reset(new TorrentFileGuard(QString()));
    m_hash = magnetUri.hash();
    // Prevent showing the dialog if download is already present
    if (BitTorrent::Session::instance()->isKnownTorrent(m_hash)) {
        BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(m_hash);
        if (torrent) {
            if (torrent->isPrivate()) {
                MessageBoxRaised::critical(0, tr("Already in download list"), tr("Torrent is already in download list. Trackers weren't merged because it is a private torrent."), QMessageBox::Ok);
            }
            else {
                torrent->addTrackers(magnetUri.trackers());
                torrent->addUrlSeeds(magnetUri.urlSeeds());
                MessageBoxRaised::information(0, tr("Already in download list"), tr("Magnet link is already in download list. Trackers were merged."), QMessageBox::Ok);
            }
        }
        else {
            MessageBoxRaised::critical(0, tr("Cannot add torrent"), tr("Cannot add this torrent. Perhaps it is already in adding."), QMessageBox::Ok);
        }
        return false;
    }

    connect(BitTorrent::Session::instance(), SIGNAL(metadataLoaded(BitTorrent::TorrentInfo)), SLOT(updateMetadata(BitTorrent::TorrentInfo)));

    // Set dialog title
    QString torrent_name = magnetUri.name();
    setWindowTitle(torrent_name.isEmpty() ? tr("Magnet link") : torrent_name);

    setupTreeview();
    TMMChanged(ui->comboTTM->currentIndex());
    // Set dialog position
    setdialogPosition();

    BitTorrent::Session::instance()->loadMetadata(magnetUri);
    setMetadataProgressIndicator(true, tr("Retrieving metadata..."));
    ui->lblhash->setText(m_hash);

    return true;
}

void AddNewTorrentDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!isTopLevel()) return;

    activateWindow();
    raise();
}


void AddNewTorrentDialog::showAdvancedSettings(bool show)
{
    const int minimumW = minimumWidth();
    setMinimumWidth(width());  // to remain the same width
    if (show) {
        ui->adv_button->setText(QString::fromUtf8(C_UP));
        ui->settings_group->setVisible(true);
        ui->infoGroup->setVisible(true);
        ui->contentTreeView->setVisible(m_hasMetadata);
        static_cast<QVBoxLayout*>(layout())->insertWidget(layout()->indexOf(ui->never_show_cb) + 1, ui->adv_button);
    }
    else {
        ui->adv_button->setText(QString::fromUtf8(C_DOWN));
        ui->settings_group->setVisible(false);
        ui->infoGroup->setVisible(false);
        ui->buttonsHLayout->insertWidget(0, layout()->takeAt(layout()->indexOf(ui->never_show_cb) + 1)->widget());
    }
    adjustSize();
    setMinimumWidth(minimumW);
}

void AddNewTorrentDialog::saveSavePathHistory() const
{
    QDir selectedSavePath(ui->savePathComboBox->itemData(ui->savePathComboBox->currentIndex()).toString());
    // Get current history
    QStringList history = settings()->loadValue(KEY_SAVEPATHHISTORY).toStringList();
    QList<QDir> historyDirs;
    foreach(const QString dir, history)
        historyDirs << QDir(dir);
    if (!historyDirs.contains(selectedSavePath)) {
        // Add save path to history
        history.push_front(selectedSavePath.absolutePath());
        // Limit list size
        if (history.size() > 8)
            history.pop_back();
        // Save history
        settings()->storeValue(KEY_SAVEPATHHISTORY, history);
    }
}

// save_path is a folder, not an absolute file path
int AddNewTorrentDialog::indexOfSavePath(const QString &save_path)
{
    QDir saveDir(save_path);
    for (int i = 0; i < ui->savePathComboBox->count(); ++i)
        if (QDir(ui->savePathComboBox->itemData(i).toString()) == saveDir)
            return i;
    return -1;
}

void AddNewTorrentDialog::updateDiskSpaceLabel()
{
    // Determine torrent size
    qulonglong torrent_size = 0;

    if (m_hasMetadata) {
        if (m_contentModel) {
            const QVector<int> priorities = m_contentModel->model()->getFilePriorities();
            Q_ASSERT(priorities.size() == m_torrentInfo.filesCount());
            for (int i = 0; i < priorities.size(); ++i)
                if (priorities[i] > 0)
                    torrent_size += m_torrentInfo.fileSize(i);
        }
        else {
            torrent_size = m_torrentInfo.totalSize();
        }
    }

    QString size_string = torrent_size ? Utils::Misc::friendlyUnit(torrent_size) : QString(tr("Not Available", "This size is unavailable."));
    size_string += " (";
    size_string += tr("Free space on disk: %1").arg(Utils::Misc::friendlyUnit(Utils::Fs::freeDiskSpaceOnPath(
                                                                   ui->savePathComboBox->itemData(
                                                                       ui->savePathComboBox->currentIndex()).toString())));
    size_string += ")";
    ui->size_lbl->setText(size_string);
}

void AddNewTorrentDialog::onSavePathChanged(int index)
{
    // Toggle default save path setting checkbox visibility
    ui->defaultSavePathCheckBox->setChecked(false);
    ui->defaultSavePathCheckBox->setVisible(
                QDir(ui->savePathComboBox->itemData(ui->savePathComboBox->currentIndex()).toString())
                != QDir(defaultSavePath()));

    // Remember index
    m_oldIndex = index;

    updateDiskSpaceLabel();
}

void AddNewTorrentDialog::categoryChanged(int index)
{
    Q_UNUSED(index);

    if (ui->comboTTM->currentIndex() == 1) {
        QString savePath = BitTorrent::Session::instance()->categorySavePath(ui->categoryComboBox->currentText());
        ui->savePathComboBox->setItemText(0, Utils::Fs::toNativePath(savePath));
        ui->savePathComboBox->setItemData(0, savePath);
    }
}

void AddNewTorrentDialog::browseButton_clicked()
{
    disconnect(ui->savePathComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onSavePathChanged(int)));

    // User is asking for a new save path
    QString curSavePath = ui->savePathComboBox->itemText(m_oldIndex);
    QString newPath;

    if (!curSavePath.isEmpty() && QDir(curSavePath).exists())
        newPath = QFileDialog::getExistingDirectory(this, tr("Choose save path"), curSavePath);
    else
        newPath = QFileDialog::getExistingDirectory(this, tr("Choose save path"), QDir::homePath());

    if (!newPath.isEmpty()) {
        const int existingIndex = indexOfSavePath(newPath);
        if (existingIndex >= 0) {
            ui->savePathComboBox->setCurrentIndex(existingIndex);
        }
        else {
            // New path, prepend to combo box
            ui->savePathComboBox->insertItem(0, Utils::Fs::toNativePath(newPath), newPath);
            ui->savePathComboBox->setCurrentIndex(0);
        }

        onSavePathChanged(0);
    }
    else {
        // Restore index
        ui->savePathComboBox->setCurrentIndex(m_oldIndex);
    }

    connect(ui->savePathComboBox, SIGNAL(currentIndexChanged(int)), SLOT(onSavePathChanged(int)));
}

void AddNewTorrentDialog::renameSelectedFile()
{
    const QModelIndexList selectedIndexes = ui->contentTreeView->selectionModel()->selectedRows(0);
    if (selectedIndexes.size() != 1)
        return;
    const QModelIndex &index = selectedIndexes.first();
    if (!index.isValid())
        return;
    // Ask for new name
    bool ok;
    const QString new_name_last = AutoExpandableDialog::getText(this, tr("Rename the file"),
                                                                tr("New name:"), QLineEdit::Normal,
                                                                index.data().toString(), &ok).trimmed();
    if (ok && !new_name_last.isEmpty()) {
        if (!Utils::Fs::isValidFileSystemName(new_name_last)) {
            MessageBoxRaised::warning(this, tr("The file could not be renamed"),
                                      tr("This file name contains forbidden characters, please choose a different one."),
                                      QMessageBox::Ok);
            return;
        }
        if (m_contentModel->itemType(index) == TorrentContentModelItem::FileType) {
            // File renaming
            const int file_index = m_contentModel->getFileIndex(index);
            QString old_name = Utils::Fs::fromNativePath(m_torrentInfo.filePath(file_index));
            qDebug("Old name: %s", qPrintable(old_name));
            QStringList path_items = old_name.split("/");
            path_items.removeLast();
            path_items << new_name_last;
            QString new_name = path_items.join("/");
            if (Utils::Fs::sameFileNames(old_name, new_name)) {
                qDebug("Name did not change");
                return;
            }
            new_name = Utils::Fs::expandPath(new_name);
            qDebug("New name: %s", qPrintable(new_name));
            // Check if that name is already used
            for (int i = 0; i < m_torrentInfo.filesCount(); ++i) {
                if (i == file_index) continue;
                if (Utils::Fs::sameFileNames(m_torrentInfo.filePath(i), new_name)) {
                    // Display error message
                    MessageBoxRaised::warning(this, tr("The file could not be renamed"),
                                              tr("This name is already in use in this folder. Please use a different name."),
                                              QMessageBox::Ok);
                    return;
                }
            }
            qDebug("Renaming %s to %s", qPrintable(old_name), qPrintable(new_name));
            m_torrentInfo.renameFile(file_index, new_name);
            // Rename in torrent files model too
            m_contentModel->setData(index, new_name_last);
        }
        else {
            // Folder renaming
            QStringList path_items;
            path_items << index.data().toString();
            QModelIndex parent = m_contentModel->parent(index);
            while(parent.isValid()) {
                path_items.prepend(parent.data().toString());
                parent = m_contentModel->parent(parent);
            }
            const QString old_path = path_items.join("/");
            path_items.removeLast();
            path_items << new_name_last;
            QString new_path = path_items.join("/");
            if (Utils::Fs::sameFileNames(old_path, new_path)) {
                qDebug("Name did not change");
                return;
            }
            if (!new_path.endsWith("/")) new_path += "/";
            // Check for overwriting
            for (int i = 0; i < m_torrentInfo.filesCount(); ++i) {
                const QString &current_name = m_torrentInfo.filePath(i);
#if defined(Q_OS_UNIX) || defined(Q_WS_QWS)
                if (current_name.startsWith(new_path, Qt::CaseSensitive)) {
#else
                if (current_name.startsWith(new_path, Qt::CaseInsensitive)) {
#endif
                    MessageBoxRaised::warning(this, tr("The folder could not be renamed"),
                                              tr("This name is already in use in this folder. Please use a different name."),
                                              QMessageBox::Ok);
                    return;
                }
            }
            // Replace path in all files
            for (int i = 0; i < m_torrentInfo.filesCount(); ++i) {
                const QString &current_name = m_torrentInfo.filePath(i);
                if (current_name.startsWith(old_path)) {
                    QString new_name = current_name;
                    new_name.replace(0, old_path.length(), new_path);
                    new_name = Utils::Fs::expandPath(new_name);
                    qDebug("Rename %s to %s", qPrintable(current_name), qPrintable(new_name));
                    m_torrentInfo.renameFile(i, new_name);
                }
            }

            // Rename folder in torrent files model too
            m_contentModel->setData(index, new_name_last);
        }
    }
}

void AddNewTorrentDialog::setdialogPosition()
{
    qApp->processEvents();
    QPoint center(Utils::Misc::screenCenter(this));
    // Adjust y
    int y = settings()->loadValue(KEY_POSITION, -1).toInt();
    if (y >= 0) {
        center.setY(y);
    }
    else {
        center.ry() -= 120;
        if (center.y() < 0)
            center.setY(0);
    }
    move(center);
}

void AddNewTorrentDialog::populateSavePathComboBox()
{
    QString defSavePath = defaultSavePath();

    ui->savePathComboBox->clear();
    ui->savePathComboBox->addItem(Utils::Fs::toNativePath(defSavePath), defSavePath);
    QDir defaultSaveDir(defSavePath);
    // Load save path history
    foreach (const QString &savePath, settings()->loadValue(KEY_SAVEPATHHISTORY).toStringList())
        if (QDir(savePath) != defaultSaveDir)
            ui->savePathComboBox->addItem(Utils::Fs::toNativePath(savePath), savePath);
}

void AddNewTorrentDialog::displayContentTreeMenu(const QPoint&)
{
    QMenu myFilesLlistMenu;
    const QModelIndexList selectedRows = ui->contentTreeView->selectionModel()->selectedRows(0);
    QAction *actRename = 0;
    if (selectedRows.size() == 1) {
        actRename = myFilesLlistMenu.addAction(GuiIconProvider::instance()->getIcon("edit-rename"), tr("Rename..."));
        myFilesLlistMenu.addSeparator();
    }
    QMenu subMenu;
    subMenu.setTitle(tr("Priority"));
    subMenu.addAction(ui->actionNot_downloaded);
    subMenu.addAction(ui->actionNormal);
    subMenu.addAction(ui->actionHigh);
    subMenu.addAction(ui->actionMaximum);
    myFilesLlistMenu.addMenu(&subMenu);
    // Call menu
    QAction *act = myFilesLlistMenu.exec(QCursor::pos());
    if (act) {
        if (act == actRename) {
            renameSelectedFile();
        }
        else {
            int prio = prio::NORMAL;
            if (act == ui->actionHigh)
                prio = prio::HIGH;
            else if (act == ui->actionMaximum)
                prio = prio::MAXIMUM;
            else if (act == ui->actionNot_downloaded)
                prio = prio::IGNORED;

            qDebug("Setting files priority");
            foreach (const QModelIndex &index, selectedRows) {
                qDebug("Setting priority(%d) for file at row %d", prio, index.row());
                m_contentModel->setData(m_contentModel->index(index.row(), PRIORITY, index.parent()), prio);
            }
        }
    }
}

void AddNewTorrentDialog::accept()
{
    if (!m_hasMetadata)
        disconnect(this, SLOT(updateMetadata(const BitTorrent::TorrentInfo &)));

    BitTorrent::AddTorrentParams params;

    if (ui->skip_check_cb->isChecked())
        // TODO: Check if destination actually exists
        params.skipChecking = true;

    // Category
    params.category = ui->categoryComboBox->currentText();

    if (ui->defaultCategoryCheckbox->isChecked())
        settings()->storeValue(KEY_DEFAULTCATEGORY, params.category);

    // Save file priorities
    if (m_contentModel)
        params.filePriorities = m_contentModel->model()->getFilePriorities();

    params.addPaused = !ui->startTorrentCheckBox->isChecked();

    QString savePath = ui->savePathComboBox->itemData(ui->savePathComboBox->currentIndex()).toString();
    if (ui->comboTTM->currentIndex() != 1) { // 0 is Manual mode and 1 is Automatic mode. Handle all non 1 values as manual mode.
        params.savePath = savePath;
        saveSavePathHistory();
        if (ui->defaultSavePathCheckBox->isChecked())
            settings()->storeValue(KEY_DEFAULTSAVEPATH, savePath);
    }

    setEnabled(!ui->never_show_cb->isChecked());

    // Add torrent
    if (!m_hasMetadata)
        BitTorrent::Session::instance()->addTorrent(m_hash, params);
    else
        BitTorrent::Session::instance()->addTorrent(m_torrentInfo, params);

    m_torrentGuard->markAsAddedToSession();
    QDialog::accept();
}

void AddNewTorrentDialog::reject()
{
    if (!m_hasMetadata) {
        disconnect(this, SLOT(updateMetadata(BitTorrent::TorrentInfo)));
        setMetadataProgressIndicator(false);
        BitTorrent::Session::instance()->cancelLoadMetadata(m_hash);
    }

    QDialog::reject();
}

void AddNewTorrentDialog::updateMetadata(const BitTorrent::TorrentInfo &info)
{
    if (info.hash() != m_hash) return;

    disconnect(this, SLOT(updateMetadata(BitTorrent::TorrentInfo)));
    if (!info.isValid()) {
        MessageBoxRaised::critical(0, tr("I/O Error"), ("Invalid metadata."));
        setMetadataProgressIndicator(false, tr("Invalid metadata"));
        return;
    }

    // Good to go
    m_torrentInfo = info;
    m_hasMetadata = true;
    setMetadataProgressIndicator(true, tr("Parsing metadata..."));

    // Update UI
    setupTreeview();
    TMMChanged(ui->comboTTM->currentIndex());
    setMetadataProgressIndicator(false, tr("Metadata retrieval complete"));
}

void AddNewTorrentDialog::setMetadataProgressIndicator(bool visibleIndicator, const QString &labelText)
{
    // Always show info label when waiting for metadata
    ui->lblMetaLoading->setVisible(true);
    ui->lblMetaLoading->setText(labelText);
    ui->progMetaLoading->setVisible(visibleIndicator);
}

void AddNewTorrentDialog::setupTreeview()
{
    if (!m_hasMetadata) {
        setCommentText(tr("Not Available", "This comment is unavailable"));
        ui->date_lbl->setText(tr("Not Available", "This date is unavailable"));
    }
    else {
        // Set dialog title
        setWindowTitle(m_torrentInfo.name());

        // Set torrent information
        setCommentText(Utils::Misc::parseHtmlLinks(m_torrentInfo.comment()));
        ui->date_lbl->setText(!m_torrentInfo.creationDate().isNull() ? m_torrentInfo.creationDate().toString(Qt::DefaultLocaleShortDate) : tr("Not available"));

        // Prepare content tree
        m_contentModel = new TorrentContentFilterModel(this);
        connect(m_contentModel->model(), SIGNAL(filteredFilesChanged()), SLOT(updateDiskSpaceLabel()));
        ui->contentTreeView->setModel(m_contentModel);
        m_contentDelegate = new PropListDelegate();
        ui->contentTreeView->setItemDelegate(m_contentDelegate);
        connect(ui->contentTreeView, SIGNAL(clicked(const QModelIndex &)), ui->contentTreeView, SLOT(edit(const QModelIndex &)));
        connect(ui->contentTreeView, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(displayContentTreeMenu(const QPoint &)));

        // List files in torrent
        m_contentModel->model()->setupModelData(m_torrentInfo);
        if (!m_headerState.isEmpty())
            ui->contentTreeView->header()->restoreState(m_headerState);

        // Hide useless columns after loading the header state
        ui->contentTreeView->hideColumn(PROGRESS);
        ui->contentTreeView->hideColumn(REMAINING);

        // Expand root folder
        ui->contentTreeView->setExpanded(m_contentModel->index(0, 0), true);
    }

    updateDiskSpaceLabel();
    showAdvancedSettings(settings()->loadValue(KEY_EXPANDED, false).toBool());
    // Set dialog position
    setdialogPosition();
}

QString AddNewTorrentDialog::defaultSavePath() const
{
    return Utils::Fs::fromNativePath(
                settings()->loadValue(KEY_DEFAULTSAVEPATH,
                                      BitTorrent::Session::instance()->defaultSavePath()).toString());
}

void AddNewTorrentDialog::handleDownloadFailed(const QString &url, const QString &reason)
{
    MessageBoxRaised::critical(0, tr("Download Error"), QString("Cannot download '%1': %2").arg(url).arg(reason));
    this->deleteLater();
}

void AddNewTorrentDialog::handleRedirectedToMagnet(const QString &url, const QString &magnetUri)
{
    Q_UNUSED(url)
    if (loadMagnet(BitTorrent::MagnetUri(magnetUri)))
        open();
    else
        this->deleteLater();
}

void AddNewTorrentDialog::handleDownloadFinished(const QString &url, const QString &filePath)
{
    Q_UNUSED(url)
    if (loadTorrent(filePath))
        open();
    else
        this->deleteLater();
}

void AddNewTorrentDialog::TMMChanged(int index)
{
    if (index != 1) { // 0 is Manual mode and 1 is Automatic mode. Handle all non 1 values as manual mode.
        populateSavePathComboBox();
        ui->groupBoxSavePath->setEnabled(true);
        ui->savePathComboBox->blockSignals(false);
        ui->savePathComboBox->setCurrentIndex(m_oldIndex < ui->savePathComboBox->count() ? m_oldIndex : ui->savePathComboBox->count() - 1);
        ui->adv_button->setEnabled(true);
    }
    else {
        ui->groupBoxSavePath->setEnabled(false);
        ui->savePathComboBox->blockSignals(true);
        ui->savePathComboBox->clear();
        QString savePath = BitTorrent::Session::instance()->categorySavePath(ui->categoryComboBox->currentText());
        ui->savePathComboBox->addItem(Utils::Fs::toNativePath(savePath), savePath);
        ui->defaultSavePathCheckBox->setVisible(false);
        ui->adv_button->setChecked(true);
        ui->adv_button->setEnabled(false);
        showAdvancedSettings(true);
    }
}

void AddNewTorrentDialog::setCommentText(const QString &str) const
{
    ui->commentLabel->setText(str);

    // workaround for the additional space introduced by QScrollArea
    int lineHeight = ui->commentLabel->fontMetrics().lineSpacing();
    int lines = 1 + str.count("\n");
    int height = lineHeight * lines;
    ui->scrollArea->setMaximumHeight(height);
}

void AddNewTorrentDialog::doNotDeleteTorrentClicked(bool checked)
{
    m_torrentGuard->setAutoRemove(!checked);
}

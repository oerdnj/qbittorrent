/*
 * Bittorrent Client using Qt4 and libtorrent.
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

#include "addnewtorrentdialog.h"
#include "ui_addnewtorrentdialog.h"
#include "proplistdelegate.h"
#include "torrentcontentmodel.h"
#include "torrentcontentfiltermodel.h"
#include "preferences.h"
#include "torrentpersistentdata.h"
#include "qbtsession.h"
#include "iconprovider.h"
#include "fs_utils.h"
#include "autoexpandabledialog.h"
#include "messageboxraised.h"
#include "core/unicodestrings.h"

#include <QDebug>
#include <QString>
#include <QFile>
#include <QUrl>
#include <QMenu>
#include <QFileDialog>
#include <libtorrent/version.hpp>

using namespace libtorrent;

AddNewTorrentDialog::AddNewTorrentDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AddNewTorrentDialog)
    , m_contentModel(0)
    , m_contentDelegate(0)
    , m_isMagnet(false)
    , m_hasMetadata(false)
    , m_hasRenamedFile(false)
    , m_oldIndex(0)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    ui->lblMetaLoading->setVisible(false);
    ui->progMetaLoading->setVisible(false);

    Preferences* const pref = Preferences::instance();
    ui->start_torrent_cb->setChecked(!pref->addTorrentsInPause());
    ui->save_path_combo->addItem(fsutils::toNativePath(pref->getSavePath()), pref->getSavePath());
    loadSavePathHistory();
    connect(ui->save_path_combo, SIGNAL(currentIndexChanged(int)), SLOT(onSavePathChanged(int)));
    connect(ui->browse_button, SIGNAL(clicked()), SLOT(browseButton_clicked()));
    ui->default_save_path_cb->setVisible(false); // Default path is selected by default

    // Load labels
    const QStringList customLabels = pref->getTorrentLabels();
    ui->label_combo->addItem("");
    foreach (const QString& label, customLabels)
        ui->label_combo->addItem(label);
    ui->label_combo->model()->sort(0);
    ui->content_tree->header()->setSortIndicator(0, Qt::AscendingOrder);
    loadState();
    // Signal / slots
    connect(ui->adv_button, SIGNAL(clicked(bool)), SLOT(showAdvancedSettings(bool)));
    editHotkey = new QShortcut(QKeySequence("F2"), ui->content_tree, 0, 0, Qt::WidgetShortcut);
    connect(editHotkey, SIGNAL(activated()), SLOT(renameSelectedFile()));
    connect(ui->content_tree, SIGNAL(doubleClicked(QModelIndex)), SLOT(renameSelectedFile()));

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

void AddNewTorrentDialog::loadState()
{
    const Preferences* const pref = Preferences::instance();
    m_headerState = pref->getAddNewTorrentDialogState();
    int width = pref->getAddNewTorrentDialogWidth();
    if (width >= 0) {
        QRect geo = geometry();
        geo.setWidth(width);
        setGeometry(geo);
    }
    ui->adv_button->setChecked(pref->getAddNewTorrentDialogExpanded());
}

void AddNewTorrentDialog::saveState()
{
    Preferences* const pref = Preferences::instance();
    if (m_contentModel)
        pref->setAddNewTorrentDialogState(ui->content_tree->header()->saveState());
    pref->setAddNewTorrentDialogPos(pos().y());
    pref->setAddNewTorrentDialogWidth(width());
    pref->setAddNewTorrentDialogExpanded(ui->adv_button->isChecked());
}

void AddNewTorrentDialog::showTorrent(const QString &torrent_path, const QString& from_url, QWidget *parent)
{
    AddNewTorrentDialog *dlg = new AddNewTorrentDialog(parent);
    if (dlg->loadTorrent(torrent_path, from_url))
        dlg->open();
    else
        delete dlg;
}

void AddNewTorrentDialog::showMagnet(const QString& link, QWidget *parent)
{
    AddNewTorrentDialog *dlg = new AddNewTorrentDialog(parent);
    if (dlg->loadMagnet(link))
        dlg->open();
    else
        delete dlg;
}

void AddNewTorrentDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    Preferences* const pref = Preferences::instance();
    if (!pref->additionDialogFront())
        return;
    activateWindow();
    raise();
}


void AddNewTorrentDialog::showAdvancedSettings(bool show)
{
    if (show) {
        ui->adv_button->setText(QString::fromUtf8(C_UP));
        ui->settings_group->setVisible(true);
        ui->info_group->setVisible(true);
        if (m_hasMetadata && (m_torrentInfo->num_files() > 1)) {
            ui->content_tree->setVisible(true);
            setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        }
        else {
            ui->content_tree->setVisible(false);
            setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        }
        static_cast<QVBoxLayout*>(layout())->insertWidget(layout()->indexOf(ui->never_show_cb) + 1, ui->adv_button);
    }
    else {
        ui->adv_button->setText(QString::fromUtf8(C_DOWN));
        ui->settings_group->setVisible(false);
        ui->info_group->setVisible(false);
        ui->buttonsHLayout->insertWidget(0, layout()->takeAt(layout()->indexOf(ui->never_show_cb) + 1)->widget());
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    relayout();
}

bool AddNewTorrentDialog::loadTorrent(const QString& torrent_path, const QString& from_url)
{
    m_isMagnet = false;
    m_url = from_url;
    if (torrent_path.startsWith("file://", Qt::CaseInsensitive))
        m_filePath = QUrl::fromEncoded(torrent_path.toLocal8Bit()).toLocalFile();
    else
        m_filePath = torrent_path;

    if (!QFile::exists(m_filePath)) {
        MessageBoxRaised::critical(0, tr("I/O Error"), tr("The torrent file does not exist."));
        return false;
    }

    m_hasMetadata = true;

    try {
        std::vector<char> buffer;
        lazy_entry entry;
        libtorrent::error_code ec;
        misc::loadBencodedFile(m_filePath, buffer, entry, ec);
        m_torrentInfo = new torrent_info(entry);
        m_hash = misc::toQString(m_torrentInfo->info_hash());
    }
    catch(const std::exception& e) {
        MessageBoxRaised::critical(0, tr("Invalid torrent"), tr("Failed to load the torrent: %1").arg(misc::toQStringU(e.what())));
        return false;
    }

    // Prevent showing the dialog if download is already present
    if (QBtSession::instance()->getTorrentHandle(m_hash).is_valid()) {
        MessageBoxRaised::information(0, tr("Already in download list"), tr("Torrent is already in download list. Merging trackers."), QMessageBox::Ok);
        QBtSession::instance()->addTorrent(m_filePath, false, m_url);;
        return false;
    }

    ui->lblhash->setText(m_hash);
    setupTreeview();
    return true;
}

bool AddNewTorrentDialog::loadMagnet(const QString &magnet_uri)
{
    connect(QBtSession::instance(), SIGNAL(metadataReceivedHidden(const QTorrentHandle &)), SLOT(updateMetadata(const QTorrentHandle &)));
    m_isMagnet = true;
    m_url = magnet_uri;
    m_hash = misc::magnetUriToHash(m_url);
    if (m_hash.isEmpty()) {
        MessageBoxRaised::critical(0, tr("Invalid magnet link"), tr("This magnet link was not recognized"));
        return false;
    }

    // Prevent showing the dialog if download is already present
    if (QBtSession::instance()->getTorrentHandle(m_hash).is_valid()) {
        MessageBoxRaised::information(0, tr("Already in download list"), tr("Magnet link is already in download list. Merging trackers."), QMessageBox::Ok);
        QBtSession::instance()->addMagnetUri(m_url, false);
        return false;
    }

    // Set dialog title
    QString torrent_name = misc::magnetUriToName(m_url);
    setWindowTitle(torrent_name.isEmpty() ? tr("Magnet link") : torrent_name);

    setupTreeview();
    // Set dialog position
    setdialogPosition();

    // Override save path
    TorrentTempData::setSavePath(m_hash, QString(QDir::tempPath() + "/" + m_hash));
    HiddenData::addData(m_hash);
    QBtSession::instance()->addMagnetUri(m_url, false);
    setMetadataProgressIndicator(true, tr("Retrieving metadata..."));
    ui->lblhash->setText(m_hash);

    return true;
}

void AddNewTorrentDialog::saveSavePathHistory() const
{
    QDir selected_save_path(ui->save_path_combo->itemData(ui->save_path_combo->currentIndex()).toString());
    Preferences* const pref = Preferences::instance();
    // Get current history
    QStringList history = pref->getAddNewTorrentDialogPathHistory();
    QList<QDir> history_dirs;
    foreach(const QString dir, history)
        history_dirs << QDir(dir);
    if (!history_dirs.contains(selected_save_path)) {
        // Add save path to history
        history.push_front(selected_save_path.absolutePath());
        // Limit list size
        if (history.size() > 8)
            history.pop_back();
        // Save history
        pref->setAddNewTorrentDialogPathHistory(history);
    }
}

// save_path is a folder, not an absolute file path
int AddNewTorrentDialog::indexOfSavePath(const QString &save_path)
{
    QDir saveDir(save_path);
    for(int i = 0; i<ui->save_path_combo->count() - 1; ++i)
        if (QDir(ui->save_path_combo->itemData(i).toString()) == saveDir)
            return i;
    return -1;
}

void AddNewTorrentDialog::updateFileNameInSavePaths(const QString &new_filename)
{
    for(int i = 0; i<ui->save_path_combo->count() - 1; ++i) {
        const QDir folder(ui->save_path_combo->itemData(i).toString());
        ui->save_path_combo->setItemText(i, fsutils::toNativePath(folder.absoluteFilePath(new_filename)));
    }
}

void AddNewTorrentDialog::updateDiskSpaceLabel()
{
    // Determine torrent size
    qulonglong torrent_size = 0;

    if (m_hasMetadata) {
        if (m_contentModel) {
            const std::vector<int> priorities = m_contentModel->model()->getFilesPriorities();
            Q_ASSERT(priorities.size() == (uint) m_torrentInfo->num_files());
            for (uint i = 0; i<priorities.size(); ++i)
                if (priorities[i] > 0)
                    torrent_size += m_torrentInfo->files().file_size(i);
        }
        else {
            torrent_size = m_torrentInfo->total_size();
        }
    }

    QString size_string = torrent_size ? misc::friendlyUnit(torrent_size) : QString(tr("Not Available", "This size is unavailable."));
    size_string += " (";
    size_string += tr("Free disk space: %1").arg(misc::friendlyUnit(fsutils::freeDiskSpaceOnPath(
                                                                   ui->save_path_combo->itemData(
                                                                       ui->save_path_combo->currentIndex()).toString())));
    size_string += ")";
    ui->size_lbl->setText(size_string);
}

void AddNewTorrentDialog::onSavePathChanged(int index)
{
    // Toggle default save path setting checkbox visibility
    ui->default_save_path_cb->setChecked(false);
    ui->default_save_path_cb->setVisible(QDir(ui->save_path_combo->itemData(ui->save_path_combo->currentIndex()).toString()) != QDir(Preferences::instance()->getSavePath()));
    relayout();

    // Remember index
    m_oldIndex = index;

    updateDiskSpaceLabel();
}

void AddNewTorrentDialog::browseButton_clicked()
{
    disconnect(ui->save_path_combo, SIGNAL(currentIndexChanged(int)), this, SLOT(onSavePathChanged(int)));
    // User is asking for a new save path
    QString cur_save_path = ui->save_path_combo->itemText(m_oldIndex);
    QString new_path, old_filename, new_filename;

    if (m_torrentInfo && m_torrentInfo->num_files() == 1) {
        old_filename = fsutils::fileName(cur_save_path);
        new_path = QFileDialog::getSaveFileName(this, tr("Choose save path"), cur_save_path, QString(), 0, QFileDialog::DontConfirmOverwrite);
        if (!new_path.isEmpty())
            new_path = fsutils::branchPath(new_path, &new_filename);
        qDebug() << "new_path: " << new_path;
        qDebug() << "new_filename: " << new_filename;
    }
    else {
        if (!cur_save_path.isEmpty() && QDir(cur_save_path).exists())
            new_path = QFileDialog::getExistingDirectory(this, tr("Choose save path"), cur_save_path);
        else
            new_path = QFileDialog::getExistingDirectory(this, tr("Choose save path"), QDir::homePath());
    }
    if (!new_path.isEmpty()) {
        const int existing_index = indexOfSavePath(new_path);
        if (existing_index >= 0) {
            ui->save_path_combo->setCurrentIndex(existing_index);
        }
        else {
            // New path, prepend to combo box
            if (!new_filename.isEmpty())
                ui->save_path_combo->insertItem(0, fsutils::toNativePath(QDir(new_path).absoluteFilePath(new_filename)), new_path);
            else
                ui->save_path_combo->insertItem(0, fsutils::toNativePath(new_path), new_path);
            ui->save_path_combo->setCurrentIndex(0);
        }
        // Update file name in all save_paths
        if (!new_filename.isEmpty() && !fsutils::sameFileNames(old_filename, new_filename)) {
            m_hasRenamedFile = true;
            m_filesPath[0] = new_filename;
            updateFileNameInSavePaths(new_filename);
        }

        onSavePathChanged(0);
    }
    else {
        // Restore index
        ui->save_path_combo->setCurrentIndex(m_oldIndex);
    }
    connect(ui->save_path_combo, SIGNAL(currentIndexChanged(int)), SLOT(onSavePathChanged(int)));
}

void AddNewTorrentDialog::relayout()
{
    qApp->processEvents();
    int min_width = minimumWidth();
    setMinimumWidth(width());
    adjustSize();
    setMinimumWidth(min_width);
}

void AddNewTorrentDialog::renameSelectedFile()
{
    const QModelIndexList selectedIndexes = ui->content_tree->selectionModel()->selectedRows(0);
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
        if (!fsutils::isValidFileSystemName(new_name_last)) {
            MessageBoxRaised::warning(this, tr("The file could not be renamed"),
                                      tr("This file name contains forbidden characters, please choose a different one."),
                                      QMessageBox::Ok);
            return;
        }
        if (m_contentModel->itemType(index) == TorrentContentModelItem::FileType) {
            // File renaming
            const int file_index = m_contentModel->getFileIndex(index);
            QString old_name = fsutils::fromNativePath(m_filesPath.at(file_index));
            qDebug("Old name: %s", qPrintable(old_name));
            QStringList path_items = old_name.split("/");
            path_items.removeLast();
            path_items << new_name_last;
            QString new_name = path_items.join("/");
            if (fsutils::sameFileNames(old_name, new_name)) {
                qDebug("Name did not change");
                return;
            }
            new_name = fsutils::expandPath(new_name);
            qDebug("New name: %s", qPrintable(new_name));
            // Check if that name is already used
            for (int i = 0; i<m_torrentInfo->num_files(); ++i) {
                if (i == file_index) continue;
                if (fsutils::sameFileNames(m_filesPath.at(i), new_name)) {
                    // Display error message
                    MessageBoxRaised::warning(this, tr("The file could not be renamed"),
                                              tr("This name is already in use in this folder. Please use a different name."),
                                              QMessageBox::Ok);
                    return;
                }
            }
            qDebug("Renaming %s to %s", qPrintable(old_name), qPrintable(new_name));
            // Rename file in files_path
            m_filesPath.replace(file_index, new_name);
            m_hasRenamedFile = true;
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
            if (!new_path.endsWith("/")) new_path += "/";
            // Check for overwriting
            for (int i = 0; i<m_torrentInfo->num_files(); ++i) {
                const QString &current_name = m_filesPath.at(i);
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
            for (int i = 0; i<m_torrentInfo->num_files(); ++i) {
                const QString &current_name = m_filesPath.at(i);
                if (current_name.startsWith(old_path)) {
                    QString new_name = current_name;
                    new_name.replace(0, old_path.length(), new_path);
                    new_name = fsutils::expandPath(new_name);
                    qDebug("Rename %s to %s", qPrintable(current_name), qPrintable(new_name));
                    // Rename in files_path
                    m_filesPath.replace(i, new_name);
                }
            }
            m_hasRenamedFile = true;
            // Rename folder in torrent files model too
            m_contentModel->setData(index, new_name_last);
        }
    }
}

void AddNewTorrentDialog::setdialogPosition()
{
    qApp->processEvents();
    QPoint center(misc::screenCenter(this));
    // Adjust y
    int y = Preferences::instance()->getAddNewTorrentDialogPos();
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

void AddNewTorrentDialog::loadSavePathHistory()
{
    QDir default_save_path(Preferences::instance()->getSavePath());
    // Load save path history
    QStringList raw_path_history = Preferences::instance()->getAddNewTorrentDialogPathHistory();
    foreach (const QString &sp, raw_path_history)
        if (QDir(sp) != default_save_path)
            ui->save_path_combo->addItem(fsutils::toNativePath(sp), sp);
}

void AddNewTorrentDialog::displayContentTreeMenu(const QPoint&)
{
    QMenu myFilesLlistMenu;
    const QModelIndexList selectedRows = ui->content_tree->selectionModel()->selectedRows(0);
    QAction *actRename = 0;
    if (selectedRows.size() == 1 && m_torrentInfo->num_files() > 1) {
        actRename = myFilesLlistMenu.addAction(IconProvider::instance()->getIcon("edit-rename"), tr("Rename..."));
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
            if (act == ui->actionHigh) {
                prio = prio::HIGH;
            }
            else {
                if (act == ui->actionMaximum)
                    prio = prio::MAXIMUM;
                else
                    if (act == ui->actionNot_downloaded)
                        prio = prio::IGNORED;
            }
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
    if (m_isMagnet)
        disconnect(this, SLOT(updateMetadata(const QTorrentHandle &)));

    Preferences* const pref = Preferences::instance();
    // Save Temporary data about torrent
    QString save_path = ui->save_path_combo->itemData(ui->save_path_combo->currentIndex()).toString();
    TorrentTempData::setSavePath(m_hash, save_path);
    if (ui->skip_check_cb->isChecked())
        // TODO: Check if destination actually exists
        TorrentTempData::setSeedingMode(m_hash, true);

    // Label
    const QString label = ui->label_combo->currentText();
    if (!label.isEmpty())
        TorrentTempData::setLabel(m_hash, label);

    // Save file priorities
    if (m_contentModel)
        TorrentTempData::setFilesPriority(m_hash, m_contentModel->model()->getFilesPriorities());

    // Rename files if necessary
    if (m_hasRenamedFile)
        TorrentTempData::setFilesPath(m_hash, m_filesPath);

    TorrentTempData::setAddPaused(m_hash, !ui->start_torrent_cb->isChecked());

    // Add torrent
    if (m_isMagnet)
        QBtSession::instance()->unhideMagnet(m_hash);
    else
        QBtSession::instance()->addTorrent(m_filePath, false, m_url);

    saveSavePathHistory();
    // Save settings
    pref->useAdditionDialog(!ui->never_show_cb->isChecked());
    if (ui->default_save_path_cb->isChecked()) {
        pref->setSavePath(ui->save_path_combo->itemData(ui->save_path_combo->currentIndex()).toString());
        QBtSession::instance()->setDefaultSavePath(pref->getSavePath());
    }
    QDialog::accept();
}

void AddNewTorrentDialog::reject()
{
    if (m_isMagnet) {
        disconnect(this, SLOT(updateMetadata(const QTorrentHandle &)));
        setMetadataProgressIndicator(false);
        QBtSession::instance()->deleteTorrent(m_hash, true);
    }
    QDialog::reject();
}

void AddNewTorrentDialog::updateMetadata(const QTorrentHandle &h)
{
    try {
        if (h.hash() != m_hash)
            return;

        disconnect(this, SLOT(updateMetadata(const QTorrentHandle &)));
        Q_ASSERT(h.has_metadata());

#if LIBTORRENT_VERSION_NUM < 10000
        m_torrentInfo = new torrent_info(h.get_torrent_info());
#else
        m_torrentInfo = new torrent_info(*h.torrent_file());
#endif

        // Good to go
        m_hasMetadata = true;
        setMetadataProgressIndicator(true, tr("Parsing metadata..."));

        // Update UI
        setupTreeview();
        setMetadataProgressIndicator(false, tr("Metadata retrieval complete"));
    } catch (invalid_handle&) {
        MessageBoxRaised::critical(0, tr("I/O Error"), ("Unknown error."));
        setMetadataProgressIndicator(false, tr("Unknown error"));
        return;
    }
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
        ui->comment_lbl->setText(tr("Not Available", "This comment is unavailable"));
        ui->date_lbl->setText(tr("Not Available", "This date is unavailable"));
    }
    else {
        // Set dialog title
        setWindowTitle(misc::toQStringU(m_torrentInfo->name()));

        // Set torrent information
        QString comment = misc::toQString(m_torrentInfo->comment());
        ui->comment_lbl->setText(comment.replace('\n', ' '));
        ui->date_lbl->setText(m_torrentInfo->creation_date() ? misc::toQString(*m_torrentInfo->creation_date()) : tr("Not available"));

        file_storage const& fs = m_torrentInfo->files();

        // Populate m_filesList
        for (int i = 0; i < fs.num_files(); ++i)
            m_filesPath << misc::toQStringU(fs.file_path(i));

        // Prepare content tree
        if (fs.num_files() > 1) {
            m_contentModel = new TorrentContentFilterModel(this);
            connect(m_contentModel->model(), SIGNAL(filteredFilesChanged()), SLOT(updateDiskSpaceLabel()));
            ui->content_tree->setModel(m_contentModel);
            ui->content_tree->hideColumn(PROGRESS);
            m_contentDelegate = new PropListDelegate();
            ui->content_tree->setItemDelegate(m_contentDelegate);
            connect(ui->content_tree, SIGNAL(clicked(const QModelIndex &)), ui->content_tree, SLOT(edit(const QModelIndex &)));
            connect(ui->content_tree, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(displayContentTreeMenu(const QPoint &)));

            // List files in torrent
            m_contentModel->model()->setupModelData(*m_torrentInfo);
            if (!m_headerState.isEmpty())
                ui->content_tree->header()->restoreState(m_headerState);

            // Expand root folder
            ui->content_tree->setExpanded(m_contentModel->index(0, 0), true);
        }
        else {
            // Update save paths (append file name to them)
            QString single_file_relpath = misc::toQStringU(fs.file_path(0));
            for (int i = 0; i<ui->save_path_combo->count() - 1; ++i)
                ui->save_path_combo->setItemText(i, fsutils::toNativePath(QDir(ui->save_path_combo->itemText(i)).absoluteFilePath(single_file_relpath)));
        }
    }

    updateDiskSpaceLabel();
    showAdvancedSettings(Preferences::instance()->getAddNewTorrentDialogExpanded());
    // Set dialog position
    setdialogPosition();
}

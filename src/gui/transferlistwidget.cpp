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

#include <QDebug>
#include <QShortcut>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QClipboard>
#include <QColor>
#include <QUrl>
#include <QMenu>
#include <QRegExp>
#include <QFileDialog>
#include <QMessageBox>

#include <libtorrent/version.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <vector>
#include <queue>

#include "transferlistwidget.h"
#include "qbtsession.h"
#include "torrentpersistentdata.h"
#include "transferlistdelegate.h"
#include "previewselect.h"
#include "speedlimitdlg.h"
#include "updownratiodlg.h"
#include "options_imp.h"
#include "mainwindow.h"
#include "preferences.h"
#include "torrentmodel.h"
#include "deletionconfirmationdlg.h"
#include "propertieswidget.h"
#include "iconprovider.h"
#include "fs_utils.h"
#include "autoexpandabledialog.h"
#include "transferlistsortmodel.h"

using namespace libtorrent;

TransferListWidget::TransferListWidget(QWidget *parent, MainWindow *main_window, QBtSession *_BTSession):
    QTreeView(parent), BTSession(_BTSession), main_window(main_window)
{

    setUniformRowHeights(true);
    // Load settings
    bool column_loaded = loadSettings();

    // Create and apply delegate
    listDelegate = new TransferListDelegate(this);
    setItemDelegate(listDelegate);

    // Create transfer list model
    listModel = new TorrentModel(this);

    nameFilterModel = new TransferListSortModel();
    nameFilterModel->setDynamicSortFilter(true);
    nameFilterModel->setSourceModel(listModel);
    nameFilterModel->setFilterKeyColumn(TorrentModelItem::TR_NAME);
    nameFilterModel->setFilterRole(Qt::DisplayRole);
    nameFilterModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    setModel(nameFilterModel);

    // Visual settings
    setRootIsDecorated(false);
    setAllColumnsShowFocus(true);
    setSortingEnabled(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setItemsExpandable(false);
    setAutoScroll(true);
    setDragDropMode(QAbstractItemView::DragOnly);
#if defined(Q_OS_MAC)
    setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
    header()->setStretchLastSection(false);

    // Default hidden columns
    if (!column_loaded) {
        setColumnHidden(TorrentModelItem::TR_ADD_DATE, true);
        setColumnHidden(TorrentModelItem::TR_SEED_DATE, true);
        setColumnHidden(TorrentModelItem::TR_UPLIMIT, true);
        setColumnHidden(TorrentModelItem::TR_DLLIMIT, true);
        setColumnHidden(TorrentModelItem::TR_TRACKER, true);
        setColumnHidden(TorrentModelItem::TR_AMOUNT_DOWNLOADED, true);
        setColumnHidden(TorrentModelItem::TR_AMOUNT_UPLOADED, true);
        setColumnHidden(TorrentModelItem::TR_AMOUNT_DOWNLOADED_SESSION, true);
        setColumnHidden(TorrentModelItem::TR_AMOUNT_UPLOADED_SESSION, true);
        setColumnHidden(TorrentModelItem::TR_AMOUNT_LEFT, true);
        setColumnHidden(TorrentModelItem::TR_TIME_ELAPSED, true);
        setColumnHidden(TorrentModelItem::TR_SAVE_PATH, true);
        setColumnHidden(TorrentModelItem::TR_COMPLETED, true);
        setColumnHidden(TorrentModelItem::TR_RATIO_LIMIT, true);
        setColumnHidden(TorrentModelItem::TR_SEEN_COMPLETE_DATE, true);
        setColumnHidden(TorrentModelItem::TR_LAST_ACTIVITY, true);
        setColumnHidden(TorrentModelItem::TR_TOTAL_SIZE, true);
    }

    //Ensure that at least one column is visible at all times
    bool atLeastOne = false;
    for (unsigned int i = 0; i<TorrentModelItem::NB_COLUMNS; i++) {
        if (!isColumnHidden(i)) {
            atLeastOne = true;
            break;
        }
    }
    if (!atLeastOne)
        setColumnHidden(TorrentModelItem::TR_NAME, false);

    //When adding/removing columns between versions some may
    //end up being size 0 when the new version is launched with
    //a conf file from the previous version.
    for (unsigned int i = 0; i<TorrentModelItem::NB_COLUMNS; i++)
        if (!columnWidth(i))
            resizeColumnToContents(i);

    setContextMenuPolicy(Qt::CustomContextMenu);

    // Listen for list events
    connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(torrentDoubleClicked(QModelIndex)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(displayListMenu(const QPoint &)));
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(displayDLHoSMenu(const QPoint &)));
    connect(header(), SIGNAL(sectionMoved(int, int, int)), this, SLOT(saveSettings()));
    connect(header(), SIGNAL(sectionResized(int, int, int)), this, SLOT(saveSettings()));
    connect(header(), SIGNAL(sortIndicatorChanged(int, Qt::SortOrder)), this, SLOT(saveSettings()));

    editHotkey = new QShortcut(QKeySequence("F2"), this, SLOT(renameSelectedTorrent()), 0, Qt::WidgetShortcut);
    deleteHotkey = new QShortcut(QKeySequence::Delete, this, SLOT(deleteSelectedTorrents()), 0, Qt::WidgetShortcut);
}

TransferListWidget::~TransferListWidget()
{
    qDebug() << Q_FUNC_INFO << "ENTER";
    // Save settings
    saveSettings();
    // Clean up
    delete nameFilterModel;
    delete listModel;
    delete listDelegate;
    delete editHotkey;
    delete deleteHotkey;
    qDebug() << Q_FUNC_INFO << "EXIT";
}

TorrentModel* TransferListWidget::getSourceModel() const
{
    return listModel;
}

void TransferListWidget::previewFile(QString filePath)
{
    misc::openPath(filePath);
}

void TransferListWidget::setRefreshInterval(int t)
{
    qDebug("Settings transfer list refresh interval to %dms", t);
    listModel->setRefreshInterval(t);
}

int TransferListWidget::getRowFromHash(QString hash) const
{
    return listModel->torrentRow(hash);
}

inline QString TransferListWidget::getHashFromRow(int row) const
{
    return listModel->torrentHash(row);
}

inline QModelIndex TransferListWidget::mapToSource(const QModelIndex &index) const
{
    Q_ASSERT(index.isValid());
    if (index.model() == nameFilterModel)
        return nameFilterModel->mapToSource(index);
    return index;
}

inline QModelIndex TransferListWidget::mapFromSource(const QModelIndex &index) const
{
    Q_ASSERT(index.isValid());
    Q_ASSERT(index.model() == nameFilterModel);
    return nameFilterModel->mapFromSource(index);
}

void TransferListWidget::torrentDoubleClicked(const QModelIndex& index)
{
    const int row = mapToSource(index).row();
    const QString hash = getHashFromRow(row);
    QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if (!h.is_valid()) return;
    int action;
    if (h.is_seed())
        action = Preferences::instance()->getActionOnDblClOnTorrentFn();
    else
        action = Preferences::instance()->getActionOnDblClOnTorrentDl();

    switch(action) {
    case TOGGLE_PAUSE:
        if (h.is_paused())
            h.resume();
        else
            h.pause();
        break;
    case OPEN_DEST:
        if (h.num_files() == 1)
            misc::openFolderSelect(QDir(h.root_path()).absoluteFilePath(h.filepath_at(0)));
        else
            misc::openPath(h.root_path());
        break;
    }
}

QStringList TransferListWidget::getSelectedTorrentsHashes() const
{
    QStringList hashes;
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    foreach (const QModelIndex &index, selectedIndexes)
        hashes << getHashFromRow(mapToSource(index).row());
    return hashes;
}

void TransferListWidget::setSelectedTorrentsLocation()
{
    const QStringList hashes = getSelectedTorrentsHashes();
    if (hashes.isEmpty()) return;
    QString dir;
    const QDir saveDir(TorrentPersistentData::instance()->getSavePath(hashes.first()));
    qDebug("Old save path is %s", qPrintable(saveDir.absolutePath()));
    dir = QFileDialog::getExistingDirectory(this, tr("Choose save path"), saveDir.absolutePath(),
                                            QFileDialog::DontConfirmOverwrite | QFileDialog::ShowDirsOnly | QFileDialog::HideNameFilterDetails);
    if (!dir.isNull()) {
        qDebug("New path is %s", qPrintable(dir));
        // Check if savePath exists
        QDir savePath(fsutils::expandPathAbs(dir));
        qDebug("New path after clean up is %s", qPrintable(savePath.absolutePath()));
        foreach (const QString & hash, hashes) {
            // Actually move storage
            QTorrentHandle h = BTSession->getTorrentHandle(hash);
            if (!BTSession->useTemporaryFolder() || h.is_seed()) {
                if (!savePath.exists()) savePath.mkpath(savePath.absolutePath());
                h.move_storage(savePath.absolutePath());
            }
            else {
                TorrentPersistentData::instance()->saveSavePath(h.hash(), savePath.absolutePath());
                main_window->getProperties()->updateSavePath(h);
            }
        }
    }
}

void TransferListWidget::startSelectedTorrents()
{
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes)
        BTSession->resumeTorrent(hash);
}

void TransferListWidget::forceStartSelectedTorrents()
{
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes)
        BTSession->resumeTorrent(hash, true);
}

void TransferListWidget::startVisibleTorrents()
{
    QStringList hashes;
    for (int i = 0; i<nameFilterModel->rowCount(); ++i) {
        const int row = mapToSource(nameFilterModel->index(i, 0)).row();
        hashes << getHashFromRow(row);
    }
    foreach (const QString &hash, hashes)
        BTSession->resumeTorrent(hash);
}

void TransferListWidget::pauseSelectedTorrents()
{
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes)
        BTSession->pauseTorrent(hash);
}

void TransferListWidget::pauseVisibleTorrents()
{
    QStringList hashes;
    for (int i = 0; i<nameFilterModel->rowCount(); ++i) {
        const int row = mapToSource(nameFilterModel->index(i, 0)).row();
        hashes << getHashFromRow(row);
    }
    foreach (const QString &hash, hashes)
        BTSession->pauseTorrent(hash);
}

void TransferListWidget::deleteSelectedTorrents()
{
    if (main_window->getCurrentTabWidget() != this) return;
    const QStringList& hashes = getSelectedTorrentsHashes();
    if (hashes.empty()) return;
    QTorrentHandle torrent = BTSession->getTorrentHandle(hashes[0]);
    bool delete_local_files = false;
    if (Preferences::instance()->confirmTorrentDeletion() &&
        !DeletionConfirmationDlg::askForDeletionConfirmation(delete_local_files, hashes.size(), torrent.name()))
        return;
    foreach (const QString &hash, hashes)
        BTSession->deleteTorrent(hash, delete_local_files);
}

void TransferListWidget::deleteVisibleTorrents()
{
    if (nameFilterModel->rowCount() <= 0) return;
    QStringList hashes;
    for (int i = 0; i<nameFilterModel->rowCount(); ++i) {
        const int row = mapToSource(nameFilterModel->index(i, 0)).row();
        hashes << getHashFromRow(row);
    }
    QTorrentHandle torrent = BTSession->getTorrentHandle(hashes[0]);
    bool delete_local_files = false;
    if (Preferences::instance()->confirmTorrentDeletion() &&
        !DeletionConfirmationDlg::askForDeletionConfirmation(delete_local_files, hashes.size(), torrent.name()))
        return;
    foreach (const QString &hash, hashes)
        BTSession->deleteTorrent(hash, delete_local_files);
}

void TransferListWidget::increasePrioSelectedTorrents()
{
    qDebug() << Q_FUNC_INFO;
    if (main_window->getCurrentTabWidget() != this) return;
    const QStringList hashes = getSelectedTorrentsHashes();
    std::priority_queue<QPair<int, QTorrentHandle>, std::vector<QPair<int, QTorrentHandle> >, std::greater<QPair<int, QTorrentHandle> > > torrent_queue;
    // Sort torrents by priority
    foreach (const QString &hash, hashes) {
        try {
            QTorrentHandle h = BTSession->getTorrentHandle(hash);
            if (!h.is_seed())
                torrent_queue.push(qMakePair(h.queue_position(), h));
        }catch(invalid_handle&) {}
    }
    // Increase torrents priority (starting with the ones with highest priority)
    while(!torrent_queue.empty()) {
        QTorrentHandle h = torrent_queue.top().second;
        try {
            h.queue_position_up();
        } catch(invalid_handle& h) {}
        torrent_queue.pop();
    }
}

void TransferListWidget::decreasePrioSelectedTorrents()
{
    qDebug() << Q_FUNC_INFO;
    if (main_window->getCurrentTabWidget() != this) return;
    const QStringList hashes = getSelectedTorrentsHashes();
    std::priority_queue<QPair<int, QTorrentHandle>, std::vector<QPair<int, QTorrentHandle> >, std::less<QPair<int, QTorrentHandle> > > torrent_queue;
    // Sort torrents by priority
    foreach (const QString &hash, hashes) {
        try {
            QTorrentHandle h = BTSession->getTorrentHandle(hash);
            if (!h.is_seed())
                torrent_queue.push(qMakePair(h.queue_position(), h));
        }catch(invalid_handle&) {}
    }
    // Decrease torrents priority (starting with the ones with lowest priority)
    while(!torrent_queue.empty()) {
        QTorrentHandle h = torrent_queue.top().second;
        try {
            h.queue_position_down();
        } catch(invalid_handle& h) {}
        torrent_queue.pop();
    }
}

void TransferListWidget::topPrioSelectedTorrents()
{
    if (main_window->getCurrentTabWidget() != this) return;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid() && !h.is_seed())
            h.queue_position_top();
    }
}

void TransferListWidget::bottomPrioSelectedTorrents()
{
    if (main_window->getCurrentTabWidget() != this) return;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid() && !h.is_seed())
            h.queue_position_bottom();
    }
}

void TransferListWidget::copySelectedMagnetURIs() const
{
    QStringList magnet_uris;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        const QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid())
            magnet_uris << misc::toQString(make_magnet_uri(h));
    }
    qApp->clipboard()->setText(magnet_uris.join("\n"));
}

void TransferListWidget::copySelectedNames() const
{
    QStringList torrent_names;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        const QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid())
            torrent_names << h.name();
    }
    qApp->clipboard()->setText(torrent_names.join("\n"));
}

void TransferListWidget::hidePriorityColumn(bool hide)
{
    qDebug("hidePriorityColumn(%d)", hide);
    setColumnHidden(TorrentModelItem::TR_PRIORITY, hide);
    if (!hide && !columnWidth(TorrentModelItem::TR_PRIORITY))
        resizeColumnToContents(TorrentModelItem::TR_PRIORITY);
}

void TransferListWidget::openSelectedTorrentsFolder() const
{
    QSet<QString> pathsList;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        const QTorrentHandle h = BTSession->getTorrentHandle(hash);
        QString path;
        if (h.num_files() == 1) {
            path = QDir(h.root_path()).absoluteFilePath(h.filepath_at(0));
            if (!pathsList.contains(path))
                misc::openFolderSelect(path);
        }
        else {
            path = h.root_path();
            if (!pathsList.contains(path))
                misc::openPath(path);
        }
        pathsList.insert(path);
    }
}

void TransferListWidget::previewSelectedTorrents()
{
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        const QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid() && h.has_metadata())
            new PreviewSelect(this, h);
    }
}

void TransferListWidget::setDlLimitSelectedTorrents()
{
    QList<QTorrentHandle> selected_torrents;
    bool first = true;
    bool all_same_limit = true;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        const QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid() && !h.is_seed()) {
            selected_torrents << h;
            // Determine current limit for selected torrents
            if (first)
                first = false;
            else
                if (all_same_limit && h.download_limit() != selected_torrents.first().download_limit())
                    all_same_limit = false;
        }
    }
    if (selected_torrents.empty()) return;

    bool ok = false;
    int default_limit = -1;
    if (all_same_limit)
        default_limit = selected_torrents.first().download_limit();
    const long new_limit = SpeedLimitDialog::askSpeedLimit(&ok, tr("Torrent Download Speed Limiting"), default_limit, Preferences::instance()->getGlobalDownloadLimit() * 1024.);
    if (ok) {
        foreach (const QTorrentHandle &h, selected_torrents) {
            qDebug("Applying download speed limit of %ld Kb/s to torrent %s", (long)(new_limit / 1024.), qPrintable(h.hash()));
            BTSession->setDownloadLimit(h.hash(), new_limit);
        }
    }
}

void TransferListWidget::setUpLimitSelectedTorrents()
{
    QList<QTorrentHandle> selected_torrents;
    bool first = true;
    bool all_same_limit = true;
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        const QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid()) {
            selected_torrents << h;
            // Determine current limit for selected torrents
            if (first)
                first = false;
            else
                if (all_same_limit && h.upload_limit() != selected_torrents.first().upload_limit())
                    all_same_limit = false;
        }
    }
    if (selected_torrents.empty()) return;

    bool ok = false;
    int default_limit = -1;
    if (all_same_limit)
        default_limit = selected_torrents.first().upload_limit();
    const long new_limit = SpeedLimitDialog::askSpeedLimit(&ok, tr("Torrent Upload Speed Limiting"), default_limit, Preferences::instance()->getGlobalUploadLimit() * 1024.);
    if (ok) {
        foreach (const QTorrentHandle &h, selected_torrents) {
            qDebug("Applying upload speed limit of %ld Kb/s to torrent %s", (long)(new_limit / 1024.), qPrintable(h.hash()));
            BTSession->setUploadLimit(h.hash(), new_limit);
        }
    }
}

void TransferListWidget::setMaxRatioSelectedTorrents()
{
    const QStringList hashes = getSelectedTorrentsHashes();
    if (hashes.isEmpty())
        return;
    bool useGlobalValue;
    qreal currentMaxRatio;
    if (hashes.count() == 1) {
        currentMaxRatio = BTSession->getMaxRatioPerTorrent(hashes.first(), &useGlobalValue);
    }
    else {
        useGlobalValue = true;
        currentMaxRatio = BTSession->getGlobalMaxRatio();
    }
    UpDownRatioDlg dlg(useGlobalValue, currentMaxRatio, QBtSession::MAX_RATIO, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    foreach (const QString &hash, hashes) {
        if (dlg.useDefault())
            BTSession->removeRatioPerTorrent(hash);
        else
            BTSession->setMaxRatioPerTorrent(hash, dlg.ratio());
    }
}

void TransferListWidget::recheckSelectedTorrents()
{
    if (Preferences::instance()->confirmTorrentRecheck()) {
        QMessageBox::StandardButton ret = QMessageBox::question(this, tr("Recheck confirmation"), tr("Are you sure you want to recheck the selected torrent(s)?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes) return;
    }
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes)
        BTSession->recheckTorrent(hash);
}

// hide/show columns menu
void TransferListWidget::displayDLHoSMenu(const QPoint&)
{
    QMenu hideshowColumn(this);
    hideshowColumn.setTitle(tr("Column visibility"));
    QList<QAction*> actions;
    for (int i = 0; i < listModel->columnCount(); ++i) {
        if (!BTSession->isQueueingEnabled() && i == TorrentModelItem::TR_PRIORITY) {
            actions.append(0);
            continue;
        }
        QAction *myAct = hideshowColumn.addAction(listModel->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString());
        myAct->setCheckable(true);
        myAct->setChecked(!isColumnHidden(i));
        actions.append(myAct);
    }
    int visibleCols = 0;
    for (unsigned int i = 0; i<TorrentModelItem::NB_COLUMNS; i++) {
        if (!isColumnHidden(i))
            visibleCols++;

        if (visibleCols > 1)
            break;
    }

    // Call menu
    QAction *act = hideshowColumn.exec(QCursor::pos());
    if (act) {
        int col = actions.indexOf(act);
        Q_ASSERT(col >= 0);
        Q_ASSERT(visibleCols > 0);
        if (!isColumnHidden(col) && visibleCols == 1)
            return;
        qDebug("Toggling column %d visibility", col);
        setColumnHidden(col, !isColumnHidden(col));
        if (!isColumnHidden(col) && columnWidth(col) <= 5)
            setColumnWidth(col, 100);
        saveSettings();
    }
}

void TransferListWidget::toggleSelectedTorrentsSuperSeeding() const
{
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        QTorrentHandle h = BTSession->getTorrentHandle(hash);
        if (h.is_valid() && h.has_metadata())
            h.super_seeding(!h.status(0).super_seeding);
    }
}

void TransferListWidget::toggleSelectedTorrentsSequentialDownload() const
{
    const QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        QTorrentHandle h = BTSession->getTorrentHandle(hash);
        h.toggleSequentialDownload();
    }
}

void TransferListWidget::toggleSelectedFirstLastPiecePrio() const
{
    QStringList hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        QTorrentHandle h = BTSession->getTorrentHandle(hash);
        h.toggleFirstLastPiecePrio();
    }
}

void TransferListWidget::askNewLabelForSelection()
{
    // Ask for label
    bool ok;
    bool invalid;
    do {
        invalid = false;
        const QString label = AutoExpandableDialog::getText(this, tr("New Label"), tr("Label:"), QLineEdit::Normal, "", &ok).trimmed();
        if (ok && !label.isEmpty()) {
            if (fsutils::isValidFileSystemName(label)) {
                setSelectionLabel(label);
            }
            else {
                QMessageBox::warning(this, tr("Invalid label name"), tr("Please don't use any special characters in the label name."));
                invalid = true;
            }
        }
    } while(invalid);
}

void TransferListWidget::renameSelectedTorrent()
{
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    if (selectedIndexes.size() != 1) return;
    if (!selectedIndexes.first().isValid()) return;
    QModelIndex mi = listModel->index(mapToSource(selectedIndexes.first()).row(), TorrentModelItem::TR_NAME);
    const QString hash = getHashFromRow(mi.row());
    const QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if (!h.is_valid()) return;
    // Ask for a new Name
    bool ok;
    QString name = AutoExpandableDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, h.name(), &ok);
    if (ok && !name.isEmpty()) {
        name.replace(QRegExp("\r?\n|\r"), " ");
        // Rename the torrent
        listModel->setData(mi, name, Qt::DisplayRole);
    }
}

void TransferListWidget::setSelectionLabel(QString label)
{
    const QStringList& hashes = getSelectedTorrentsHashes();
    foreach (const QString &hash, hashes) {
        Q_ASSERT(!hash.isEmpty());
        const int row = getRowFromHash(hash);
        const QString old_label = listModel->data(listModel->index(row, TorrentModelItem::TR_LABEL)).toString();
        listModel->setData(listModel->index(row, TorrentModelItem::TR_LABEL), QVariant(label), Qt::DisplayRole);
        // Update save path if necessary
        QTorrentHandle h = BTSession->getTorrentHandle(hash);
        BTSession->changeLabelInTorrentSavePath(h, old_label, label);
    }
}

void TransferListWidget::removeLabelFromRows(QString label)
{
    for (int i = 0; i<listModel->rowCount(); ++i) {
        if (listModel->data(listModel->index(i, TorrentModelItem::TR_LABEL)) == label) {
            const QString hash = getHashFromRow(i);
            listModel->setData(listModel->index(i, TorrentModelItem::TR_LABEL), "", Qt::DisplayRole);
            // Update save path if necessary
            QTorrentHandle h = BTSession->getTorrentHandle(hash);
            BTSession->changeLabelInTorrentSavePath(h, label, "");
        }
    }
}

void TransferListWidget::displayListMenu(const QPoint&)
{
    QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    if (selectedIndexes.size() == 0)
        return;
    // Create actions
    QAction actionStart(IconProvider::instance()->getIcon("media-playback-start"), tr("Resume", "Resume/start the torrent"), 0);
    connect(&actionStart, SIGNAL(triggered()), this, SLOT(startSelectedTorrents()));
    QAction actionPause(IconProvider::instance()->getIcon("media-playback-pause"), tr("Pause", "Pause the torrent"), 0);
    connect(&actionPause, SIGNAL(triggered()), this, SLOT(pauseSelectedTorrents()));
    QAction actionForceStart(IconProvider::instance()->getIcon("media-seek-forward"), tr("Force Resume", "Force Resume/start the torrent"), 0);
    connect(&actionForceStart, SIGNAL(triggered()), this, SLOT(forceStartSelectedTorrents()));
    QAction actionDelete(IconProvider::instance()->getIcon("edit-delete"), tr("Delete", "Delete the torrent"), 0);
    connect(&actionDelete, SIGNAL(triggered()), this, SLOT(deleteSelectedTorrents()));
    QAction actionPreview_file(IconProvider::instance()->getIcon("view-preview"), tr("Preview file..."), 0);
    connect(&actionPreview_file, SIGNAL(triggered()), this, SLOT(previewSelectedTorrents()));
    QAction actionSet_max_ratio(QIcon(QString::fromUtf8(":/icons/skin/ratio.png")), tr("Limit share ratio..."), 0);
    connect(&actionSet_max_ratio, SIGNAL(triggered()), this, SLOT(setMaxRatioSelectedTorrents()));
    QAction actionSet_upload_limit(QIcon(QString::fromUtf8(":/icons/skin/seeding.png")), tr("Limit upload rate..."), 0);
    connect(&actionSet_upload_limit, SIGNAL(triggered()), this, SLOT(setUpLimitSelectedTorrents()));
    QAction actionSet_download_limit(QIcon(QString::fromUtf8(":/icons/skin/download.png")), tr("Limit download rate..."), 0);
    connect(&actionSet_download_limit, SIGNAL(triggered()), this, SLOT(setDlLimitSelectedTorrents()));
    QAction actionOpen_destination_folder(IconProvider::instance()->getIcon("inode-directory"), tr("Open destination folder"), 0);
    connect(&actionOpen_destination_folder, SIGNAL(triggered()), this, SLOT(openSelectedTorrentsFolder()));
    QAction actionIncreasePriority(IconProvider::instance()->getIcon("go-up"), tr("Move up", "i.e. move up in the queue"), 0);
    connect(&actionIncreasePriority, SIGNAL(triggered()), this, SLOT(increasePrioSelectedTorrents()));
    QAction actionDecreasePriority(IconProvider::instance()->getIcon("go-down"), tr("Move down", "i.e. Move down in the queue"), 0);
    connect(&actionDecreasePriority, SIGNAL(triggered()), this, SLOT(decreasePrioSelectedTorrents()));
    QAction actionTopPriority(IconProvider::instance()->getIcon("go-top"), tr("Move to top", "i.e. Move to top of the queue"), 0);
    connect(&actionTopPriority, SIGNAL(triggered()), this, SLOT(topPrioSelectedTorrents()));
    QAction actionBottomPriority(IconProvider::instance()->getIcon("go-bottom"), tr("Move to bottom", "i.e. Move to bottom of the queue"), 0);
    connect(&actionBottomPriority, SIGNAL(triggered()), this, SLOT(bottomPrioSelectedTorrents()));
    QAction actionSetTorrentPath(IconProvider::instance()->getIcon("inode-directory"), tr("Set location..."), 0);
    connect(&actionSetTorrentPath, SIGNAL(triggered()), this, SLOT(setSelectedTorrentsLocation()));
    QAction actionForce_recheck(IconProvider::instance()->getIcon("document-edit-verify"), tr("Force recheck"), 0);
    connect(&actionForce_recheck, SIGNAL(triggered()), this, SLOT(recheckSelectedTorrents()));
    QAction actionCopy_magnet_link(QIcon(":/icons/magnet.png"), tr("Copy magnet link"), 0);
    connect(&actionCopy_magnet_link, SIGNAL(triggered()), this, SLOT(copySelectedMagnetURIs()));
    QAction actionCopy_name(IconProvider::instance()->getIcon("edit-copy"), tr("Copy name"), 0);
    connect(&actionCopy_name, SIGNAL(triggered()), this, SLOT(copySelectedNames()));
    QAction actionSuper_seeding_mode(tr("Super seeding mode"), 0);
    actionSuper_seeding_mode.setCheckable(true);
    connect(&actionSuper_seeding_mode, SIGNAL(triggered()), this, SLOT(toggleSelectedTorrentsSuperSeeding()));
    QAction actionRename(IconProvider::instance()->getIcon("edit-rename"), tr("Rename..."), 0);
    connect(&actionRename, SIGNAL(triggered()), this, SLOT(renameSelectedTorrent()));
    QAction actionSequential_download(tr("Download in sequential order"), 0);
    actionSequential_download.setCheckable(true);
    connect(&actionSequential_download, SIGNAL(triggered()), this, SLOT(toggleSelectedTorrentsSequentialDownload()));
    QAction actionFirstLastPiece_prio(tr("Download first and last piece first"), 0);
    actionFirstLastPiece_prio.setCheckable(true);
    connect(&actionFirstLastPiece_prio, SIGNAL(triggered()), this, SLOT(toggleSelectedFirstLastPiecePrio()));
    // End of actions

    // Enable/disable pause/start action given the DL state
    bool needs_pause = false, needs_start = false, needs_force = false, needs_preview = false;
    bool all_same_super_seeding = true;
    bool super_seeding_mode = false;
    bool all_same_sequential_download_mode = true, all_same_prio_firstlast = true;
    bool sequential_download_mode = false, prioritize_first_last = false;
    bool one_has_metadata = false, one_not_seed = false;
    bool all_same_label = true;
    QString first_label;
    bool first = true;
    QTorrentHandle h;
    qDebug("Displaying menu");
    foreach (const QModelIndex &index, selectedIndexes) {
        // Get the file name
        QString hash = getHashFromRow(mapToSource(index).row());
        // Get handle and pause the torrent
        h = BTSession->getTorrentHandle(hash);
        if (!h.is_valid()) continue;

        if (first_label.isEmpty() && first)
            first_label = listModel->data(listModel->index(mapToSource(index).row(), TorrentModelItem::TR_LABEL)).toString();

        all_same_label = (first_label == (listModel->data(listModel->index(mapToSource(index).row(), TorrentModelItem::TR_LABEL)).toString()));

        if (h.has_metadata())
            one_has_metadata = true;
        if (!h.is_seed()) {
            one_not_seed = true;
            if (h.has_metadata()) {
                if (first) {
                    sequential_download_mode = h.is_sequential_download();
                    prioritize_first_last = h.first_last_piece_first();
                }
                else {
                    if (sequential_download_mode != h.is_sequential_download())
                        all_same_sequential_download_mode = false;
                    if (prioritize_first_last != h.first_last_piece_first())
                        all_same_prio_firstlast = false;
                }
            }
        }
        else {
            if (!one_not_seed && all_same_super_seeding && h.has_metadata()) {
                if (first) {
                    super_seeding_mode = h.status(0).super_seeding;
                }
                else if (super_seeding_mode != h.status(0).super_seeding)
                    all_same_super_seeding = false;

            }
        }
        if (!h.is_forced())
            needs_force = true;
        else
            needs_start = true;
        if (h.is_paused())
            needs_start = true;
        else
            needs_pause = true;
        if (h.has_metadata())
            needs_preview = true;

        first = false;

        if (one_has_metadata && one_not_seed && !all_same_sequential_download_mode
            && !all_same_prio_firstlast && !all_same_super_seeding && !all_same_label
            && needs_start && needs_force && needs_pause && needs_preview) {
            break;
        }
    }
    QMenu listMenu(this);
    if (needs_start)
        listMenu.addAction(&actionStart);
    if (needs_pause)
        listMenu.addAction(&actionPause);
    if (needs_force)
        listMenu.addAction(&actionForceStart);
    listMenu.addSeparator();
    listMenu.addAction(&actionDelete);
    listMenu.addSeparator();
    listMenu.addAction(&actionSetTorrentPath);
    if (selectedIndexes.size() == 1)
        listMenu.addAction(&actionRename);
    // Label Menu
    QStringList customLabels = Preferences::instance()->getTorrentLabels();
    customLabels.sort();
    QList<QAction*> labelActions;
    QMenu *labelMenu = listMenu.addMenu(IconProvider::instance()->getIcon("view-categories"), tr("Label"));
    labelActions << labelMenu->addAction(IconProvider::instance()->getIcon("list-add"), tr("New...", "New label..."));
    labelActions << labelMenu->addAction(IconProvider::instance()->getIcon("edit-clear"), tr("Reset", "Reset label"));
    labelMenu->addSeparator();
    foreach (QString label, customLabels) {
        label.replace('&', "&&");  // avoid '&' becomes accelerator key
        QAction *lb = new QAction(IconProvider::instance()->getIcon("inode-directory"), label, labelMenu);
        if (all_same_label && (label == first_label)) {
            lb->setCheckable(true);
            lb->setChecked(true);
        }
        labelMenu->addAction(lb);
        labelActions << lb;
    }
    listMenu.addSeparator();
    if (one_not_seed)
        listMenu.addAction(&actionSet_download_limit);
    listMenu.addAction(&actionSet_max_ratio);
    listMenu.addAction(&actionSet_upload_limit);
    if (!one_not_seed && all_same_super_seeding && one_has_metadata) {
        actionSuper_seeding_mode.setChecked(super_seeding_mode);
        listMenu.addAction(&actionSuper_seeding_mode);
    }
    listMenu.addSeparator();
    bool added_preview_action = false;
    if (needs_preview) {
        listMenu.addAction(&actionPreview_file);
        added_preview_action = true;
    }
    if (one_not_seed && one_has_metadata) {
        if (all_same_sequential_download_mode) {
            actionSequential_download.setChecked(sequential_download_mode);
            listMenu.addAction(&actionSequential_download);
            added_preview_action = true;
        }
        if (all_same_prio_firstlast) {
            actionFirstLastPiece_prio.setChecked(prioritize_first_last);
            listMenu.addAction(&actionFirstLastPiece_prio);
            added_preview_action = true;
        }
    }
    if (added_preview_action)
        listMenu.addSeparator();
    if (one_has_metadata) {
        listMenu.addAction(&actionForce_recheck);
        listMenu.addSeparator();
    }
    listMenu.addAction(&actionOpen_destination_folder);
    if (BTSession->isQueueingEnabled() && one_not_seed) {
        listMenu.addSeparator();
        QMenu *prioMenu = listMenu.addMenu(tr("Priority"));
        prioMenu->addAction(&actionTopPriority);
        prioMenu->addAction(&actionIncreasePriority);
        prioMenu->addAction(&actionDecreasePriority);
        prioMenu->addAction(&actionBottomPriority);
    }
    listMenu.addSeparator();
    listMenu.addAction(&actionCopy_name);
    listMenu.addAction(&actionCopy_magnet_link);
    // Call menu
    QAction *act = 0;
    act = listMenu.exec(QCursor::pos());
    if (act) {
        // Parse label actions only (others have slots assigned)
        int i = labelActions.indexOf(act);
        if (i >= 0) {
            // Label action
            if (i == 0) {
                // New Label
                askNewLabelForSelection();
            }
            else {
                QString label = "";
                if (i > 1)
                    label = customLabels.at(i - 2);
                // Update Label
                setSelectionLabel(label);
            }
        }
    }
}

void TransferListWidget::currentChanged(const QModelIndex& current, const QModelIndex&)
{
    qDebug("CURRENT CHANGED");
    QTorrentHandle h;
    if (current.isValid()) {
        const int row = mapToSource(current).row();
        h = BTSession->getTorrentHandle(getHashFromRow(row));
        // Scroll Fix
        scrollTo(current);
    }
    emit currentTorrentChanged(h);
}

void TransferListWidget::applyLabelFilterAll()
{
    nameFilterModel->disableLabelFilter();
}

void TransferListWidget::applyLabelFilter(QString label)
{
    qDebug("Applying Label filter: %s", qPrintable(label));
    nameFilterModel->setLabelFilter(label);
}

void TransferListWidget::applyTrackerFilterAll()
{
    nameFilterModel->disableTrackerFilter();
}

void TransferListWidget::applyTrackerFilter(const QStringList &hashes)
{
    nameFilterModel->setTrackerFilter(hashes);
}

void TransferListWidget::applyNameFilter(const QString& name)
{
    nameFilterModel->setFilterRegExp(QRegExp(QRegExp::escape(name), Qt::CaseInsensitive));
}

void TransferListWidget::applyStatusFilter(int f)
{
    nameFilterModel->setStatusFilter((TorrentFilter::TorrentFilter)f);
    // Select first item if nothing is selected
    if (selectionModel()->selectedRows(0).empty() && nameFilterModel->rowCount() > 0) {
        qDebug("Nothing is selected, selecting first row: %s", qPrintable(nameFilterModel->index(0, TorrentModelItem::TR_NAME).data().toString()));
        selectionModel()->setCurrentIndex(nameFilterModel->index(0, TorrentModelItem::TR_NAME), QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    }
}

void TransferListWidget::saveSettings()
{
    Preferences::instance()->setTransHeaderState(header()->saveState());
}

bool TransferListWidget::loadSettings()
{
    bool ok = header()->restoreState(Preferences::instance()->getTransHeaderState());
    if (!ok)
        header()->resizeSection(0, 200); // Default
    return ok;
}


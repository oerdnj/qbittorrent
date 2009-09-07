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
#include "downloadingTorrents.h"
#include "misc.h"
#include "properties_imp.h"
#include "bittorrent.h"
#include "allocationDlg.h"
#include "DLListDelegate.h"
#include "GUI.h"

#include <QFile>
#include <QSettings>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include <QTime>
#include <QMenu>

DownloadingTorrents::DownloadingTorrents(QObject *parent, bittorrent *BTSession) : parent(parent), BTSession(BTSession), nbTorrents(0) {
  setupUi(this);
  // Setting icons
  actionStart->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/play.png")));
  actionPause->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/pause.png")));
  actionDelete->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/delete.png")));
  actionPreview_file->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/preview.png")));
  actionSet_upload_limit->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/seeding.png")));
  actionSet_download_limit->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png")));
  actionDelete_Permanently->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/delete_perm.png")));
  actionTorrent_Properties->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/properties.png")));
  actionCopy_magnet_link->setIcon(QIcon(QString::fromUtf8(":/Icons/magnet.png")));
  //   tabBottom->setTabIcon(0, QIcon(QString::fromUtf8(":/Icons/oxygen/log.png")));
  //   tabBottom->setTabIcon(1, QIcon(QString::fromUtf8(":/Icons/oxygen/filter.png")));

  // Set Download list model
  DLListModel = new QStandardItemModel(0,10);
  DLListModel->setHeaderData(NAME, Qt::Horizontal, tr("Name", "i.e: file name"));
  DLListModel->setHeaderData(SIZE, Qt::Horizontal, tr("Size", "i.e: file size"));
  DLListModel->setHeaderData(PROGRESS, Qt::Horizontal, tr("Progress", "i.e: % downloaded"));
  DLListModel->setHeaderData(DLSPEED, Qt::Horizontal, tr("DL Speed", "i.e: Download speed"));
  DLListModel->setHeaderData(UPSPEED, Qt::Horizontal, tr("UP Speed", "i.e: Upload speed"));
  DLListModel->setHeaderData(SEEDSLEECH, Qt::Horizontal, tr("Seeds/Leechers", "i.e: full/partial sources"));
  DLListModel->setHeaderData(RATIO, Qt::Horizontal, tr("Ratio"));
  DLListModel->setHeaderData(ETA, Qt::Horizontal, tr("ETA", "i.e: Estimated Time of Arrival / Time left"));
  DLListModel->setHeaderData(PRIORITY, Qt::Horizontal, "#");
  downloadList->setRootIsDecorated(false);
  downloadList->setAllColumnsShowFocus(true);
  DLDelegate = new DLListDelegate(downloadList);
  downloadList->setItemDelegate(DLDelegate);
  proxyModel = new QSortFilterProxyModel();
  proxyModel->setDynamicSortFilter(true);
  proxyModel->setSourceModel(DLListModel);
  downloadList->setModel(proxyModel);
  downloadList->setSortingEnabled(true);
  // Hide priority column
  downloadList->hideColumn(PRIORITY);
  // Hide hash column
  downloadList->hideColumn(HASH);
  loadHiddenColumns();

  connect(BTSession, SIGNAL(metadataReceived(QTorrentHandle&)), this, SLOT(updateMetadata(QTorrentHandle&)));

  // Load last columns width for download list
  if(!loadColWidthDLList()) {
    downloadList->header()->resizeSection(0, 200);
  }
  // Make download list header clickable for sorting
  downloadList->header()->setClickable(true);
  downloadList->header()->setSortIndicatorShown(true);
  // Connecting Actions to slots
  connect(downloadList, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(notifyTorrentDoubleClicked(const QModelIndex&)));
  connect(downloadList, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayDLListMenu(const QPoint&)));
  downloadList->header()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(downloadList->header(), SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayDLHoSMenu(const QPoint&)));
  // Actions
  connect(actionPause, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionPause_triggered()));
  connect(actionStart, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionStart_triggered()));
  connect(actionDelete, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionDelete_triggered()));
  connect(actionIncreasePriority, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionIncreasePriority_triggered()));
  connect(actionDecreasePriority, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionDecreasePriority_triggered()));
  connect(actionPreview_file, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionPreview_file_triggered()));
  connect(actionDelete_Permanently, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionDelete_Permanently_triggered()));
  connect(actionOpen_destination_folder, SIGNAL(triggered()), (GUI*)parent, SLOT(openDestinationFolder()));
  connect(actionTorrent_Properties, SIGNAL(triggered()), this, SLOT(propertiesSelection()));
  connect(actionForce_recheck, SIGNAL(triggered()), this, SLOT(forceRecheck()));
  connect(actionBuy_it, SIGNAL(triggered()), (GUI*)parent, SLOT(goBuyPage()));
  connect(actionCopy_magnet_link, SIGNAL(triggered()), (GUI*)parent, SLOT(copyMagnetURI()));

  connect(actionHOSColName, SIGNAL(triggered()), this, SLOT(hideOrShowColumnName()));
  connect(actionHOSColSize, SIGNAL(triggered()), this, SLOT(hideOrShowColumnSize()));
  connect(actionHOSColProgress, SIGNAL(triggered()), this, SLOT(hideOrShowColumnProgress()));
  connect(actionHOSColDownSpeed, SIGNAL(triggered()), this, SLOT(hideOrShowColumnDownSpeed()));
  connect(actionHOSColUpSpeed, SIGNAL(triggered()), this, SLOT(hideOrShowColumnUpSpeed()));
  connect(actionHOSColSeedersLeechers, SIGNAL(triggered()), this, SLOT(hideOrShowColumnSeedersLeechers()));
  connect(actionHOSColRatio, SIGNAL(triggered()), this, SLOT(hideOrShowColumnRatio()));
  connect(actionHOSColEta, SIGNAL(triggered()), this, SLOT(hideOrShowColumnEta()));
  connect(actionHOSColPriority, SIGNAL(triggered()), this, SLOT(hideOrShowColumnPriority()));

  // Load last sorted column
  loadLastSortedColumn();
  // Set info Bar infos
  BTSession->addConsoleMessage(tr("qBittorrent %1 started.", "e.g: qBittorrent v0.x started.").arg(QString::fromUtf8(""VERSION)));
  qDebug("Download tab built");
}

DownloadingTorrents::~DownloadingTorrents() {
  saveLastSortedColumn();
  saveColWidthDLList();
  saveHiddenColumns();
  delete DLDelegate;
  delete proxyModel;
  delete DLListModel;
}

void DownloadingTorrents::enablePriorityColumn(bool enable) {
  if(enable) {
    downloadList->showColumn(PRIORITY);
  } else {
    downloadList->hideColumn(PRIORITY);
  }
}

void DownloadingTorrents::notifyTorrentDoubleClicked(const QModelIndex& index) {
  unsigned int row = index.row();
  QString hash = getHashFromRow(row);
  emit torrentDoubleClicked(hash, false);
}

unsigned int DownloadingTorrents::getNbTorrentsInList() const {
  return nbTorrents;
}

// Note: do not actually pause the torrent in BT session
void DownloadingTorrents::pauseTorrent(QString hash) {
  int row = getRowFromHash(hash);
  if(row == -1)
    return;
  DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)0.0));
  DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)0.0));
  DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
  DLListModel->setData(DLListModel->index(row, NAME), QIcon(QString::fromUtf8(":/Icons/skin/paused.png")), Qt::DecorationRole);
  DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(QString::fromUtf8("0/0")));
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  //DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
  setRowColor(row, QString::fromUtf8("red"));
}

QString DownloadingTorrents::getHashFromRow(unsigned int row) const {
  Q_ASSERT(row < (unsigned int)proxyModel->rowCount());
  return proxyModel->data(proxyModel->index(row, HASH)).toString();
}

// Show torrent properties dialog
void DownloadingTorrents::showProperties(const QModelIndex &index) {
  showPropertiesFromHash(getHashFromRow(index.row()));
}

void DownloadingTorrents::showPropertiesFromHash(QString hash) {
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  if(h.is_valid() && h.has_metadata()) {
    properties *prop = new properties(this, BTSession, h);
    connect(prop, SIGNAL(filteredFilesChanged(QString)), this, SLOT(updateFileSizeAndProgress(QString)));
    connect(prop, SIGNAL(trackersChanged(QString)), BTSession, SLOT(saveTrackerFile(QString)));
    prop->show();
  }
}

// Remove a torrent from the download list but NOT from the BT Session
void DownloadingTorrents::deleteTorrent(QString hash) {
  int row = getRowFromHash(hash);
  if(row == -1){
    qDebug("torrent is not in download list, nothing to delete");
    return;
  }
  DLListModel->removeRow(row);
  --nbTorrents;
  emit unfinishedTorrentsNumberChanged(nbTorrents);
}

void DownloadingTorrents::on_actionSet_download_limit_triggered() {
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  QStringList hashes;
  foreach(const QModelIndex &index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file hash
      hashes << getHashFromRow(index.row());
    }
  }
  Q_ASSERT(hashes.size() > 0);
  new BandwidthAllocationDialog(this, false, BTSession, hashes);
}

void DownloadingTorrents::on_actionSet_upload_limit_triggered() {
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  QStringList hashes;
  foreach(const QModelIndex &index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file hash
      hashes << getHashFromRow(index.row());
    }
  }
  Q_ASSERT(hashes.size() > 0);
  new BandwidthAllocationDialog(this, true, BTSession, hashes);
}

// display properties of selected items
void DownloadingTorrents::propertiesSelection(){
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  foreach(const QModelIndex &index, selectedIndexes){
    if(index.column() == NAME){
      showProperties(index);
    }
  }
}

void DownloadingTorrents::forceRecheck() {
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  foreach(const QModelIndex &index, selectedIndexes){
    if(index.column() == NAME){
      QString hash = getHashFromRow(index.row());
      QTorrentHandle h = BTSession->getTorrentHandle(hash);
      if(h.is_valid() && h.has_metadata())
        h.force_recheck();
    }
  }
}

void DownloadingTorrents::displayDLListMenu(const QPoint&) {
  QMenu myDLLlistMenu(this);
  // Enable/disable pause/start action given the DL state
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  bool has_pause = false, has_start = false, has_preview = false;
  bool one_has_metadata = false;
  QTorrentHandle h;
  qDebug("Displaying menu");
  foreach(const QModelIndex &index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file name
      QString hash = getHashFromRow(index.row());
      // Get handle and pause the torrent
      h = BTSession->getTorrentHandle(hash);
      if(!h.is_valid()) continue;
      if(h.has_metadata()) {
        one_has_metadata = true;
      }
      if(h.is_paused()) {
        if(!has_start) {
          myDLLlistMenu.addAction(actionStart);
          has_start = true;
        }
      }else{
        if(!has_pause) {
          myDLLlistMenu.addAction(actionPause);
          has_pause = true;
        }
      }
      if(h.has_metadata() && BTSession->isFilePreviewPossible(hash) && !has_preview) {
        myDLLlistMenu.addAction(actionPreview_file);
        has_preview = true;
      }
      if(has_pause && has_start && has_preview) break;
    }
  }
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionDelete);
  myDLLlistMenu.addAction(actionDelete_Permanently);
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionSet_download_limit);
  myDLLlistMenu.addAction(actionSet_upload_limit);
  myDLLlistMenu.addSeparator();
  if(one_has_metadata) {
    myDLLlistMenu.addAction(actionForce_recheck);
    myDLLlistMenu.addSeparator();
  }
  myDLLlistMenu.addAction(actionOpen_destination_folder);
  if(one_has_metadata)
    myDLLlistMenu.addAction(actionTorrent_Properties);
  if(BTSession->isQueueingEnabled()) {
    myDLLlistMenu.addSeparator();
    myDLLlistMenu.addAction(actionIncreasePriority);
    myDLLlistMenu.addAction(actionDecreasePriority);
  }
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionCopy_magnet_link);
  myDLLlistMenu.addAction(actionBuy_it);
  // Call menu
  myDLLlistMenu.exec(QCursor::pos());
}


/*
 * Hiding Columns functions
 */

// hide/show columns menu
void DownloadingTorrents::displayDLHoSMenu(const QPoint&){
  QMenu hideshowColumn(this);
  hideshowColumn.setTitle(tr("Hide or Show Column"));
  int lastCol;
  if(BTSession->isQueueingEnabled()) {
    lastCol = PRIORITY;
  } else {
    lastCol = ETA;
  }
  for(int i=0; i <= lastCol; ++i) {
    hideshowColumn.addAction(getActionHoSCol(i));
  }
  // Call menu
  hideshowColumn.exec(QCursor::pos());
}

// toggle hide/show a column
void DownloadingTorrents::hideOrShowColumn(int index) {
  unsigned int nbVisibleColumns = 0;
  unsigned int nbCols = DLListModel->columnCount();
  // Count visible columns
  for(unsigned int i=0; i<nbCols; ++i) {
    if(!downloadList->isColumnHidden(i))
      ++nbVisibleColumns;
  }
  if(!downloadList->isColumnHidden(index)) {
    // User wants to hide the column
    // Is there at least one other visible column?
    if(nbVisibleColumns <= 1) return;
    // User can hide the column, do it.
    downloadList->setColumnHidden(index, true);
    getActionHoSCol(index)->setIcon(QIcon(QString::fromUtf8(":/Icons/oxygen/button_cancel.png")));
    --nbVisibleColumns;
  } else {
    // User want to display the column
    downloadList->setColumnHidden(index, false);
    getActionHoSCol(index)->setIcon(QIcon(QString::fromUtf8(":/Icons/oxygen/button_ok.png")));
    ++nbVisibleColumns;
  }
  //resize all others non-hidden columns
  for(unsigned int i=0; i<nbCols; ++i) {
    if(!downloadList->isColumnHidden(i)) {
      downloadList->setColumnWidth(i, (int)ceil(downloadList->columnWidth(i)+(downloadList->columnWidth(index)/nbVisibleColumns)));
    }
  }
}

void DownloadingTorrents::hidePriorityColumn(bool hide) {
  downloadList->setColumnHidden(PRIORITY, hide);
  if(hide)
    getActionHoSCol(PRIORITY)->setIcon(QIcon(QString::fromUtf8(":/Icons/oxygen/button_cancel.png")));
  else
    getActionHoSCol(PRIORITY)->setIcon(QIcon(QString::fromUtf8(":/Icons/oxygen/button_ok.png")));
}

// save the hidden columns in settings
void DownloadingTorrents::saveHiddenColumns() {
  QSettings settings("qBittorrent", "qBittorrent");
  QStringList ishidden_list;
  short nbColumns = DLListModel->columnCount()-1;

  for(short i=0; i<nbColumns; ++i){
    if(downloadList->isColumnHidden(i)) {
      ishidden_list << QString::fromUtf8(misc::toString(0).c_str());
    } else {
      ishidden_list << QString::fromUtf8(misc::toString(1).c_str());
    }
  }
  settings.setValue("DownloadListColsHoS", ishidden_list.join(" "));
}

// load the previous settings, and hide the columns
bool DownloadingTorrents::loadHiddenColumns() {
  bool loaded = false;
  QSettings settings("qBittorrent", "qBittorrent");
  QString line = settings.value("DownloadListColsHoS", QString()).toString();
  QStringList ishidden_list;
  if(!line.isEmpty()) {
    ishidden_list = line.split(' ');
    if(ishidden_list.size() == DLListModel->columnCount()-1) {
      unsigned int listSize = ishidden_list.size();
      for(unsigned int i=0; i<listSize; ++i){
        downloadList->header()->resizeSection(i, ishidden_list.at(i).toInt());
      }
      loaded = true;
    }
  }
  for(int i=0; i<DLListModel->columnCount()-1; i++) {
    if(loaded && ishidden_list.at(i) == "0") {
      downloadList->setColumnHidden(i, true);
      getActionHoSCol(i)->setIcon(QIcon(QString::fromUtf8(":/Icons/oxygen/button_cancel.png")));
    } else {
      getActionHoSCol(i)->setIcon(QIcon(QString::fromUtf8(":/Icons/oxygen/button_ok.png")));
    }
  }
  return loaded;
}

void DownloadingTorrents::hideOrShowColumnName() {
  hideOrShowColumn(NAME);
}

void DownloadingTorrents::hideOrShowColumnSize() {
  hideOrShowColumn(SIZE);
}

void DownloadingTorrents::hideOrShowColumnProgress() {
  hideOrShowColumn(PROGRESS);
}

void DownloadingTorrents::hideOrShowColumnDownSpeed() {
  hideOrShowColumn(DLSPEED);
}

void DownloadingTorrents::hideOrShowColumnUpSpeed() {
  hideOrShowColumn(UPSPEED);
}

void DownloadingTorrents::hideOrShowColumnSeedersLeechers() {
  hideOrShowColumn(SEEDSLEECH);
}

void DownloadingTorrents::hideOrShowColumnRatio() {
  hideOrShowColumn(RATIO);
}

void DownloadingTorrents::hideOrShowColumnEta() {
  hideOrShowColumn(ETA);
}

void DownloadingTorrents::hideOrShowColumnPriority() {
  hideOrShowColumn(PRIORITY);
}

// getter, return the action hide or show whose id is index
QAction* DownloadingTorrents::getActionHoSCol(int index) {
  switch(index) {
  case NAME :
    return actionHOSColName;
    break;
  case SIZE :
    return actionHOSColSize;
    break;
  case PROGRESS :
    return actionHOSColProgress;
    break;
  case DLSPEED :
    return actionHOSColDownSpeed;
    break;
  case UPSPEED :
    return actionHOSColUpSpeed;
    break;
  case SEEDSLEECH :
    return actionHOSColSeedersLeechers;
    break;
  case RATIO :
    return actionHOSColRatio;
    break;
  case ETA :
    return actionHOSColEta;
    break;
  case PRIORITY :
    return actionHOSColPriority;
    break;
  default :
      return NULL;
}
}

QStringList DownloadingTorrents::getSelectedTorrents(bool only_one) const{
  QStringList res;
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  foreach(const QModelIndex &index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file hash
      QString hash = getHashFromRow(index.row());
      res << hash;
      if(only_one) break;
    }
  }
  return res;
}

void DownloadingTorrents::updateMetadata(QTorrentHandle &h) {
  QString hash = h.hash();
  int row = getRowFromHash(hash);
  if(row != -1) {
    qDebug("Updating torrent metadata in download list");
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(h.name()));
    DLListModel->setData(DLListModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  }
}

// get information from torrent handles and
// update download list accordingly
bool DownloadingTorrents::updateTorrent(QTorrentHandle h) {
  if(!h.is_valid()) return false;
  bool added = false;
  try{
    QString hash = h.hash();
    int row = getRowFromHash(hash);
    if(row == -1) {
      qDebug("Info: Could not find filename in download list, adding it...");
      addTorrent(hash);
      row = getRowFromHash(hash);
      added = true;
    }
    Q_ASSERT(row != -1);
    // Update Priority
    if(BTSession->isQueueingEnabled()) {
      DLListModel->setData(DLListModel->index(row, PRIORITY), QVariant((int)BTSession->getDlTorrentPriority(hash)));
      if(h.is_queued()) {
        if(h.state() == torrent_status::checking_files || h.state() == torrent_status::queued_for_checking) {
          DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/oxygen/time.png"))), Qt::DecorationRole);
          if(!downloadList->isColumnHidden(PROGRESS)) {
            DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
          }
        }else {
          DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/queued.png"))), Qt::DecorationRole);
          if(!downloadList->isColumnHidden(ETA)) {
            DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
          }
        }
        // Reset speeds and seeds/leech
        DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)0.));
        DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)0.));
        DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant("0/0"));
        setRowColor(row, QString::fromUtf8("grey"));
        return added;
      }
    }
    if(!downloadList->isColumnHidden(PROGRESS))
      DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
    // No need to update a paused torrent
    if(h.is_paused()) return added;
    // Parse download state
    // Setting download state
    switch(h.state()) {
        case torrent_status::checking_files:
        case torrent_status::queued_for_checking:
      DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/oxygen/time.png"))), Qt::DecorationRole);
      setRowColor(row, QString::fromUtf8("grey"));
      break;
        case torrent_status::downloading:
        case torrent_status::downloading_metadata:
      if(h.download_payload_rate() > 0) {
        DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png"))), Qt::DecorationRole);
        if(!downloadList->isColumnHidden(ETA)) {
          DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)BTSession->getETA(hash)));
        }
        setRowColor(row, QString::fromUtf8("green"));
      }else{
        DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/stalled.png"))), Qt::DecorationRole);
        if(!downloadList->isColumnHidden(ETA)) {
          DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
        }
        setRowColor(row, QApplication::palette().color(QPalette::WindowText));
      }
      if(!downloadList->isColumnHidden(DLSPEED)) {
        DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)h.download_payload_rate()));
      }
      if(!downloadList->isColumnHidden(UPSPEED)) {
        DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)h.upload_payload_rate()));
      }
      break;
        default:
      if(!downloadList->isColumnHidden(ETA)) {
        DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
      }
    }
    if(!downloadList->isColumnHidden(SEEDSLEECH)) {
      QString tmp = misc::toQString(h.num_seeds(), true);
      if(h.num_complete() >= 0)
        tmp.append(QString("(")+misc::toQString(h.num_complete())+QString(")"));
      tmp.append(QString("/")+misc::toQString(h.num_peers() - h.num_seeds(), true));
      if(h.num_incomplete() >= 0)
        tmp.append(QString("(")+misc::toQString(h.num_incomplete())+QString(")"));
      DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(tmp));
    }
    if(!downloadList->isColumnHidden(RATIO)) {
      DLListModel->setData(DLListModel->index(row, RATIO), QVariant(misc::toQString(BTSession->getRealRatio(hash))));
    }
  }catch(invalid_handle e) {}
  return added;
}

void DownloadingTorrents::addTorrent(QString hash) {
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  int row = getRowFromHash(hash);
  qDebug("DL: addTorrent(): %s, row: %d", (const char*)hash.toLocal8Bit(), row);
  if(row != -1) return;
  row = DLListModel->rowCount();
  // Adding torrent to download list
  DLListModel->insertRow(row);
  DLListModel->setData(DLListModel->index(row, NAME), QVariant(h.name()));
  DLListModel->setData(DLListModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(QString::fromUtf8("0/0")));
  DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
  DLListModel->setData(DLListModel->index(row, RATIO), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
  if(BTSession->isQueueingEnabled())
    DLListModel->setData(DLListModel->index(row, PRIORITY), QVariant((int)BTSession->getDlTorrentPriority(hash)));
  DLListModel->setData(DLListModel->index(row, HASH), QVariant(hash));
  // Pause torrent if it is
  if(h.is_paused()) {
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/paused.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("red"));
  }else{
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/stalled.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("grey"));
  }
  ++nbTorrents;
  emit unfinishedTorrentsNumberChanged(nbTorrents);
}

// Save columns width in a file to remember them
// (download list)
void DownloadingTorrents::saveColWidthDLList() const{
  qDebug("Saving columns width in download list");
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QStringList width_list;
  QStringList new_width_list;
  short nbColumns = DLListModel->columnCount()-1;
  QString line = settings.value("DownloadListColsWidth", QString()).toString();
  if(!line.isEmpty()) {
    width_list = line.split(' ');
  }
  for(short i=0; i<nbColumns; ++i){
    if(downloadList->columnWidth(i)<1 && width_list.size() == nbColumns && width_list.at(i).toInt()>=1) {
      // load the former width
      new_width_list << width_list.at(i);
    } else if(downloadList->columnWidth(i)>=1) { 
      // usual case, save the current width
      new_width_list << QString::fromUtf8(misc::toString(downloadList->columnWidth(i)).c_str());
    } else { 
      // default width
      downloadList->resizeColumnToContents(i);
      new_width_list << QString::fromUtf8(misc::toString(downloadList->columnWidth(i)).c_str());
    }
  }
  settings.setValue(QString::fromUtf8("DownloadListColsWidth"), new_width_list.join(QString::fromUtf8(" ")));
  QVariantList visualIndexes;
  for(int i=0; i<nbColumns; ++i) {
    visualIndexes.append(downloadList->header()->visualIndex(i));
  }
  settings.setValue(QString::fromUtf8("DownloadListVisualIndexes"), visualIndexes);
  qDebug("Download list columns width saved");
}

// Load columns width in a file that were saved previously
// (download list)
bool DownloadingTorrents::loadColWidthDLList() {
  qDebug("Loading columns width for download list");
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QString line = settings.value(QString::fromUtf8("DownloadListColsWidth"), QString()).toString();
  if(line.isEmpty())
    return false;
  QStringList width_list = line.split(QString::fromUtf8(" "));
  if(width_list.size() != DLListModel->columnCount()-1) {
    qDebug("Corrupted values for download list columns sizes");
    return false;
  }
  unsigned int listSize = width_list.size();
  for(unsigned int i=0; i<listSize; ++i) {
    downloadList->header()->resizeSection(i, width_list.at(i).toInt());
  }
  QVariantList visualIndexes = settings.value(QString::fromUtf8("DownloadListVisualIndexes"), QVariantList()).toList();
  if(visualIndexes.size() != DLListModel->columnCount()-1) {
    qDebug("Corrupted values for download list columns sizes");
    return false;
  }
  bool change = false;
  do {
    change = false;
    for(int i=0;i<visualIndexes.size(); ++i) {
      int new_visual_index = visualIndexes.at(downloadList->header()->logicalIndex(i)).toInt();
      if(i != new_visual_index) {
        qDebug("Moving column from %d to %d", downloadList->header()->logicalIndex(i), new_visual_index);
        downloadList->header()->moveSection(i, new_visual_index);
        change = true;
      }
    }
  }while(change);
  qDebug("Download list columns width loaded");
  return true;
}

void DownloadingTorrents::saveLastSortedColumn() {
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  Qt::SortOrder sortOrder = downloadList->header()->sortIndicatorOrder();
  QString sortOrderLetter;
  if(sortOrder == Qt::AscendingOrder)
    sortOrderLetter = QString::fromUtf8("a");
  else
    sortOrderLetter = QString::fromUtf8("d");
  int index = downloadList->header()->sortIndicatorSection();
  settings.setValue(QString::fromUtf8("DownloadListSortedCol"), misc::toQString(index)+sortOrderLetter);
}

void DownloadingTorrents::loadLastSortedColumn() {
  // Loading last sorted column
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QString sortedCol = settings.value(QString::fromUtf8("DownloadListSortedCol"), QString()).toString();
  if(!sortedCol.isEmpty()) {
    Qt::SortOrder sortOrder;
    if(sortedCol.endsWith(QString::fromUtf8("d")))
      sortOrder = Qt::DescendingOrder;
    else
      sortOrder = Qt::AscendingOrder;
    sortedCol = sortedCol.left(sortedCol.size()-1);
    int index = sortedCol.toInt();
    downloadList->sortByColumn(index, sortOrder);
  }
}

void DownloadingTorrents::updateFileSizeAndProgress(QString hash) {
  int row = getRowFromHash(hash);
  Q_ASSERT(row != -1);
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  DLListModel->setData(DLListModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  //DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
}

// Set the color of a row in data model
void DownloadingTorrents::setRowColor(int row, QColor color) {
  unsigned int nbColumns = DLListModel->columnCount()-1;
  for(unsigned int i=0; i<nbColumns; ++i) {
    DLListModel->setData(DLListModel->index(row, i), QVariant(color), Qt::ForegroundRole);
  }
}

// return the row of in data model
// corresponding to the given the hash
int DownloadingTorrents::getRowFromHash(QString hash) const{
  unsigned int nbRows = DLListModel->rowCount();
  for(unsigned int i=0; i<nbRows; ++i) {
    if(DLListModel->data(DLListModel->index(i, HASH)) == hash) {
      return i;
    }
  }
  return -1;
}

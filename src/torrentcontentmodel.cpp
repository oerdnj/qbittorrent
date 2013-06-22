/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006-2012  Christophe Dumez
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

#include "iconprovider.h"
#include "misc.h"
#include "torrentcontentmodel.h"
#include "torrentcontentmodelitem.h"
#include <QDir>

TorrentContentModel::TorrentContentModel(QObject *parent):
  QAbstractItemModel(parent),
    m_rootItem(new TorrentContentModelItem(QList<QVariant>() << tr("Name") << tr("Size")
                                         << tr("Progress") << tr("Priority")))
{
}

TorrentContentModel::~TorrentContentModel()
{
  delete m_rootItem;
}

void TorrentContentModel::updateFilesProgress(const std::vector<libtorrent::size_type>& fp)
{
  emit layoutAboutToBeChanged();
  Q_ASSERT(m_filesIndex.size() == (int)fp.size());
  if (m_filesIndex.size() != (int)fp.size()) return;
  for (uint i=0; i<fp.size(); ++i) {
    m_filesIndex[i]->setProgress(fp[i]);
  }
  emit dataChanged(index(0,0), index(rowCount(), columnCount()));
}

void TorrentContentModel::updateFilesPriorities(const std::vector<int>& fprio)
{
  emit layoutAboutToBeChanged();
  Q_ASSERT(m_filesIndex.size() == (int)fprio.size());
  if (m_filesIndex.size() != (int)fprio.size()) return;
  for (uint i=0; i<fprio.size(); ++i) {
    m_filesIndex[i]->setPriority(fprio[i]);
  }
  emit dataChanged(index(0,0), index(rowCount(), columnCount()));
}

std::vector<int> TorrentContentModel::getFilesPriorities() const
{
  std::vector<int> prio;
  prio.reserve(m_filesIndex.size());
  foreach (const TorrentContentModelItem* file, m_filesIndex) {
    prio.push_back(file->getPriority());
  }
  return prio;
}

bool TorrentContentModel::allFiltered() const
{
  for (int i=0; i<m_rootItem->childCount(); ++i) {
    if (m_rootItem->child(i)->getPriority() != prio::IGNORED)
      return false;
  }
  return true;
}

int TorrentContentModel::columnCount(const QModelIndex& parent) const
{
  if (parent.isValid())
    return static_cast<TorrentContentModelItem*>(parent.internalPointer())->columnCount();
  else
    return m_rootItem->columnCount();
}

bool TorrentContentModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
  if (!index.isValid())
    return false;

  if (index.column() == 0 && role == Qt::CheckStateRole) {
    TorrentContentModelItem *item = static_cast<TorrentContentModelItem*>(index.internalPointer());
    qDebug("setData(%s, %d", qPrintable(item->getName()), value.toInt());
    if (item->getPriority() != value.toInt()) {
      if (value.toInt() == Qt::PartiallyChecked)
        item->setPriority(prio::PARTIAL);
      else if (value.toInt() == Qt::Unchecked)
        item->setPriority(prio::IGNORED);
      else
        item->setPriority(prio::NORMAL);
      emit dataChanged(this->index(0,0), this->index(rowCount()-1, columnCount()-1));
      emit filteredFilesChanged();
    }
    return true;
  }

  if (role == Qt::EditRole) {
    TorrentContentModelItem *item = static_cast<TorrentContentModelItem*>(index.internalPointer());
    switch(index.column()) {
    case TorrentContentModelItem::COL_NAME:
      item->setName(value.toString());
      break;
    case TorrentContentModelItem::COL_SIZE:
      item->setSize(value.toULongLong());
      break;
    case TorrentContentModelItem::COL_PROGRESS:
      item->setProgress(value.toDouble());
      break;
    case TorrentContentModelItem::COL_PRIO:
      item->setPriority(value.toInt());
      break;
    default:
      return false;
    }
    emit dataChanged(index, index);
    return true;
  }
  return false;
}

TorrentContentModelItem::FileType TorrentContentModel::getType(const QModelIndex& index) const
{
  const TorrentContentModelItem *item = static_cast<const TorrentContentModelItem*>(index.internalPointer());
  return item->getType();
}

int TorrentContentModel::getFileIndex(const QModelIndex& index)
{
  TorrentContentModelItem* item = static_cast<TorrentContentModelItem*>(index.internalPointer());
  return item->getFileIndex();
}

QVariant TorrentContentModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid())
    return QVariant();

  TorrentContentModelItem *item = static_cast<TorrentContentModelItem*>(index.internalPointer());
  if (index.column() == 0 && role == Qt::DecorationRole) {
    if (item->isFolder())
      return IconProvider::instance()->getIcon("inode-directory");
    else
      return IconProvider::instance()->getIcon("text-plain");
  }
  if (index.column() == 0 && role == Qt::CheckStateRole) {
    if (item->data(TorrentContentModelItem::COL_PRIO).toInt() == prio::IGNORED)
      return Qt::Unchecked;
    if (item->data(TorrentContentModelItem::COL_PRIO).toInt() == prio::PARTIAL)
      return Qt::PartiallyChecked;
    return Qt::Checked;
  }
  if (role != Qt::DisplayRole)
    return QVariant();

  return item->data(index.column());
}

Qt::ItemFlags TorrentContentModel::flags(const QModelIndex& index) const
{
  if (!index.isValid())
    return 0;

  if (getType(index) == TorrentContentModelItem::FOLDER)
    return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsTristate;
  return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
}

QVariant TorrentContentModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    return m_rootItem->data(section);

  return QVariant();
}

QModelIndex TorrentContentModel::index(int row, int column, const QModelIndex& parent) const
{
  if (parent.isValid() && parent.column() != 0)
    return QModelIndex();

  if (column >= TorrentContentModelItem::NB_COL)
    return QModelIndex();

  TorrentContentModelItem *parentItem;

  if (!parent.isValid())
    parentItem = m_rootItem;
  else
    parentItem = static_cast<TorrentContentModelItem*>(parent.internalPointer());
  Q_ASSERT(parentItem);
  if (row >= parentItem->childCount())
    return QModelIndex();

  TorrentContentModelItem *childItem = parentItem->child(row);
  if (childItem) {
    return createIndex(row, column, childItem);
  } else {
    return QModelIndex();
  }
}

QModelIndex TorrentContentModel::parent(const QModelIndex& index) const
{
  if (!index.isValid())
    return QModelIndex();

  TorrentContentModelItem *childItem = static_cast<TorrentContentModelItem*>(index.internalPointer());
  if (!childItem)
    return QModelIndex();
  TorrentContentModelItem *parentItem = childItem->parent();

  if (parentItem == m_rootItem)
    return QModelIndex();

  return createIndex(parentItem->row(), 0, parentItem);
}

int TorrentContentModel::rowCount(const QModelIndex& parent) const
{
  TorrentContentModelItem* parentItem;

  if (parent.column() > 0)
    return 0;

  if (!parent.isValid())
    parentItem = m_rootItem;
  else
    parentItem = static_cast<TorrentContentModelItem*>(parent.internalPointer());

  return parentItem->childCount();
}

void TorrentContentModel::clear()
{
  qDebug("clear called");
  beginResetModel();
  m_filesIndex.clear();
  m_rootItem->deleteAllChildren();
  endResetModel();
}

void TorrentContentModel::setupModelData(const libtorrent::torrent_info& t)
{
  qDebug("setup model data called");
  if (t.num_files() == 0)
    return;

  emit layoutAboutToBeChanged();
  // Initialize files_index array
  qDebug("Torrent contains %d files", t.num_files());
  m_filesIndex.reserve(t.num_files());

  TorrentContentModelItem* parent = m_rootItem;
  TorrentContentModelItem* root_folder = parent;
  TorrentContentModelItem* current_parent;

  // Iterate over files
  for (int i=0; i<t.num_files(); ++i) {
    const libtorrent::file_entry& fentry = t.file_at(i);
    current_parent = root_folder;
#if LIBTORRENT_VERSION_MINOR >= 16
    QString path = misc::toQStringU(fentry.path);
#else
    QString path = misc::toQStringU(fentry.path.string());
#endif
    // Iterate of parts of the path to create necessary folders
    QStringList pathFolders = path.split(QRegExp("[/\\\\]"), QString::SkipEmptyParts);
    pathFolders.removeLast();
    foreach (const QString& pathPart, pathFolders) {
      if (pathPart == ".unwanted")
        continue;
      TorrentContentModelItem* new_parent = current_parent->childWithName(pathPart);
      if (!new_parent)
        new_parent = new TorrentContentModelItem(pathPart, current_parent);
      current_parent = new_parent;
    }
    // Actually create the file
    m_filesIndex.push_back(new TorrentContentModelItem(fentry, current_parent, i));
  }
  emit layoutChanged();
}

void TorrentContentModel::selectAll()
{
  for (int i=0; i<m_rootItem->childCount(); ++i) {
    TorrentContentModelItem* child = m_rootItem->child(i);
    if (child->getPriority() == prio::IGNORED)
      child->setPriority(prio::NORMAL);
  }
  emit dataChanged(index(0,0), index(rowCount(), columnCount()));
}

void TorrentContentModel::selectNone()
{
  for (int i=0; i<m_rootItem->childCount(); ++i) {
    m_rootItem->child(i)->setPriority(prio::IGNORED);
  }
 emit dataChanged(index(0,0), index(rowCount(), columnCount()));
}

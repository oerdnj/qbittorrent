/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2017  Tony Gregerson <tony.gregerson@gmail.com>
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
 */

#include "tagfiltermodel.h"

#include <QDebug>
#include <QHash>
#include <QIcon>

#include "base/bittorrent/session.h"
#include "base/bittorrent/torrenthandle.h"
#include "guiiconprovider.h"

namespace
{
    QString getSpecialAllTag()
    {
        static const QString *const ALL_TAG = new QString(" ");
        Q_ASSERT(!BitTorrent::Session::isValidTag(*ALL_TAG));
        return *ALL_TAG;
    }

    QString getSpecialUntaggedTag()
    {
        static const QString *const UNTAGGED_TAG = new QString("  ");
        Q_ASSERT(!BitTorrent::Session::isValidTag(*UNTAGGED_TAG));
        return *UNTAGGED_TAG;
    }
}

class TagModelItem
{
public:
    TagModelItem(const QString &tag, int torrentsCount = 0)
        : m_tag(tag)
        , m_torrentsCount(torrentsCount)
    {
    }

    QString tag() const
    {
        return m_tag;
    }

    int torrentsCount() const
    {
        return m_torrentsCount;
    }

    void increaseTorrentsCount()
    {
        ++m_torrentsCount;
    }

    void decreaseTorrentsCount()
    {
        Q_ASSERT(m_torrentsCount > 0);
        --m_torrentsCount;
    }

private:
    QString m_tag;
    int m_torrentsCount;
};

TagFilterModel::TagFilterModel(QObject *parent)
    : QAbstractListModel(parent)
{
    using Session = BitTorrent::Session;
    auto session = Session::instance();

    connect(session, &Session::tagAdded, this, &TagFilterModel::tagAdded);
    connect(session, &Session::tagRemoved, this, &TagFilterModel::tagRemoved);
    connect(session, &Session::torrentTagAdded, this, &TagFilterModel::torrentTagAdded);
    connect(session, &Session::torrentTagRemoved, this, &TagFilterModel::torrentTagRemoved);
    connect(session, &Session::torrentAdded, this, &TagFilterModel::torrentAdded);
    connect(session, &Session::torrentAboutToBeRemoved, this, &TagFilterModel::torrentAboutToBeRemoved);
    populate();
}

TagFilterModel::~TagFilterModel() = default;

bool TagFilterModel::isSpecialItem(const QModelIndex &index)
{
    // the first two items are special items: 'All' and 'Untagged'
    return (!index.parent().isValid() && (index.row() <= 1));
}

QVariant TagFilterModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() != 0)
        return QVariant();

    const int row = index.internalId();
    Q_ASSERT(isValidRow(row));
    const TagModelItem &item = m_tagItems[row];

    switch (role) {
    case Qt::DecorationRole:
        return GuiIconProvider::instance()->getIcon("inode-directory");
    case Qt::DisplayRole:
        return QString(QLatin1String("%1 (%2)"))
               .arg(tagDisplayName(item.tag())).arg(item.torrentsCount());
    case Qt::UserRole:
        return item.torrentsCount();
    default:
        return QVariant();
    }
}

Qt::ItemFlags TagFilterModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant TagFilterModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if ((orientation == Qt::Horizontal) && (role == Qt::DisplayRole))
        if (section == 0)
            return tr("Tags");
    return QVariant();
}

QModelIndex TagFilterModel::index(int row, int, const QModelIndex &) const
{
    if (!isValidRow(row))
        return QModelIndex();
    return createIndex(row, 0, row);
}

int TagFilterModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_tagItems.count();
    return 0;
}

bool TagFilterModel::isValidRow(int row) const
{
    return (row >= 0) && (row < m_tagItems.size());
}

QModelIndex TagFilterModel::index(const QString &tag) const
{
    const int row = findRow(tag);
    if (!isValidRow(row))
        return QModelIndex();
    return index(row, 0, QModelIndex());
}

QString TagFilterModel::tag(const QModelIndex &index) const
{
    if (!index.isValid())
        return QString();
    const int row = index.internalId();
    Q_ASSERT(isValidRow(row));
    return m_tagItems[row].tag();
}

void TagFilterModel::tagAdded(const QString &tag)
{
    const int row = m_tagItems.count();
    beginInsertRows(QModelIndex(), row, row);
    addToModel(tag, 0);
    endInsertRows();
}

void TagFilterModel::tagRemoved(const QString &tag)
{
    QModelIndex i = index(tag);
    beginRemoveRows(i.parent(), i.row(), i.row());
    removeFromModel(i.row());
    endRemoveRows();
}

void TagFilterModel::torrentTagAdded(BitTorrent::TorrentHandle *const torrent, const QString &tag)
{
    if (torrent->tags().count() == 1)
        untaggedItem()->decreaseTorrentsCount();

    const int row = findRow(tag);
    Q_ASSERT(isValidRow(row));
    TagModelItem &item = m_tagItems[row];

    item.increaseTorrentsCount();
    const QModelIndex i = index(row, 0, QModelIndex());
    emit dataChanged(i, i);
}

void TagFilterModel::torrentTagRemoved(BitTorrent::TorrentHandle* const torrent, const QString &tag)
{
    Q_ASSERT(torrent->tags().count() >= 0);
    if (torrent->tags().count() == 0)
        untaggedItem()->increaseTorrentsCount();

    const int row = findRow(tag);
    Q_ASSERT(isValidRow(row));
    TagModelItem &item = m_tagItems[row];

    item.decreaseTorrentsCount();
    const QModelIndex i = index(row, 0, QModelIndex());
    emit dataChanged(i, i);
}

void TagFilterModel::torrentAdded(BitTorrent::TorrentHandle *const torrent)
{
    allTagsItem()->increaseTorrentsCount();

    const QVector<TagModelItem *> items = findItems(torrent->tags());
    if (items.isEmpty())
        untaggedItem()->increaseTorrentsCount();

    foreach (TagModelItem *item, items)
        item->increaseTorrentsCount();
}

void TagFilterModel::torrentAboutToBeRemoved(BitTorrent::TorrentHandle *const torrent)
{
    allTagsItem()->decreaseTorrentsCount();

    if (torrent->tags().isEmpty())
        untaggedItem()->decreaseTorrentsCount();

    foreach (TagModelItem *item, findItems(torrent->tags()))
        item->decreaseTorrentsCount();
}

QString TagFilterModel::tagDisplayName(const QString &tag)
{
    if (tag == getSpecialAllTag())
        return tr("All");
    if (tag == getSpecialUntaggedTag())
        return tr("Untagged");
    return tag;
}

void TagFilterModel::populate()
{
    using Torrent = BitTorrent::TorrentHandle;

    auto session = BitTorrent::Session::instance();
    auto torrents = session->torrents();

    // All torrents
    addToModel(getSpecialAllTag(), torrents.count());

    const int untaggedCount = std::count_if(torrents.begin(), torrents.end(),
                                             [](Torrent *torrent) { return torrent->tags().isEmpty(); });
    addToModel(getSpecialUntaggedTag(), untaggedCount);

    foreach (const QString &tag, session->tags()) {
        const int count = std::count_if(torrents.begin(), torrents.end(),
                                        [tag](Torrent *torrent) { return torrent->hasTag(tag); });
        addToModel(tag, count);
    }
}

void TagFilterModel::addToModel(const QString &tag, int count)
{
    m_tagItems.append(TagModelItem(tag, count));
}

void TagFilterModel::removeFromModel(int row)
{
    Q_ASSERT(isValidRow(row));
    m_tagItems.removeAt(row);
}

int TagFilterModel::findRow(const QString &tag) const
{
    for (int i = 0; i < m_tagItems.size(); ++i) {
        if (m_tagItems[i].tag() == tag)
            return i;
    }
    return -1;
}

TagModelItem *TagFilterModel::findItem(const QString &tag)
{
    const int row = findRow(tag);
    if (!isValidRow(row))
        return nullptr;
    return &m_tagItems[row];
}

QVector<TagModelItem *> TagFilterModel::findItems(const QSet<QString> &tags)
{
    QVector<TagModelItem *> items;
    items.reserve(tags.size());
    foreach (const QString &tag, tags) {
        TagModelItem *item = findItem(tag);
        if (item)
            items.push_back(item);
        else
            qWarning() << QString("Requested tag '%1' missing from the model.").arg(tag);
    }
    return items;
}

TagModelItem *TagFilterModel::allTagsItem()
{
    Q_ASSERT(m_tagItems.size() > 0);
    return &m_tagItems[0];
}

TagModelItem *TagFilterModel::untaggedItem()
{
    Q_ASSERT(m_tagItems.size() > 1);
    return &m_tagItems[1];
}

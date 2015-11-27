/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2010  Christophe Dumez
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
#include <QApplication>
#include <QPalette>

#include "torrentmodel.h"
#include "torrentpersistentdata.h"
#include "qbtsession.h"
#include "fs_utils.h"

#include <libtorrent/session.hpp>

using namespace libtorrent;

namespace {
QIcon get_paused_icon() {
    static QIcon cached = QIcon(":/icons/skin/paused.png");
    return cached;
}

QIcon get_queued_icon() {
    static QIcon cached = QIcon(":/icons/skin/queued.png");
    return cached;
}

QIcon get_downloading_icon() {
    static QIcon cached = QIcon(":/icons/skin/downloading.png");
    return cached;
}

QIcon get_stalled_downloading_icon() {
    static QIcon cached = QIcon(":/icons/skin/stalledDL.png");
    return cached;
}

QIcon get_uploading_icon() {
    static QIcon cached = QIcon(":/icons/skin/uploading.png");
    return cached;
}

QIcon get_stalled_uploading_icon() {
    static QIcon cached = QIcon(":/icons/skin/stalledUP.png");
    return cached;
}

QIcon get_completed_icon() {
    static QIcon cached = QIcon(":/icons/skin/completed.png");
    return cached;
}

QIcon get_checking_icon() {
    static QIcon cached = QIcon(":/icons/skin/checking.png");
    return cached;
}

QIcon get_error_icon() {
    static QIcon cached = QIcon(":/icons/skin/error.png");
    return cached;
}

bool isDarkTheme()
{
    QPalette pal = QApplication::palette();
    // QPalette::Base is used for the background of the Treeview
    QColor color = pal.color(QPalette::Active, QPalette::Base);
    return (color.lightness() < 127);
}
}

TorrentStatusReport::TorrentStatusReport()
    : nb_downloading(0)
    , nb_seeding(0)
    , nb_completed(0)
    , nb_active(0)
    , nb_inactive(0)
    , nb_paused(0)
{
}

TorrentModelItem::TorrentModelItem(const QTorrentHandle &h)
    : m_torrent(h)
    , m_lastStatus(h.status(torrent_handle::query_accurate_download_counters))
    , m_addedTime(TorrentPersistentData::instance()->getAddedDate(h.hash()))
    , m_label(TorrentPersistentData::instance()->getLabel(h.hash()))
    , m_name(TorrentPersistentData::instance()->getName(h.hash()))
    , m_hash(h.hash())
{
    if (m_name.isEmpty())
        m_name = h.name();
    // If name is empty show the hash. This happens when magnet isn't retrieved.
    if (m_name.isEmpty())
        m_name = h.hash();
}

void TorrentModelItem::refreshStatus(libtorrent::torrent_status const& status) {
    m_lastStatus = status;
}

TorrentModelItem::State TorrentModelItem::state() const {
    try {
        // Pause or Queued
        if (m_torrent.is_paused(m_lastStatus)) {
            if (TorrentPersistentData::instance()->getHasMissingFiles(misc::toQString(m_lastStatus.info_hash)))
                return STATE_PAUSED_MISSING;
            else
                return m_torrent.is_seed(m_lastStatus) ? STATE_PAUSED_UP : STATE_PAUSED_DL;
        }

        if (m_torrent.is_queued(m_lastStatus)
            && m_lastStatus.state != torrent_status::queued_for_checking
            && m_lastStatus.state != torrent_status::checking_resume_data
            && m_lastStatus.state != torrent_status::checking_files)
            return m_torrent.is_seed(m_lastStatus) ? STATE_QUEUED_UP : STATE_QUEUED_DL;

        // Other states
        switch(m_lastStatus.state) {
        case torrent_status::allocating:
            return STATE_ALLOCATING;
        case torrent_status::downloading_metadata:
            return STATE_DOWNLOADING_META;
        case torrent_status::downloading:
            if (!m_torrent.is_forced(m_lastStatus))
                return m_lastStatus.download_payload_rate > 0 ? STATE_DOWNLOADING : STATE_STALLED_DL;
            else
                return STATE_FORCED_DL;
        case torrent_status::finished:
        case torrent_status::seeding:
            if (!m_torrent.is_forced(m_lastStatus))
                return m_lastStatus.upload_payload_rate > 0 ? STATE_SEEDING : STATE_STALLED_UP;
            else
                return STATE_FORCED_UP;
        case torrent_status::queued_for_checking:
            return STATE_QUEUED_CHECK;
        case torrent_status::checking_resume_data:
            return STATE_QUEUED_FASTCHECK;
        case torrent_status::checking_files:
            return m_torrent.is_seed(m_lastStatus) ? STATE_CHECKING_UP : STATE_CHECKING_DL;
        default:
            return STATE_INVALID;
        }
    } catch(invalid_handle&) {
        return STATE_INVALID;
    }
}

QIcon TorrentModelItem::getIconByState(State state) {
    switch (state) {
    case STATE_DOWNLOADING:
    case STATE_DOWNLOADING_META:
    case STATE_FORCED_DL:
        return get_downloading_icon();
    case STATE_ALLOCATING:
    case STATE_STALLED_DL:
        return get_stalled_downloading_icon();
    case STATE_STALLED_UP:
        return get_stalled_uploading_icon();
    case STATE_SEEDING:
    case STATE_FORCED_UP:
        return get_uploading_icon();
    case STATE_PAUSED_DL:
        return get_paused_icon();
    case STATE_PAUSED_UP:
        return get_completed_icon();
    case STATE_QUEUED_DL:
    case STATE_QUEUED_UP:
        return get_queued_icon();
    case STATE_CHECKING_UP:
    case STATE_CHECKING_DL:
    case STATE_QUEUED_CHECK:
    case STATE_QUEUED_FASTCHECK:
        return get_checking_icon();
    case STATE_INVALID:
    case STATE_PAUSED_MISSING:
        return get_error_icon();
    default:
        Q_ASSERT(false);
        return get_error_icon();
    }
}

QColor TorrentModelItem::getColorByState(State state) {
    // Color names taken from http://cloford.com/resources/colours/500col.htm
    bool dark = isDarkTheme();
    switch (state) {
    case STATE_DOWNLOADING:
    case STATE_DOWNLOADING_META:
    case STATE_FORCED_DL:
        if (!dark)
            return QColor(34, 139, 34); // Forest Green
        else
            return QColor(50, 205, 50); // Lime Green
    case STATE_ALLOCATING:
    case STATE_STALLED_DL:
    case STATE_STALLED_UP:
        if (!dark)
            return QColor(0, 0, 0); // Black
        else
            return QColor(204, 204, 204); // Gray 80
    case STATE_SEEDING:
    case STATE_FORCED_UP:
        if (!dark)
            return QColor(65, 105, 225); // Royal Blue
        else
            return QColor(99, 184, 255); // Steel Blue 1
    case STATE_PAUSED_DL:
        return QColor(250, 128, 114); // Salmon
    case STATE_PAUSED_UP:
        if (!dark)
            return QColor(0, 0, 139); // Dark Blue
        else
            return QColor(79, 148, 205); // Steel Blue 3
    case STATE_PAUSED_MISSING:
        return QColor(255, 0, 0); // red
    case STATE_QUEUED_DL:
    case STATE_QUEUED_UP:
    case STATE_CHECKING_UP:
    case STATE_CHECKING_DL:
    case STATE_QUEUED_CHECK:
    case STATE_QUEUED_FASTCHECK:
        if (!dark)
            return QColor(0, 128, 128); // Teal
        else
            return QColor(0, 205, 205); // Cyan 3
    case STATE_INVALID:
        return QColor(255, 0, 0); // red
    default:
        Q_ASSERT(false);
        return QColor(255, 0, 0); // red
    }
}

bool TorrentModelItem::setData(int column, const QVariant &value, int role)
{
    qDebug() << Q_FUNC_INFO << column << value;
    if (role != Qt::DisplayRole) return false;
    // Label, seed date and Name columns can be edited
    switch(column) {
    case TR_NAME:
        m_name = value.toString();
        TorrentPersistentData::instance()->saveName(m_torrent.hash(), m_name);
        return true;
    case TR_LABEL: {
        QString new_label = value.toString();
        if (m_label != new_label) {
            QString old_label = m_label;
            m_label = new_label;
            TorrentPersistentData::instance()->saveLabel(m_torrent.hash(), new_label);
            emit labelChanged(old_label, new_label);
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

QVariant TorrentModelItem::data(int column, int role) const
{
    if (role == Qt::DecorationRole && column == TR_NAME) {
        return getIconByState(state());
    }
    if (role == Qt::ForegroundRole) {
        return getColorByState(state());
    }
    if (role != Qt::DisplayRole && role != Qt::UserRole) return QVariant();
    switch(column) {
    case TR_NAME:
        return m_name.isEmpty() ? m_torrent.name() : m_name;
    case TR_PRIORITY: {
        int pos = m_torrent.queue_position(m_lastStatus);
        if (pos > -1)
            return pos - HiddenData::getSize();
        else
            return pos;
    }
    case TR_SIZE:
        return m_lastStatus.has_metadata ? static_cast<qlonglong>(m_lastStatus.total_wanted) : -1;
    case TR_PROGRESS:
        return m_torrent.progress(m_lastStatus);
    case TR_STATUS:
        return state();
    case TR_SEEDS: {
        return (role == Qt::DisplayRole) ? m_lastStatus.num_seeds : m_lastStatus.num_complete;
    }
    case TR_PEERS: {
        return (role == Qt::DisplayRole) ? (m_lastStatus.num_peers-m_lastStatus.num_seeds) : m_lastStatus.num_incomplete;
    }
    case TR_DLSPEED:
        return m_lastStatus.download_payload_rate;
    case TR_UPSPEED:
        return m_lastStatus.upload_payload_rate;
    case TR_ETA: {
        // XXX: Is this correct?
        if (m_torrent.is_paused(m_lastStatus) || m_torrent.is_queued(m_lastStatus)) return MAX_ETA;
        return QBtSession::instance()->getETA(m_hash, m_lastStatus);
    }
    case TR_RATIO:
        return QBtSession::instance()->getRealRatio(m_lastStatus);
    case TR_LABEL:
        return m_label;
    case TR_ADD_DATE:
        return m_addedTime;
    case TR_SEED_DATE:
        return m_lastStatus.completed_time ? QDateTime::fromTime_t(m_lastStatus.completed_time) : QDateTime();
    case TR_TRACKER:
        return misc::toQString(m_lastStatus.current_tracker);
    case TR_DLLIMIT:
        return m_torrent.download_limit();
    case TR_UPLIMIT:
        return m_torrent.upload_limit();
    case TR_AMOUNT_DOWNLOADED:
        return static_cast<qlonglong>(m_lastStatus.all_time_download);
    case TR_AMOUNT_UPLOADED:
        return static_cast<qlonglong>(m_lastStatus.all_time_upload);
    case TR_AMOUNT_DOWNLOADED_SESSION:
        return static_cast<qlonglong>(m_lastStatus.total_payload_download);
    case TR_AMOUNT_UPLOADED_SESSION:
        return static_cast<qlonglong>(m_lastStatus.total_payload_upload);
    case TR_AMOUNT_LEFT:
        return static_cast<qlonglong>(m_lastStatus.total_wanted - m_lastStatus.total_wanted_done);
    case TR_TIME_ELAPSED:
        return (role == Qt::DisplayRole) ? m_lastStatus.active_time : m_lastStatus.seeding_time;
    case TR_SAVE_PATH:
        return fsutils::toNativePath(m_torrent.save_path_parsed());
    case TR_COMPLETED:
        return static_cast<qlonglong>(m_lastStatus.total_wanted_done);
    case TR_RATIO_LIMIT: {
        QString hash = misc::toQString(m_lastStatus.info_hash);
        return QBtSession::instance()->getMaxRatioPerTorrent(hash, NULL);
    }
    case TR_SEEN_COMPLETE_DATE:
        return m_lastStatus.last_seen_complete ? QDateTime::fromTime_t(m_lastStatus.last_seen_complete) : QDateTime();
    case TR_LAST_ACTIVITY:
        if (m_torrent.is_paused(m_lastStatus) || m_torrent.is_checking(m_lastStatus))
            return -1;
        if (m_lastStatus.time_since_upload < m_lastStatus.time_since_download)
            return m_lastStatus.time_since_upload;
        else
            return m_lastStatus.time_since_download;
    case TR_TOTAL_SIZE:
        return m_lastStatus.has_metadata ? static_cast<qlonglong>(m_torrent.total_size()) : -1;
    default:
        return QVariant();
    }
}

QTorrentHandle TorrentModelItem::torrentHandle() const
{
    return m_torrent;
}

// TORRENT MODEL

TorrentModel::TorrentModel(QObject *parent) :
    QAbstractListModel(parent), m_refreshInterval(2000)
{
}

void TorrentModel::populate() {
    // Load the torrents
    std::vector<torrent_handle> torrents = QBtSession::instance()->getSession()->get_torrents();

    std::vector<torrent_handle>::const_iterator it = torrents.begin();
    std::vector<torrent_handle>::const_iterator itend = torrents.end();
    for ( ; it != itend; ++it) {
        const QTorrentHandle h(*it);
        if (HiddenData::hasData(h.hash()))
            continue;
        addTorrent(h);
    }
    // Refresh timer
    connect(&m_refreshTimer, SIGNAL(timeout()), SLOT(forceModelRefresh()));
    m_refreshTimer.start(m_refreshInterval);
    // Listen for torrent changes
    connect(QBtSession::instance(), SIGNAL(addedTorrent(QTorrentHandle)), SLOT(addTorrent(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(torrentAboutToBeRemoved(QTorrentHandle)), SLOT(handleTorrentAboutToBeRemoved(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(finishedTorrent(QTorrentHandle)), SLOT(handleFinishedTorrent(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(metadataReceived(QTorrentHandle)), SLOT(handleTorrentUpdate(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(resumedTorrent(QTorrentHandle)), SLOT(handleTorrentUpdate(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(pausedTorrent(QTorrentHandle)), SLOT(handleTorrentUpdate(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(torrentFinishedChecking(QTorrentHandle)), SLOT(handleTorrentUpdate(QTorrentHandle)));
    connect(QBtSession::instance(), SIGNAL(stateUpdate(std::vector<libtorrent::torrent_status>)), SLOT(stateUpdated(std::vector<libtorrent::torrent_status>)));
}

TorrentModel::~TorrentModel() {
    qDebug() << Q_FUNC_INFO << "ENTER";
    qDeleteAll(m_torrents);
    m_torrents.clear();
    qDebug() << Q_FUNC_INFO << "EXIT";
}

QVariant TorrentModel::headerData(int section, Qt::Orientation orientation,
                                  int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) {
            switch(section) {
            case TorrentModelItem::TR_PRIORITY: return "#";
            case TorrentModelItem::TR_NAME: return tr("Name", "i.e: torrent name");
            case TorrentModelItem::TR_SIZE: return tr("Size", "i.e: torrent size");
            case TorrentModelItem::TR_PROGRESS: return tr("Done", "% Done");
            case TorrentModelItem::TR_STATUS: return tr("Status", "Torrent status (e.g. downloading, seeding, paused)");
            case TorrentModelItem::TR_SEEDS: return tr("Seeds", "i.e. full sources (often untranslated)");
            case TorrentModelItem::TR_PEERS: return tr("Peers", "i.e. partial sources (often untranslated)");
            case TorrentModelItem::TR_DLSPEED: return tr("Down Speed", "i.e: Download speed");
            case TorrentModelItem::TR_UPSPEED: return tr("Up Speed", "i.e: Upload speed");
            case TorrentModelItem::TR_RATIO: return tr("Ratio", "Share ratio");
            case TorrentModelItem::TR_ETA: return tr("ETA", "i.e: Estimated Time of Arrival / Time left");
            case TorrentModelItem::TR_LABEL: return tr("Label");
            case TorrentModelItem::TR_ADD_DATE: return tr("Added On", "Torrent was added to transfer list on 01/01/2010 08:00");
            case TorrentModelItem::TR_SEED_DATE: return tr("Completed On", "Torrent was completed on 01/01/2010 08:00");
            case TorrentModelItem::TR_TRACKER: return tr("Tracker");
            case TorrentModelItem::TR_DLLIMIT: return tr("Down Limit", "i.e: Download limit");
            case TorrentModelItem::TR_UPLIMIT: return tr("Up Limit", "i.e: Upload limit");
            case TorrentModelItem::TR_AMOUNT_DOWNLOADED: return tr("Downloaded", "Amount of data downloaded (e.g. in MB)");
            case TorrentModelItem::TR_AMOUNT_UPLOADED: return tr("Uploaded", "Amount of data uploaded (e.g. in MB)");
            case TorrentModelItem::TR_AMOUNT_DOWNLOADED_SESSION: return tr("Session Download", "Amount of data downloaded since program open (e.g. in MB)");
            case TorrentModelItem::TR_AMOUNT_UPLOADED_SESSION: return tr("Session Upload", "Amount of data uploaded since program open (e.g. in MB)");
            case TorrentModelItem::TR_AMOUNT_LEFT: return tr("Remaining", "Amount of data left to download (e.g. in MB)");
            case TorrentModelItem::TR_TIME_ELAPSED: return tr("Time Active", "Time (duration) the torrent is active (not paused)");
            case TorrentModelItem::TR_SAVE_PATH: return tr("Save path", "Torrent save path");
            case TorrentModelItem::TR_COMPLETED: return tr("Completed", "Amount of data completed (e.g. in MB)");
            case TorrentModelItem::TR_RATIO_LIMIT: return tr("Ratio Limit", "Upload share ratio limit");
            case TorrentModelItem::TR_SEEN_COMPLETE_DATE: return tr("Last Seen Complete", "Indicates the time when the torrent was last seen complete/whole");
            case TorrentModelItem::TR_LAST_ACTIVITY: return tr("Last Activity", "Time passed since a chunk was downloaded/uploaded");
            case TorrentModelItem::TR_TOTAL_SIZE: return tr("Total Size", "i.e. Size including unwanted data");
            default:
                return QVariant();
            }
        }
        else if (role == Qt::TextAlignmentRole) {
            switch(section) {
            case TorrentModelItem::TR_AMOUNT_DOWNLOADED:
            case TorrentModelItem::TR_AMOUNT_UPLOADED:
            case TorrentModelItem::TR_AMOUNT_DOWNLOADED_SESSION:
            case TorrentModelItem::TR_AMOUNT_UPLOADED_SESSION:
            case TorrentModelItem::TR_AMOUNT_LEFT:
            case TorrentModelItem::TR_COMPLETED:
            case TorrentModelItem::TR_SIZE:
            case TorrentModelItem::TR_TOTAL_SIZE:
            case TorrentModelItem::TR_ETA:
            case TorrentModelItem::TR_SEEDS:
            case TorrentModelItem::TR_PEERS:
            case TorrentModelItem::TR_UPSPEED:
            case TorrentModelItem::TR_DLSPEED:
            case TorrentModelItem::TR_UPLIMIT:
            case TorrentModelItem::TR_DLLIMIT:
            case TorrentModelItem::TR_RATIO_LIMIT:
            case TorrentModelItem::TR_RATIO:
            case TorrentModelItem::TR_PRIORITY:
            case TorrentModelItem::TR_LAST_ACTIVITY:
                return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return QAbstractListModel::headerData(section, orientation, role);
            }
        }
    }

    return QVariant();
}

QVariant TorrentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    try {
        if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0 && index.column() < columnCount())
            return m_torrents[index.row()]->data(index.column(), role);
    } catch(invalid_handle&) {}
    return QVariant();
}

bool TorrentModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    qDebug() << Q_FUNC_INFO << value;
    if (!index.isValid() || role != Qt::DisplayRole) return false;
    qDebug("Index is valid and role is DisplayRole");
    try {
        if (index.row() >= 0 && index.row() < rowCount() && index.column() >= 0 && index.column() < columnCount()) {
            bool change = m_torrents[index.row()]->setData(index.column(), value, role);
            if (change)
                notifyTorrentChanged(index.row());
            return change;
        }
    } catch(invalid_handle&) {}
    return false;
}

int TorrentModel::torrentRow(const QString &hash) const
{
    int row = 0;

    QList<TorrentModelItem*>::const_iterator it = m_torrents.constBegin();
    QList<TorrentModelItem*>::const_iterator itend = m_torrents.constEnd();
    for ( ; it != itend; ++it) {
        if ((*it)->hash() == hash) return row;
        ++row;
    }
    return -1;
}

void TorrentModel::addTorrent(const QTorrentHandle &h)
{
    if (torrentRow(h.hash()) < 0) {
        beginInsertTorrent(m_torrents.size());
        TorrentModelItem *item = new TorrentModelItem(h);
        connect(item, SIGNAL(labelChanged(QString,QString)), SLOT(handleTorrentLabelChange(QString,QString)));
        m_torrents << item;
        emit torrentAdded(item);
        endInsertTorrent();
    }
}

void TorrentModel::beginInsertTorrent(int row)
{
    beginInsertRows(QModelIndex(), row, row);
}

void TorrentModel::endInsertTorrent()
{
    endInsertRows();
}

void TorrentModel::beginRemoveTorrent(int row)
{
    beginRemoveRows(QModelIndex(), row, row);
}

void TorrentModel::endRemoveTorrent()
{
    endRemoveRows();
}

void TorrentModel::handleTorrentUpdate(const QTorrentHandle &h)
{
    const int row = torrentRow(h.hash());
    if (row >= 0) {
        // This line changes the torrent name when magnet is retrieved.
        // When magnet link is added, "dn" parameter is used as name, but when metadata is retrieved
        // we change the name with the retrieved torrent name.
        m_torrents[row]->setData(TorrentModelItem::TR_NAME, h.name(), Qt::DisplayRole);

        m_torrents[row]->refreshStatus(h.status(torrent_handle::query_accurate_download_counters));
        notifyTorrentChanged(row);
    }
}

void TorrentModel::handleFinishedTorrent(const QTorrentHandle& h)
{
    const int row = torrentRow(h.hash());
    if (row < 0)
        return;

    // Update completion date
    m_torrents[row]->refreshStatus(h.status(torrent_handle::query_accurate_download_counters));
    notifyTorrentChanged(row);
}

void TorrentModel::notifyTorrentChanged(int row)
{
    emit dataChanged(index(row, 0), index(row, columnCount()-1));
}

void TorrentModel::setRefreshInterval(int refreshInterval)
{
    if (m_refreshInterval != refreshInterval) {
        m_refreshInterval = refreshInterval;
        m_refreshTimer.stop();
        m_refreshTimer.start(m_refreshInterval);
    }
}

void TorrentModel::forceModelRefresh()
{
    QBtSession::instance()->postTorrentUpdate();
}

TorrentStatusReport TorrentModel::getTorrentStatusReport() const
{
    TorrentStatusReport report;

    QList<TorrentModelItem*>::const_iterator it = m_torrents.constBegin();
    QList<TorrentModelItem*>::const_iterator itend = m_torrents.constEnd();
    for ( ; it != itend; ++it) {
        switch((*it)->state()) {
        case TorrentModelItem::STATE_DOWNLOADING:
        case TorrentModelItem::STATE_FORCED_DL:
            ++report.nb_active;
            ++report.nb_downloading;
            break;
        case TorrentModelItem::STATE_DOWNLOADING_META:
            ++report.nb_downloading;
            break;
        case TorrentModelItem::STATE_PAUSED_DL:
        case TorrentModelItem::STATE_PAUSED_MISSING:
            ++report.nb_paused;
        case TorrentModelItem::STATE_STALLED_DL:
        case TorrentModelItem::STATE_CHECKING_DL:
        case TorrentModelItem::STATE_QUEUED_DL: {
            ++report.nb_inactive;
            ++report.nb_downloading;
            break;
        }
        case TorrentModelItem::STATE_SEEDING:
        case TorrentModelItem::STATE_FORCED_UP:
            ++report.nb_active;
            ++report.nb_seeding;
            ++report.nb_completed;
            break;
        case TorrentModelItem::STATE_STALLED_UP:
        case TorrentModelItem::STATE_CHECKING_UP:
        case TorrentModelItem::STATE_QUEUED_UP:
            ++report.nb_seeding;
        case TorrentModelItem::STATE_PAUSED_UP:
            ++report.nb_completed;
            ++report.nb_inactive;
            break;
        default:
            break;
        }
    }
    return report;
}

Qt::ItemFlags TorrentModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    // Explicitely mark as editable
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

void TorrentModel::handleTorrentLabelChange(QString previous, QString current)
{
    emit torrentChangedLabel(static_cast<TorrentModelItem*>(sender()), previous, current);
}

QString TorrentModel::torrentHash(int row) const
{
    if (row >= 0 && row < rowCount())
        return m_torrents.at(row)->hash();
    return QString();
}

void TorrentModel::handleTorrentAboutToBeRemoved(const QTorrentHandle &h)
{
    const int row = torrentRow(h.hash());
    qDebug() << Q_FUNC_INFO << row;
    if (row >= 0) {
        emit torrentAboutToBeRemoved(m_torrents.at(row));

        beginRemoveTorrent(row);
        delete m_torrents[row];
        m_torrents.removeAt(row);
        endRemoveTorrent();
    }
}

void TorrentModel::stateUpdated(const std::vector<libtorrent::torrent_status> &statuses) {
    typedef std::vector<libtorrent::torrent_status> statuses_t;

    for (statuses_t::const_iterator i = statuses.begin(), end = statuses.end(); i != end; ++i) {
        libtorrent::torrent_status const& status = *i;

        const int row = torrentRow(misc::toQString(status.info_hash));
        if (row >= 0) {
            m_torrents[row]->refreshStatus(status);
            notifyTorrentChanged(row);
        }
    }

    emit modelRefreshed();
}

bool TorrentModel::inhibitSystem()
{
    QList<TorrentModelItem*>::const_iterator it = m_torrents.constBegin();
    QList<TorrentModelItem*>::const_iterator itend = m_torrents.constEnd();
    for ( ; it != itend; ++it) {
        switch((*it)->data(TorrentModelItem::TR_STATUS).toInt()) {
        case TorrentModelItem::STATE_DOWNLOADING:
        case TorrentModelItem::STATE_DOWNLOADING_META:
        case TorrentModelItem::STATE_FORCED_DL:
        case TorrentModelItem::STATE_STALLED_DL:
        case TorrentModelItem::STATE_SEEDING:
        case TorrentModelItem::STATE_FORCED_UP:
        case TorrentModelItem::STATE_STALLED_UP:
            return true;
        default:
            break;
        }
    }
    return false;
}

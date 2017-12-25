/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2012, Christophe Dumez
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

#include "btjson.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QVariant>

#include <libtorrent/version.hpp>

#include "base/bittorrent/cachestatus.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/sessionstatus.h"
#include "base/bittorrent/peerinfo.h"
#include "base/bittorrent/torrenthandle.h"
#include "base/bittorrent/trackerentry.h"
#include "base/logger.h"
#include "base/net/geoipmanager.h"
#include "base/preferences.h"
#include "base/torrentfilter.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "jsonutils.h"

#define CACHED_VARIABLE(VARTYPE, VAR, DUR) \
    static VARTYPE VAR; \
    static QElapsedTimer cacheTimer; \
    static bool initialized = false; \
    if (initialized && !cacheTimer.hasExpired(DUR)) \
        return json::toJson(VAR); \
    initialized = true; \
    cacheTimer.start(); \
    VAR = VARTYPE()

#define CACHED_VARIABLE_FOR_HASH(VARTYPE, VAR, DUR, HASH) \
    static VARTYPE VAR; \
    static QString prev_hash; \
    static QElapsedTimer cacheTimer; \
    if (prev_hash == HASH && !cacheTimer.hasExpired(DUR)) \
        return json::toJson(VAR); \
    prev_hash = HASH; \
    cacheTimer.start(); \
    VAR = VARTYPE()


// Numerical constants
static const int CACHE_DURATION_MS = 1500; // 1500ms

// Torrent keys
static const char KEY_TORRENT_HASH[] = "hash";
static const char KEY_TORRENT_NAME[] = "name";
static const char KEY_TORRENT_MAGNET_URI[] = "magnet_uri";
static const char KEY_TORRENT_SIZE[] = "size";
static const char KEY_TORRENT_PROGRESS[] = "progress";
static const char KEY_TORRENT_DLSPEED[] = "dlspeed";
static const char KEY_TORRENT_UPSPEED[] = "upspeed";
static const char KEY_TORRENT_PRIORITY[] = "priority";
static const char KEY_TORRENT_SEEDS[] = "num_seeds";
static const char KEY_TORRENT_NUM_COMPLETE[] = "num_complete";
static const char KEY_TORRENT_LEECHS[] = "num_leechs";
static const char KEY_TORRENT_NUM_INCOMPLETE[] = "num_incomplete";
static const char KEY_TORRENT_RATIO[] = "ratio";
static const char KEY_TORRENT_ETA[] = "eta";
static const char KEY_TORRENT_STATE[] = "state";
static const char KEY_TORRENT_SEQUENTIAL_DOWNLOAD[] = "seq_dl";
static const char KEY_TORRENT_FIRST_LAST_PIECE_PRIO[] = "f_l_piece_prio";
static const char KEY_TORRENT_CATEGORY[] = "category";
static const char KEY_TORRENT_SUPER_SEEDING[] = "super_seeding";
static const char KEY_TORRENT_FORCE_START[] = "force_start";
static const char KEY_TORRENT_SAVE_PATH[] = "save_path";
static const char KEY_TORRENT_ADDED_ON[] = "added_on";
static const char KEY_TORRENT_COMPLETION_ON[] = "completion_on";
static const char KEY_TORRENT_TRACKER[] = "tracker";
static const char KEY_TORRENT_DL_LIMIT[] = "dl_limit";
static const char KEY_TORRENT_UP_LIMIT[] = "up_limit";
static const char KEY_TORRENT_AMOUNT_DOWNLOADED[] = "downloaded";
static const char KEY_TORRENT_AMOUNT_UPLOADED[] = "uploaded";
static const char KEY_TORRENT_AMOUNT_DOWNLOADED_SESSION[] = "downloaded_session";
static const char KEY_TORRENT_AMOUNT_UPLOADED_SESSION[] = "uploaded_session";
static const char KEY_TORRENT_AMOUNT_LEFT[] = "amount_left";
static const char KEY_TORRENT_AMOUNT_COMPLETED[] = "completed";
static const char KEY_TORRENT_RATIO_LIMIT[] = "ratio_limit";
static const char KEY_TORRENT_LAST_SEEN_COMPLETE_TIME[] = "seen_complete";
static const char KEY_TORRENT_LAST_ACTIVITY_TIME[] = "last_activity";
static const char KEY_TORRENT_TOTAL_SIZE[] = "total_size";
static const char KEY_TORRENT_AUTO_TORRENT_MANAGEMENT[] = "auto_tmm";

// Peer keys
static const char KEY_PEER_IP[] = "ip";
static const char KEY_PEER_PORT[] = "port";
static const char KEY_PEER_COUNTRY_CODE[] = "country_code";
static const char KEY_PEER_COUNTRY[] = "country";
static const char KEY_PEER_CLIENT[] = "client";
static const char KEY_PEER_PROGRESS[] = "progress";
static const char KEY_PEER_DOWN_SPEED[] = "dl_speed";
static const char KEY_PEER_UP_SPEED[] = "up_speed";
static const char KEY_PEER_TOT_DOWN[] = "downloaded";
static const char KEY_PEER_TOT_UP[] = "uploaded";
static const char KEY_PEER_CONNECTION_TYPE[] = "connection";
static const char KEY_PEER_FLAGS[] = "flags";
static const char KEY_PEER_FLAGS_DESCRIPTION[] = "flags_desc";
static const char KEY_PEER_RELEVANCE[] = "relevance";
static const char KEY_PEER_FILES[] = "files";

// Tracker keys
static const char KEY_TRACKER_URL[] = "url";
static const char KEY_TRACKER_STATUS[] = "status";
static const char KEY_TRACKER_MSG[] = "msg";
static const char KEY_TRACKER_PEERS[] = "num_peers";

// Web seed keys
static const char KEY_WEBSEED_URL[] = "url";

// Torrent keys (Properties)
static const char KEY_PROP_TIME_ELAPSED[] = "time_elapsed";
static const char KEY_PROP_SEEDING_TIME[] = "seeding_time";
static const char KEY_PROP_ETA[] = "eta";
static const char KEY_PROP_CONNECT_COUNT[] = "nb_connections";
static const char KEY_PROP_CONNECT_COUNT_LIMIT[] = "nb_connections_limit";
static const char KEY_PROP_DOWNLOADED[] = "total_downloaded";
static const char KEY_PROP_DOWNLOADED_SESSION[] = "total_downloaded_session";
static const char KEY_PROP_UPLOADED[] = "total_uploaded";
static const char KEY_PROP_UPLOADED_SESSION[] = "total_uploaded_session";
static const char KEY_PROP_DL_SPEED[] = "dl_speed";
static const char KEY_PROP_DL_SPEED_AVG[] = "dl_speed_avg";
static const char KEY_PROP_UP_SPEED[] = "up_speed";
static const char KEY_PROP_UP_SPEED_AVG[] = "up_speed_avg";
static const char KEY_PROP_DL_LIMIT[] = "dl_limit";
static const char KEY_PROP_UP_LIMIT[] = "up_limit";
static const char KEY_PROP_WASTED[] = "total_wasted";
static const char KEY_PROP_SEEDS[] = "seeds";
static const char KEY_PROP_SEEDS_TOTAL[] = "seeds_total";
static const char KEY_PROP_PEERS[] = "peers";
static const char KEY_PROP_PEERS_TOTAL[] = "peers_total";
static const char KEY_PROP_RATIO[] = "share_ratio";
static const char KEY_PROP_REANNOUNCE[] = "reannounce";
static const char KEY_PROP_TOTAL_SIZE[] = "total_size";
static const char KEY_PROP_PIECES_NUM[] = "pieces_num";
static const char KEY_PROP_PIECE_SIZE[] = "piece_size";
static const char KEY_PROP_PIECES_HAVE[] = "pieces_have";
static const char KEY_PROP_CREATED_BY[] = "created_by";
static const char KEY_PROP_LAST_SEEN[] = "last_seen";
static const char KEY_PROP_ADDITION_DATE[] = "addition_date";
static const char KEY_PROP_COMPLETION_DATE[] = "completion_date";
static const char KEY_PROP_CREATION_DATE[] = "creation_date";
static const char KEY_PROP_SAVE_PATH[] = "save_path";
static const char KEY_PROP_COMMENT[] = "comment";

// File keys
static const char KEY_FILE_NAME[] = "name";
static const char KEY_FILE_SIZE[] = "size";
static const char KEY_FILE_PROGRESS[] = "progress";
static const char KEY_FILE_PRIORITY[] = "priority";
static const char KEY_FILE_IS_SEED[] = "is_seed";
static const char KEY_FILE_PIECE_RANGE[] = "piece_range";

// TransferInfo keys
static const char KEY_TRANSFER_DLSPEED[] = "dl_info_speed";
static const char KEY_TRANSFER_DLDATA[] = "dl_info_data";
static const char KEY_TRANSFER_DLRATELIMIT[] = "dl_rate_limit";
static const char KEY_TRANSFER_UPSPEED[] = "up_info_speed";
static const char KEY_TRANSFER_UPDATA[] = "up_info_data";
static const char KEY_TRANSFER_UPRATELIMIT[] = "up_rate_limit";
static const char KEY_TRANSFER_DHT_NODES[] = "dht_nodes";
static const char KEY_TRANSFER_CONNECTION_STATUS[] = "connection_status";

// Statistics keys
static const char KEY_TRANSFER_ALLTIME_DL[] = "alltime_dl";
static const char KEY_TRANSFER_ALLTIME_UL[] = "alltime_ul";
static const char KEY_TRANSFER_TOTAL_WASTE_SESSION[] = "total_wasted_session";
static const char KEY_TRANSFER_GLOBAL_RATIO[] = "global_ratio";
static const char KEY_TRANSFER_TOTAL_PEER_CONNECTIONS[] = "total_peer_connections";
static const char KEY_TRANSFER_READ_CACHE_HITS[] = "read_cache_hits";
static const char KEY_TRANSFER_TOTAL_BUFFERS_SIZE[] = "total_buffers_size";
static const char KEY_TRANSFER_WRITE_CACHE_OVERLOAD[] = "write_cache_overload";
static const char KEY_TRANSFER_READ_CACHE_OVERLOAD[] = "read_cache_overload";
static const char KEY_TRANSFER_QUEUED_IO_JOBS[] = "queued_io_jobs";
static const char KEY_TRANSFER_AVERAGE_TIME_QUEUE[] = "average_time_queue";
static const char KEY_TRANSFER_TOTAL_QUEUED_SIZE[] = "total_queued_size";

// Sync main data keys
static const char KEY_SYNC_MAINDATA_QUEUEING[] = "queueing";
static const char KEY_SYNC_MAINDATA_USE_ALT_SPEED_LIMITS[] = "use_alt_speed_limits";
static const char KEY_SYNC_MAINDATA_REFRESH_INTERVAL[] = "refresh_interval";

// Sync torrent peers keys
static const char KEY_SYNC_TORRENT_PEERS_SHOW_FLAGS[] = "show_flags";

static const char KEY_FULL_UPDATE[] = "full_update";
static const char KEY_RESPONSE_ID[] = "rid";
static const char KEY_SUFFIX_REMOVED[] = "_removed";

// Log keys
static const char KEY_LOG_ID[] = "id";
static const char KEY_LOG_TIMESTAMP[] = "timestamp";
static const char KEY_LOG_MSG_TYPE[] = "type";
static const char KEY_LOG_MSG_MESSAGE[] = "message";
static const char KEY_LOG_PEER_IP[] = "ip";
static const char KEY_LOG_PEER_BLOCKED[] = "blocked";
static const char KEY_LOG_PEER_REASON[] = "reason";

namespace
{
    QString torrentStateToString(const BitTorrent::TorrentState state)
    {
        switch (state) {
        case BitTorrent::TorrentState::Error:
            return QLatin1String("error");
        case BitTorrent::TorrentState::MissingFiles:
            return QLatin1String("missingFiles");
        case BitTorrent::TorrentState::Uploading:
            return QLatin1String("uploading");
        case BitTorrent::TorrentState::PausedUploading:
            return QLatin1String("pausedUP");
        case BitTorrent::TorrentState::QueuedUploading:
            return QLatin1String("queuedUP");
        case BitTorrent::TorrentState::StalledUploading:
            return QLatin1String("stalledUP");
        case BitTorrent::TorrentState::CheckingUploading:
            return QLatin1String("checkingUP");
        case BitTorrent::TorrentState::ForcedUploading:
            return QLatin1String("forcedUP");
        case BitTorrent::TorrentState::Allocating:
            return QLatin1String("allocating");
        case BitTorrent::TorrentState::Downloading:
            return QLatin1String("downloading");
        case BitTorrent::TorrentState::DownloadingMetadata:
            return QLatin1String("metaDL");
        case BitTorrent::TorrentState::PausedDownloading:
            return QLatin1String("pausedDL");
        case BitTorrent::TorrentState::QueuedDownloading:
            return QLatin1String("queuedDL");
        case BitTorrent::TorrentState::StalledDownloading:
            return QLatin1String("stalledDL");
        case BitTorrent::TorrentState::CheckingDownloading:
            return QLatin1String("checkingDL");
        case BitTorrent::TorrentState::ForcedDownloading:
            return QLatin1String("forcedDL");
#if LIBTORRENT_VERSION_NUM < 10100
        case BitTorrent::TorrentState::QueuedForChecking:
            return QLatin1String("queuedForChecking");
#endif
        case BitTorrent::TorrentState::CheckingResumeData:
            return QLatin1String("checkingResumeData");
        default:
            return QLatin1String("unknown");
        }
    }

    class QTorrentCompare
    {
    public:
        QTorrentCompare(const QString &key, bool greaterThan = false)
            : m_key(key)
            , m_greaterThan(greaterThan)
        {
        }

        bool operator()(const QVariant &torrent1, const QVariant &torrent2)
        {
            return m_greaterThan
                ? (torrent1.toMap().value(m_key) > torrent2.toMap().value(m_key))
                : (torrent1.toMap().value(m_key) < torrent2.toMap().value(m_key));
        }

    private:
        const QString m_key;
        const bool m_greaterThan;
    };

    QVariantMap getTranserInfoMap();
    QVariantMap toMap(BitTorrent::TorrentHandle *const torrent);
    void processMap(const QVariantMap &prevData, const QVariantMap &data, QVariantMap &syncData);
    void processHash(QVariantHash prevData, const QVariantHash &data, QVariantMap &syncData, QVariantList &removedItems);
    void processList(QVariantList prevData, const QVariantList &data, QVariantList &syncData, QVariantList &removedItems);
    QVariantMap generateSyncData(int acceptedResponseId, const QVariantMap &data, QVariantMap &lastAcceptedData, QVariantMap &lastData);

    QVariantMap getTranserInfoMap()
    {
        QVariantMap map;
        const BitTorrent::SessionStatus &sessionStatus = BitTorrent::Session::instance()->status();
        const BitTorrent::CacheStatus &cacheStatus = BitTorrent::Session::instance()->cacheStatus();
        map[KEY_TRANSFER_DLSPEED] = sessionStatus.payloadDownloadRate;
        map[KEY_TRANSFER_DLDATA] = sessionStatus.totalPayloadDownload;
        map[KEY_TRANSFER_UPSPEED] = sessionStatus.payloadUploadRate;
        map[KEY_TRANSFER_UPDATA] = sessionStatus.totalPayloadUpload;
        map[KEY_TRANSFER_DLRATELIMIT] = BitTorrent::Session::instance()->downloadSpeedLimit();
        map[KEY_TRANSFER_UPRATELIMIT] = BitTorrent::Session::instance()->uploadSpeedLimit();

        quint64 atd = BitTorrent::Session::instance()->getAlltimeDL();
        quint64 atu = BitTorrent::Session::instance()->getAlltimeUL();
        map[KEY_TRANSFER_ALLTIME_DL] = atd;
        map[KEY_TRANSFER_ALLTIME_UL] = atu;
        map[KEY_TRANSFER_TOTAL_WASTE_SESSION] = sessionStatus.totalWasted;
        map[KEY_TRANSFER_GLOBAL_RATIO] = ( atd > 0 && atu > 0 ) ? Utils::String::fromDouble(static_cast<qreal>(atu) / atd, 2) : "-";
        map[KEY_TRANSFER_TOTAL_PEER_CONNECTIONS] = sessionStatus.peersCount;

        qreal readRatio = cacheStatus.readRatio;
        map[KEY_TRANSFER_READ_CACHE_HITS] = (readRatio >= 0) ? Utils::String::fromDouble(100 * readRatio, 2) : "-";
        map[KEY_TRANSFER_TOTAL_BUFFERS_SIZE] = cacheStatus.totalUsedBuffers * 16 * 1024;

        // num_peers is not reliable (adds up peers, which didn't even overcome tcp handshake)
        quint32 peers = 0;
        foreach (BitTorrent::TorrentHandle *const torrent, BitTorrent::Session::instance()->torrents())
            peers += torrent->peersCount();
        map[KEY_TRANSFER_WRITE_CACHE_OVERLOAD] = ((sessionStatus.diskWriteQueue > 0) && (peers > 0)) ? Utils::String::fromDouble((100. * sessionStatus.diskWriteQueue) / peers, 2) : "0";
        map[KEY_TRANSFER_READ_CACHE_OVERLOAD] = ((sessionStatus.diskReadQueue > 0) && (peers > 0)) ? Utils::String::fromDouble((100. * sessionStatus.diskReadQueue) / peers, 2) : "0";

        map[KEY_TRANSFER_QUEUED_IO_JOBS] = cacheStatus.jobQueueLength;
        map[KEY_TRANSFER_AVERAGE_TIME_QUEUE] = cacheStatus.averageJobTime;
        map[KEY_TRANSFER_TOTAL_QUEUED_SIZE] = cacheStatus.queuedBytes;

        map[KEY_TRANSFER_DHT_NODES] = sessionStatus.dhtNodes;
        if (!BitTorrent::Session::instance()->isListening())
            map[KEY_TRANSFER_CONNECTION_STATUS] = "disconnected";
        else
            map[KEY_TRANSFER_CONNECTION_STATUS] = sessionStatus.hasIncomingConnections ? "connected" : "firewalled";
        return map;
    }

    QVariantMap toMap(BitTorrent::TorrentHandle *const torrent)
    {
        QVariantMap ret;
        ret[KEY_TORRENT_HASH] = QString(torrent->hash());
        ret[KEY_TORRENT_NAME] = torrent->name();
        ret[KEY_TORRENT_MAGNET_URI] = torrent->toMagnetUri();
        ret[KEY_TORRENT_SIZE] = torrent->wantedSize();
        ret[KEY_TORRENT_PROGRESS] = torrent->progress();
        ret[KEY_TORRENT_DLSPEED] = torrent->downloadPayloadRate();
        ret[KEY_TORRENT_UPSPEED] = torrent->uploadPayloadRate();
        ret[KEY_TORRENT_PRIORITY] = torrent->queuePosition();
        ret[KEY_TORRENT_SEEDS] = torrent->seedsCount();
        ret[KEY_TORRENT_NUM_COMPLETE] = torrent->totalSeedsCount();
        ret[KEY_TORRENT_LEECHS] = torrent->leechsCount();
        ret[KEY_TORRENT_NUM_INCOMPLETE] = torrent->totalLeechersCount();
        const qreal ratio = torrent->realRatio();
        ret[KEY_TORRENT_RATIO] = (ratio > BitTorrent::TorrentHandle::MAX_RATIO) ? -1 : ratio;
        ret[KEY_TORRENT_STATE] = torrentStateToString(torrent->state());
        ret[KEY_TORRENT_ETA] = torrent->eta();
        ret[KEY_TORRENT_SEQUENTIAL_DOWNLOAD] = torrent->isSequentialDownload();
        if (torrent->hasMetadata())
            ret[KEY_TORRENT_FIRST_LAST_PIECE_PRIO] = torrent->hasFirstLastPiecePriority();
        ret[KEY_TORRENT_CATEGORY] = torrent->category();
        ret[KEY_TORRENT_SUPER_SEEDING] = torrent->superSeeding();
        ret[KEY_TORRENT_FORCE_START] = torrent->isForced();
        ret[KEY_TORRENT_SAVE_PATH] = Utils::Fs::toNativePath(torrent->savePath());
        ret[KEY_TORRENT_ADDED_ON] = torrent->addedTime().toTime_t();
        ret[KEY_TORRENT_COMPLETION_ON] = torrent->completedTime().toTime_t();
        ret[KEY_TORRENT_TRACKER] = torrent->currentTracker();
        ret[KEY_TORRENT_DL_LIMIT] = torrent->downloadLimit();
        ret[KEY_TORRENT_UP_LIMIT] = torrent->uploadLimit();
        ret[KEY_TORRENT_AMOUNT_DOWNLOADED] = torrent->totalDownload();
        ret[KEY_TORRENT_AMOUNT_UPLOADED] = torrent->totalUpload();
        ret[KEY_TORRENT_AMOUNT_DOWNLOADED_SESSION] = torrent->totalPayloadDownload();
        ret[KEY_TORRENT_AMOUNT_UPLOADED_SESSION] = torrent->totalPayloadUpload();
        ret[KEY_TORRENT_AMOUNT_LEFT] = torrent->incompletedSize();
        ret[KEY_TORRENT_AMOUNT_COMPLETED] = torrent->completedSize();
        ret[KEY_TORRENT_RATIO_LIMIT] = torrent->maxRatio();
        ret[KEY_TORRENT_LAST_SEEN_COMPLETE_TIME] = torrent->lastSeenComplete().toTime_t();
        ret[KEY_TORRENT_AUTO_TORRENT_MANAGEMENT] = torrent->isAutoTMMEnabled();

        if (torrent->isPaused() || torrent->isChecking())
            ret[KEY_TORRENT_LAST_ACTIVITY_TIME] = 0;
        else {
            QDateTime dt = QDateTime::currentDateTime();
            dt = dt.addSecs(-torrent->timeSinceActivity());
            ret[KEY_TORRENT_LAST_ACTIVITY_TIME] = dt.toTime_t();
        }

        ret[KEY_TORRENT_TOTAL_SIZE] = torrent->totalSize();

        return ret;
    }

    // Compare two structures (prevData, data) and calculate difference (syncData).
    // Structures encoded as map.
    void processMap(const QVariantMap &prevData, const QVariantMap &data, QVariantMap &syncData)
    {
        // initialize output variable
        syncData.clear();

        QVariantList removedItems;
        foreach (QString key, data.keys()) {
            removedItems.clear();

            switch (static_cast<QMetaType::Type>(data[key].type())) {
            case QMetaType::QVariantMap: {
                    QVariantMap map;
                    processMap(prevData[key].toMap(), data[key].toMap(), map);
                    if (!map.isEmpty())
                        syncData[key] = map;
                }
                break;
            case QMetaType::QVariantHash: {
                    QVariantMap map;
                    processHash(prevData[key].toHash(), data[key].toHash(), map, removedItems);
                    if (!map.isEmpty())
                        syncData[key] = map;
                    if (!removedItems.isEmpty())
                        syncData[key + KEY_SUFFIX_REMOVED] = removedItems;
                }
                break;
            case QMetaType::QVariantList: {
                    QVariantList list;
                    processList(prevData[key].toList(), data[key].toList(), list, removedItems);
                    if (!list.isEmpty())
                        syncData[key] = list;
                    if (!removedItems.isEmpty())
                        syncData[key + KEY_SUFFIX_REMOVED] = removedItems;
                }
                break;
            case QMetaType::QString:
            case QMetaType::LongLong:
            case QMetaType::Float:
            case QMetaType::Int:
            case QMetaType::Bool:
            case QMetaType::Double:
            case QMetaType::ULongLong:
            case QMetaType::UInt:
            case QMetaType::QDateTime:
                if (prevData[key] != data[key])
                    syncData[key] = data[key];
                break;
            default:
                Q_ASSERT_X(false, "processMap"
                           , QString("Unexpected type: %1")
                           .arg(QMetaType::typeName(static_cast<QMetaType::Type>(data[key].type())))
                           .toUtf8().constData());
            }
        }
    }

    // Compare two lists of structures (prevData, data) and calculate difference (syncData, removedItems).
    // Structures encoded as map.
    // Lists are encoded as hash table (indexed by structure key value) to improve ease of searching for removed items.
    void processHash(QVariantHash prevData, const QVariantHash &data, QVariantMap &syncData, QVariantList &removedItems)
    {
        // initialize output variables
        syncData.clear();
        removedItems.clear();

        if (prevData.isEmpty()) {
            // If list was empty before, then difference is a whole new list.
            foreach (QString key, data.keys())
                syncData[key] = data[key];
        }
        else {
            foreach (QString key, data.keys()) {
                switch (data[key].type()) {
                case QVariant::Map:
                    if (!prevData.contains(key)) {
                        // new list item found - append it to syncData
                        syncData[key] = data[key];
                    }
                    else {
                        QVariantMap map;
                        processMap(prevData[key].toMap(), data[key].toMap(), map);
                        // existing list item found - remove it from prevData
                        prevData.remove(key);
                        if (!map.isEmpty())
                            // changed list item found - append its changes to syncData
                            syncData[key] = map;
                    }
                    break;
                default:
                    Q_ASSERT(0);
                }
            }

            if (!prevData.isEmpty()) {
                // prevData contains only items that are missing now -
                // put them in removedItems
                foreach (QString s, prevData.keys())
                    removedItems << s;
            }
        }
    }

    // Compare two lists of simple value (prevData, data) and calculate difference (syncData, removedItems).
    void processList(QVariantList prevData, const QVariantList &data, QVariantList &syncData, QVariantList &removedItems)
    {
        // initialize output variables
        syncData.clear();
        removedItems.clear();

        if (prevData.isEmpty()) {
            // If list was empty before, then difference is a whole new list.
            syncData = data;
        }
        else {
            foreach (QVariant item, data) {
                if (!prevData.contains(item))
                    // new list item found - append it to syncData
                    syncData.append(item);
                else
                    // unchanged list item found - remove it from prevData
                    prevData.removeOne(item);
            }

            if (!prevData.isEmpty())
                // prevData contains only items that are missing now -
                // put them in removedItems
                removedItems = prevData;
        }
    }

    QVariantMap generateSyncData(int acceptedResponseId, const QVariantMap &data, QVariantMap &lastAcceptedData, QVariantMap &lastData)
    {
        QVariantMap syncData;
        bool fullUpdate = true;
        int lastResponseId = 0;
        if (acceptedResponseId > 0) {
            lastResponseId = lastData[KEY_RESPONSE_ID].toInt();

            if (lastResponseId == acceptedResponseId)
                lastAcceptedData = lastData;

            int lastAcceptedResponseId = lastAcceptedData[KEY_RESPONSE_ID].toInt();

            if (lastAcceptedResponseId == acceptedResponseId) {
                processMap(lastAcceptedData, data, syncData);
                fullUpdate = false;
            }
        }

        if (fullUpdate) {
            lastAcceptedData.clear();
            syncData = data;
            syncData[KEY_FULL_UPDATE] = true;
        }

        lastResponseId = lastResponseId % 1000000 + 1;  // cycle between 1 and 1000000
        lastData = data;
        lastData[KEY_RESPONSE_ID] = lastResponseId;
        syncData[KEY_RESPONSE_ID] = lastResponseId;

        return syncData;
    }
}

/**
 * Returns all the torrents in JSON format.
 *
 * The return value is a JSON-formatted list of dictionaries.
 * The dictionary keys are:
 *   - "hash": Torrent hash
 *   - "name": Torrent name
 *   - "size": Torrent size
 *   - "progress: Torrent progress
 *   - "dlspeed": Torrent download speed
 *   - "upspeed": Torrent upload speed
 *   - "priority": Torrent priority (-1 if queuing is disabled)
 *   - "num_seeds": Torrent seeds connected to
 *   - "num_complete": Torrent seeds in the swarm
 *   - "num_leechs": Torrent leechers connected to
 *   - "num_incomplete": Torrent leechers in the swarm
 *   - "ratio": Torrent share ratio
 *   - "eta": Torrent ETA
 *   - "state": Torrent state
 *   - "seq_dl": Torrent sequential download state
 *   - "f_l_piece_prio": Torrent first last piece priority state
 *   - "force_start": Torrent force start state
 *   - "category": Torrent category
 */
QByteArray btjson::getTorrents(QString filter, QString category,
                               QString sortedColumn, bool reverse, int limit, int offset)
{
    QVariantList torrentList;
    TorrentFilter torrentFilter(filter, TorrentFilter::AnyHash, category);
    foreach (BitTorrent::TorrentHandle *const torrent, BitTorrent::Session::instance()->torrents()) {
        if (torrentFilter.match(torrent))
            torrentList.append(toMap(torrent));
    }

    std::sort(torrentList.begin(), torrentList.end(), QTorrentCompare(sortedColumn, reverse));
    int size = torrentList.size();
    // normalize offset
    if (offset < 0)
        offset = size + offset;
    if ((offset >= size) || (offset < 0))
        offset = 0;
    // normalize limit
    if (limit <= 0)
        limit = -1; // unlimited

    if ((limit > 0) || (offset > 0))
        return json::toJson(torrentList.mid(offset, limit));
    else
        return json::toJson(torrentList);
}

/**
 * The function returns the changed data from the server to synchronize with the web client.
 * Return value is map in JSON format.
 * Map contain the key:
 *  - "Rid": ID response
 * Map can contain the keys:
 *  - "full_update": full data update flag
 *  - "torrents": dictionary contains information about torrents.
 *  - "torrents_removed": a list of hashes of removed torrents
 *  - "categories": list of categories
 *  - "categories_removed": list of removed categories
 *  - "server_state": map contains information about the state of the server
 * The keys of the 'torrents' dictionary are hashes of torrents.
 * Each value of the 'torrents' dictionary contains map. The map can contain following keys:
 *  - "name": Torrent name
 *  - "size": Torrent size
 *  - "progress: Torrent progress
 *  - "dlspeed": Torrent download speed
 *  - "upspeed": Torrent upload speed
 *  - "priority": Torrent priority (-1 if queuing is disabled)
 *  - "num_seeds": Torrent seeds connected to
 *  - "num_complete": Torrent seeds in the swarm
 *  - "num_leechs": Torrent leechers connected to
 *  - "num_incomplete": Torrent leechers in the swarm
 *  - "ratio": Torrent share ratio
 *  - "eta": Torrent ETA
 *  - "state": Torrent state
 *  - "seq_dl": Torrent sequential download state
 *  - "f_l_piece_prio": Torrent first last piece priority state
 *  - "completion_on": Torrent copletion time
 *  - "tracker": Torrent tracker
 *  - "dl_limit": Torrent download limit
 *  - "up_limit": Torrent upload limit
 *  - "downloaded": Amount of data downloaded
 *  - "uploaded": Amount of data uploaded
 *  - "downloaded_session": Amount of data downloaded since program open
 *  - "uploaded_session": Amount of data uploaded since program open
 *  - "amount_left": Amount of data left to download
 *  - "save_path": Torrent save path
 *  - "completed": Amount of data completed
 *  - "ratio_limit": Upload share ratio limit
 *  - "seen_complete": Indicates the time when the torrent was last seen complete/whole
 *  - "last_activity": Last time when a chunk was downloaded/uploaded
 *  - "total_size": Size including unwanted data
 * Server state map may contain the following keys:
 *  - "connection_status": connection status
 *  - "dht_nodes": DHT nodes count
 *  - "dl_info_data": bytes downloaded
 *  - "dl_info_speed": download speed
 *  - "dl_rate_limit: download rate limit
 *  - "up_info_data: bytes uploaded
 *  - "up_info_speed: upload speed
 *  - "up_rate_limit: upload speed limit
 *  - "queueing": priority system usage flag
 *  - "refresh_interval": torrents table refresh interval
 */
QByteArray btjson::getSyncMainData(int acceptedResponseId, QVariantMap &lastData, QVariantMap &lastAcceptedData)
{
    QVariantMap data;
    QVariantHash torrents;

    BitTorrent::Session *const session = BitTorrent::Session::instance();

    foreach (BitTorrent::TorrentHandle *const torrent, session->torrents()) {
        QVariantMap map = toMap(torrent);
        map.remove(KEY_TORRENT_HASH);

        // Calculated last activity time can differ from actual value by up to 10 seconds (this is a libtorrent issue).
        // So we don't need unnecessary updates of last activity time in response.
        if (lastData.contains("torrents") && lastData["torrents"].toHash().contains(torrent->hash()) &&
                lastData["torrents"].toHash()[torrent->hash()].toMap().contains(KEY_TORRENT_LAST_ACTIVITY_TIME)) {
            uint lastValue = lastData["torrents"].toHash()[torrent->hash()].toMap()[KEY_TORRENT_LAST_ACTIVITY_TIME].toUInt();
            if (qAbs(static_cast<int>(lastValue - map[KEY_TORRENT_LAST_ACTIVITY_TIME].toUInt())) < 15)
                map[KEY_TORRENT_LAST_ACTIVITY_TIME] = lastValue;
        }

        torrents[torrent->hash()] = map;
    }

    data["torrents"] = torrents;

    QVariantList categories;
    foreach (const QString &category, session->categories().keys())
        categories << category;

    data["categories"] = categories;

    QVariantMap serverState = getTranserInfoMap();
    serverState[KEY_SYNC_MAINDATA_QUEUEING] = session->isQueueingSystemEnabled();
    serverState[KEY_SYNC_MAINDATA_USE_ALT_SPEED_LIMITS] = session->isAltGlobalSpeedLimitEnabled();
    serverState[KEY_SYNC_MAINDATA_REFRESH_INTERVAL] = session->refreshInterval();
    data["server_state"] = serverState;

    return json::toJson(generateSyncData(acceptedResponseId, data, lastAcceptedData, lastData));
}

QByteArray btjson::getSyncTorrentPeersData(int acceptedResponseId, QString hash, QVariantMap &lastData, QVariantMap &lastAcceptedData)
{
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    QVariantMap data;
    QVariantHash peers;
    QList<BitTorrent::PeerInfo> peersList = torrent->peers();
#ifndef DISABLE_COUNTRIES_RESOLUTION
    bool resolvePeerCountries = Preferences::instance()->resolvePeerCountries();
#else
    bool resolvePeerCountries = false;
#endif

    data[KEY_SYNC_TORRENT_PEERS_SHOW_FLAGS] = resolvePeerCountries;

    foreach (const BitTorrent::PeerInfo &pi, peersList) {
        if (pi.address().ip.isNull()) continue;
        QVariantMap peer;
#ifndef DISABLE_COUNTRIES_RESOLUTION
        if (resolvePeerCountries) {
            peer[KEY_PEER_COUNTRY_CODE] = pi.country().toLower();
            peer[KEY_PEER_COUNTRY] = Net::GeoIPManager::CountryName(pi.country());
        }
#endif
        peer[KEY_PEER_IP] = pi.address().ip.toString();
        peer[KEY_PEER_PORT] = pi.address().port;
        peer[KEY_PEER_CLIENT] = pi.client();
        peer[KEY_PEER_PROGRESS] = pi.progress();
        peer[KEY_PEER_DOWN_SPEED] = pi.payloadDownSpeed();
        peer[KEY_PEER_UP_SPEED] = pi.payloadUpSpeed();
        peer[KEY_PEER_TOT_DOWN] = pi.totalDownload();
        peer[KEY_PEER_TOT_UP] = pi.totalUpload();
        peer[KEY_PEER_CONNECTION_TYPE] = pi.connectionType();
        peer[KEY_PEER_FLAGS] = pi.flags();
        peer[KEY_PEER_FLAGS_DESCRIPTION] = pi.flagsDescription();
        peer[KEY_PEER_RELEVANCE] = pi.relevance();
        peer[KEY_PEER_FILES] = torrent->info().filesForPiece(pi.downloadingPieceIndex()).join(QLatin1String("\n"));

        peers[pi.address().ip.toString() + ":" + QString::number(pi.address().port)] = peer;
    }

    data["peers"] = peers;

    return json::toJson(generateSyncData(acceptedResponseId, data, lastAcceptedData, lastData));
}

/**
 * Returns the trackers for a torrent in JSON format.
 *
 * The return value is a JSON-formatted list of dictionaries.
 * The dictionary keys are:
 *   - "url": Tracker URL
 *   - "status": Tracker status
 *   - "num_peers": Tracker peer count
 *   - "msg": Tracker message (last)
 */
QByteArray btjson::getTrackersForTorrent(const QString& hash)
{
    CACHED_VARIABLE_FOR_HASH(QVariantList, trackerList, CACHE_DURATION_MS, hash);
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    QHash<QString, BitTorrent::TrackerInfo> trackers_data = torrent->trackerInfos();
    foreach (const BitTorrent::TrackerEntry &tracker, torrent->trackers()) {
        QVariantMap trackerDict;
        trackerDict[KEY_TRACKER_URL] = tracker.url();
        const BitTorrent::TrackerInfo data = trackers_data.value(tracker.url());
        QString status;
        switch (tracker.status()) {
        case BitTorrent::TrackerEntry::NotContacted:
            status = tr("Not contacted yet"); break;
        case BitTorrent::TrackerEntry::Updating:
            status = tr("Updating..."); break;
        case BitTorrent::TrackerEntry::Working:
            status = tr("Working"); break;
        case BitTorrent::TrackerEntry::NotWorking:
            status = tr("Not working"); break;
        }
        trackerDict[KEY_TRACKER_STATUS] = status;
        trackerDict[KEY_TRACKER_PEERS] = data.numPeers;
        trackerDict[KEY_TRACKER_MSG] = data.lastMessage.trimmed();

        trackerList.append(trackerDict);
    }

    return json::toJson(trackerList);
}

/**
 * Returns the web seeds for a torrent in JSON format.
 *
 * The return value is a JSON-formatted list of dictionaries.
 * The dictionary keys are:
 *   - "url": Web seed URL
 */
QByteArray btjson::getWebSeedsForTorrent(const QString& hash)
{
    CACHED_VARIABLE_FOR_HASH(QVariantList, webSeedList, CACHE_DURATION_MS, hash);
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    foreach (const QUrl &webseed, torrent->urlSeeds()) {
        QVariantMap webSeedDict;
        webSeedDict[KEY_WEBSEED_URL] = webseed.toString();
        webSeedList.append(webSeedDict);
    }

    return json::toJson(webSeedList);
}

/**
 * Returns the properties for a torrent in JSON format.
 *
 * The return value is a JSON-formatted dictionary.
 * The dictionary keys are:
 *   - "time_elapsed": Torrent elapsed time
 *   - "seeding_time": Torrent elapsed time while complete
 *   - "eta": Torrent ETA
 *   - "nb_connections": Torrent connection count
 *   - "nb_connections_limit": Torrent connection count limit
 *   - "total_downloaded": Total data uploaded for torrent
 *   - "total_downloaded_session": Total data downloaded this session
 *   - "total_uploaded": Total data uploaded for torrent
 *   - "total_uploaded_session": Total data uploaded this session
 *   - "dl_speed": Torrent download speed
 *   - "dl_speed_avg": Torrent average download speed
 *   - "up_speed": Torrent upload speed
 *   - "up_speed_avg": Torrent average upload speed
 *   - "dl_limit": Torrent download limit
 *   - "up_limit": Torrent upload limit
 *   - "total_wasted": Total data wasted for torrent
 *   - "seeds": Torrent connected seeds
 *   - "seeds_total": Torrent total number of seeds
 *   - "peers": Torrent connected peers
 *   - "peers_total": Torrent total number of peers
 *   - "share_ratio": Torrent share ratio
 *   - "reannounce": Torrent next reannounce time
 *   - "total_size": Torrent total size
 *   - "pieces_num": Torrent pieces count
 *   - "piece_size": Torrent piece size
 *   - "pieces_have": Torrent pieces have
 *   - "created_by": Torrent creator
 *   - "last_seen": Torrent last seen complete
 *   - "addition_date": Torrent addition date
 *   - "completion_date": Torrent completion date
 *   - "creation_date": Torrent creation date
 *   - "save_path": Torrent save path
 *   - "comment": Torrent comment
 */
QByteArray btjson::getPropertiesForTorrent(const QString& hash)
{
    CACHED_VARIABLE_FOR_HASH(QVariantMap, dataDict, CACHE_DURATION_MS, hash);
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    dataDict[KEY_PROP_TIME_ELAPSED] = torrent->activeTime();
    dataDict[KEY_PROP_SEEDING_TIME] = torrent->seedingTime();
    dataDict[KEY_PROP_ETA] = torrent->eta();
    dataDict[KEY_PROP_CONNECT_COUNT] = torrent->connectionsCount();
    dataDict[KEY_PROP_CONNECT_COUNT_LIMIT] = torrent->connectionsLimit();
    dataDict[KEY_PROP_DOWNLOADED] = torrent->totalDownload();
    dataDict[KEY_PROP_DOWNLOADED_SESSION] = torrent->totalPayloadDownload();
    dataDict[KEY_PROP_UPLOADED] = torrent->totalUpload();
    dataDict[KEY_PROP_UPLOADED_SESSION] = torrent->totalPayloadUpload();
    dataDict[KEY_PROP_DL_SPEED] = torrent->downloadPayloadRate();
    dataDict[KEY_PROP_DL_SPEED_AVG] = torrent->totalDownload() / (1 + torrent->activeTime() - torrent->finishedTime());
    dataDict[KEY_PROP_UP_SPEED] = torrent->uploadPayloadRate();
    dataDict[KEY_PROP_UP_SPEED_AVG] = torrent->totalUpload() / (1 + torrent->activeTime());
    dataDict[KEY_PROP_DL_LIMIT] = torrent->downloadLimit() <= 0 ? -1 : torrent->downloadLimit();
    dataDict[KEY_PROP_UP_LIMIT] = torrent->uploadLimit() <= 0 ? -1 : torrent->uploadLimit();
    dataDict[KEY_PROP_WASTED] = torrent->wastedSize();
    dataDict[KEY_PROP_SEEDS] = torrent->seedsCount();
    dataDict[KEY_PROP_SEEDS_TOTAL] = torrent->totalSeedsCount();
    dataDict[KEY_PROP_PEERS] = torrent->leechsCount();
    dataDict[KEY_PROP_PEERS_TOTAL] = torrent->totalLeechersCount();
    const qreal ratio = torrent->realRatio();
    dataDict[KEY_PROP_RATIO] = ratio > BitTorrent::TorrentHandle::MAX_RATIO ? -1 : ratio;
    dataDict[KEY_PROP_REANNOUNCE] = torrent->nextAnnounce();
    dataDict[KEY_PROP_TOTAL_SIZE] = torrent->totalSize();
    dataDict[KEY_PROP_PIECES_NUM] = torrent->piecesCount();
    dataDict[KEY_PROP_PIECE_SIZE] = torrent->pieceLength();
    dataDict[KEY_PROP_PIECES_HAVE] = torrent->piecesHave();
    dataDict[KEY_PROP_CREATED_BY] = torrent->creator();
    dataDict[KEY_PROP_ADDITION_DATE] = torrent->addedTime().toTime_t();
    if (torrent->hasMetadata()) {
        dataDict[KEY_PROP_LAST_SEEN] = torrent->lastSeenComplete().isValid() ? static_cast<int>(torrent->lastSeenComplete().toTime_t()) : -1;
        dataDict[KEY_PROP_COMPLETION_DATE] = torrent->completedTime().isValid() ? static_cast<int>(torrent->completedTime().toTime_t()) : -1;
        dataDict[KEY_PROP_CREATION_DATE] = torrent->creationDate().toTime_t();
    }
    else {
        dataDict[KEY_PROP_LAST_SEEN] = -1;
        dataDict[KEY_PROP_COMPLETION_DATE] = -1;
        dataDict[KEY_PROP_CREATION_DATE] = -1;
    }
    dataDict[KEY_PROP_SAVE_PATH] = Utils::Fs::toNativePath(torrent->savePath());
    dataDict[KEY_PROP_COMMENT] = torrent->comment();

    return json::toJson(dataDict);
}

/**
 * Returns the files in a torrent in JSON format.
 *
 * The return value is a JSON-formatted list of dictionaries.
 * The dictionary keys are:
 *   - "name": File name
 *   - "size": File size
 *   - "progress": File progress
 *   - "priority": File priority
 *   - "is_seed": Flag indicating if torrent is seeding/complete
 *   - "piece_range": Piece index range, the first number is the starting piece index
 *        and the second number is the ending piece index (inclusive)
 */
QByteArray btjson::getFilesForTorrent(const QString& hash)
{
    CACHED_VARIABLE_FOR_HASH(QVariantList, fileList, CACHE_DURATION_MS, hash);
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    if (!torrent->hasMetadata())
        return json::toJson(fileList);

    const QVector<int> priorities = torrent->filePriorities();
    const QVector<qreal> fp = torrent->filesProgress();
    const BitTorrent::TorrentInfo info = torrent->info();
    for (int i = 0; i < torrent->filesCount(); ++i) {
        QVariantMap fileDict;
        fileDict[KEY_FILE_PROGRESS] = fp[i];
        fileDict[KEY_FILE_PRIORITY] = priorities[i];
        fileDict[KEY_FILE_SIZE] = torrent->fileSize(i);

        QString fileName = torrent->filePath(i);
        if (fileName.endsWith(QB_EXT, Qt::CaseInsensitive))
            fileName.chop(QB_EXT.size());
        fileDict[KEY_FILE_NAME] = Utils::Fs::toNativePath(fileName);

        const BitTorrent::TorrentInfo::PieceRange idx = info.filePieces(i);
        fileDict[KEY_FILE_PIECE_RANGE] = QVariantList {idx.first(), idx.last()};

        if (i == 0)
            fileDict[KEY_FILE_IS_SEED] = torrent->isSeed();

        fileList.append(fileDict);
    }

    return json::toJson(fileList);
}

/**
 * Returns an array of hashes (of each pieces respectively) for a torrent in JSON format.
 *
 * The return value is a JSON-formatted array of strings (hex strings).
 */
QByteArray btjson::getPieceHashesForTorrent(const QString &hash)
{
    CACHED_VARIABLE_FOR_HASH(QVariantList, pieceHashes, CACHE_DURATION_MS, hash);
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    const QVector<QByteArray> hashes = torrent->info().pieceHashes();
    pieceHashes.reserve(hashes.size());
    foreach (const QByteArray &hash, hashes)
        pieceHashes.append(hash.toHex());

    return json::toJson(pieceHashes);
}

/**
 * Returns an array of states (of each pieces respectively) for a torrent in JSON format.
 *
 * The return value is a JSON-formatted array of ints.
 * 0: piece not downloaded
 * 1: piece requested or downloading
 * 2: piece already downloaded
 */
QByteArray btjson::getPieceStatesForTorrent(const QString &hash)
{
    CACHED_VARIABLE_FOR_HASH(QVariantList, pieceStates, CACHE_DURATION_MS, hash);
    BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
    if (!torrent) {
        qWarning() << Q_FUNC_INFO << "Invalid torrent " << qPrintable(hash);
        return QByteArray();
    }

    const QBitArray states = torrent->pieces();
    pieceStates.reserve(states.size());
    for (int i = 0; i < states.size(); ++i)
        pieceStates.append(static_cast<int>(states[i]) * 2);

    const QBitArray dlstates = torrent->downloadingPieces();
    for (int i = 0; i < states.size(); ++i) {
        if (dlstates[i])
            pieceStates[i] = 1;
    }

    return json::toJson(pieceStates);
}

/**
 * Returns the global transfer information in JSON format.
 *
 * The return value is a JSON-formatted dictionary.
 * The dictionary keys are:
 *   - "dl_info_speed": Global download rate
 *   - "dl_info_data": Data downloaded this session
 *   - "up_info_speed": Global upload rate
 *   - "up_info_data": Data uploaded this session
 *   - "dl_rate_limit": Download rate limit
 *   - "up_rate_limit": Upload rate limit
 *   - "dht_nodes": DHT nodes connected to
 *   - "connection_status": Connection status
 */
QByteArray btjson::getTransferInfo()
{
    return json::toJson(getTranserInfoMap());
}

QByteArray btjson::getTorrentsRatesLimits(QStringList &hashes, bool downloadLimits)
{
    QVariantMap map;

    foreach (const QString &hash, hashes) {
        int limit = -1;
        BitTorrent::TorrentHandle *const torrent = BitTorrent::Session::instance()->findTorrent(hash);
        if (torrent)
            limit = downloadLimits ? torrent->downloadLimit() : torrent->uploadLimit();
        map[hash] = limit;
    }

    return json::toJson(map);
}

/**
 * Returns the log in JSON format.
 *
 * The return value is an array of dictionaries.
 * The dictionary keys are:
 *   - "id": id of the message
 *   - "timestamp": milliseconds since epoch
 *   - "type": type of the message (int, see MsgType)
 *   - "message": text of the message
 */
QByteArray btjson::getLog(bool normal, bool info, bool warning, bool critical, int lastKnownId)
{
    Logger* const logger = Logger::instance();
    QVariantList msgList;

    foreach (const Log::Msg& msg, logger->getMessages(lastKnownId)) {
        if (!((msg.type == Log::NORMAL && normal)
              || (msg.type == Log::INFO && info)
              || (msg.type == Log::WARNING && warning)
              || (msg.type == Log::CRITICAL && critical)))
            continue;
        QVariantMap map;
        map[KEY_LOG_ID] = msg.id;
        map[KEY_LOG_TIMESTAMP] = msg.timestamp;
        map[KEY_LOG_MSG_TYPE] = msg.type;
        map[KEY_LOG_MSG_MESSAGE] = msg.message;
        msgList.append(map);
    }

    return json::toJson(msgList);
}

/**
 * Returns the peer log in JSON format.
 *
 * The return value is an array of dictionaries.
 * The dictionary keys are:
 *   - "id": id of the message
 *   - "timestamp": milliseconds since epoch
 *   - "ip": IP of the peer
 *   - "blocked": whether or not the peer was blocked
 *   - "reason": reason of the block
 */
QByteArray btjson::getPeerLog(int lastKnownId)
{
    Logger* const logger = Logger::instance();
    QVariantList peerList;

    foreach (const Log::Peer& peer, logger->getPeers(lastKnownId)) {
        QVariantMap map;
        map[KEY_LOG_ID] = peer.id;
        map[KEY_LOG_TIMESTAMP] = peer.timestamp;
        map[KEY_LOG_PEER_IP] = peer.ip;
        map[KEY_LOG_PEER_BLOCKED] = peer.blocked;
        map[KEY_LOG_PEER_REASON] = peer.reason;
        peerList.append(map);
    }

    return json::toJson(peerList);
}

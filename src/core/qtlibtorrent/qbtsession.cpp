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
#include <QDir>
#include <QDateTime>
#include <QString>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QProcess>
#include <QCoreApplication>

#include "smtp.h"
#include "filesystemwatcher.h"
#include "torrentspeedmonitor.h"
#include "torrentstatistics.h"
#include "qbtsession.h"
#include "alertdispatcher.h"
#include "misc.h"
#include "fs_utils.h"
#include "downloadthread.h"
#include "filterparserthread.h"
#include "preferences.h"
#include "scannedfoldersmodel.h"
#include "qtracker.h"
#include "logger.h"
#include "unicodestrings.h"
#ifndef DISABLE_GUI
#include "shutdownconfirm.h"
#include "geoipmanager.h"
#endif
#include "torrentpersistentdata.h"
#include "bandwidthscheduler.h"
#include <libtorrent/version.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/extensions/lt_trackers.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
//#include <libtorrent/extensions/metadata_transfer.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/identify_client.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <queue>
#include <string.h>

#if LIBTORRENT_VERSION_NUM < 10000
#include <libtorrent/upnp.hpp>
#include <libtorrent/natpmp.hpp>
#endif

using namespace libtorrent;

QBtSession* QBtSession::m_instance = 0;
const qreal QBtSession::MAX_RATIO = 9999.;

const int MAX_TRACKER_ERRORS = 2;

/* Converts a QString hash into a libtorrent sha1_hash */
static libtorrent::sha1_hash QStringToSha1(const QString& s) {
    QByteArray raw = s.toLatin1();
    Q_ASSERT(raw.size() == 40);
    libtorrent::sha1_hash ret;
    from_hex(raw.constData(), 40, (char*)&ret[0]);
    return ret;
}

// Main constructor
QBtSession::QBtSession()
    : m_scanFolders(ScanFoldersModel::instance(this)),
      preAllocateAll(false), global_ratio_limit(-1),
      LSDEnabled(false),
      DHTEnabled(false), queueingEnabled(false),
      m_torrentExportEnabled(false),
      m_finishedTorrentExportEnabled(false)
#ifndef DISABLE_GUI
    , geoipDBLoaded(false), resolve_countries(false)
#endif
    , m_tracker(0), m_shutdownAct(NO_SHUTDOWN)
#if LIBTORRENT_VERSION_NUM < 10000
    , m_upnp(0), m_natpmp(0)
#endif
    , m_alertDispatcher(0)
{
    BigRatioTimer = new QTimer(this);
    BigRatioTimer->setInterval(10000);
    connect(BigRatioTimer, SIGNAL(timeout()), SLOT(processBigRatios()));
    Preferences* const pref = Preferences::instance();
    // Creating Bittorrent session
    QList<int> version;
    version << VERSION_MAJOR;
    version << VERSION_MINOR;
    version << VERSION_BUGFIX;
    version << VERSION_BUILD;
    const QString peer_id = "qB";
    // Construct session
    s = new session(fingerprint(peer_id.toLocal8Bit().constData(), version.at(0), version.at(1), version.at(2), version.at(3)), 0);
    //std::cout << "Peer ID: " << fingerprint(peer_id.toLocal8Bit().constData(), version.at(0), version.at(1), version.at(2), version.at(3)).to_string() << std::endl;
    Logger::instance()->addMessage(tr("Peer ID: ")+misc::toQString(fingerprint(peer_id.toLocal8Bit().constData(), version.at(0), version.at(1), version.at(2), version.at(3)).to_string()));

    // Set severity level of libtorrent session
    s->set_alert_mask(alert::error_notification | alert::peer_notification | alert::port_mapping_notification | alert::storage_notification | alert::tracker_notification | alert::status_notification | alert::ip_block_notification | alert::progress_notification | alert::stats_notification);
    // Load previous state
    loadSessionState();
    // Enabling plugins
    //s->add_extension(&create_metadata_plugin);
    s->add_extension(&create_ut_metadata_plugin);
    if (pref->trackerExchangeEnabled())
        s->add_extension(&create_lt_trackers_plugin);
    if (pref->isPeXEnabled()) {
        PeXEnabled = true;
        s->add_extension(&create_ut_pex_plugin);
    } else {
        PeXEnabled = false;
    }
    s->add_extension(&create_smart_ban_plugin);
    m_alertDispatcher = new QAlertDispatcher(s, this);
    connect(m_alertDispatcher, SIGNAL(alertsReceived()), SLOT(readAlerts()));
    appendLabelToSavePath = pref->appendTorrentLabel();
    appendqBExtension = pref->useIncompleteFilesExtension();
    connect(m_scanFolders, SIGNAL(torrentsAdded(QStringList&)), SLOT(addTorrentsFromScanFolder(QStringList&)));
    // Apply user settings to Bittorrent session
    configureSession();
    connect(pref, SIGNAL(changed()), SLOT(configureSession()));
    // Torrent speed monitor
    m_speedMonitor = new TorrentSpeedMonitor(this);
    m_torrentStatistics = new TorrentStatistics(this, this);
    // To download from urls
    downloader = new DownloadThread(this);
    connect(downloader, SIGNAL(downloadFinished(QString, QString)), SLOT(processDownloadedFile(QString, QString)));
    connect(downloader, SIGNAL(downloadFailure(QString, QString)), SLOT(handleDownloadFailure(QString, QString)));
    connect(downloader, SIGNAL(magnetRedirect(QString, QString)), SLOT(handleMagnetRedirect(QString, QString)));
    // Regular saving of fastresume data
    connect(&resumeDataTimer, SIGNAL(timeout()), SLOT(saveTempFastResumeData()));
    resumeDataTimer.start(pref->saveResumeDataInterval() * 60 * 1000);
    qDebug("* BTSession constructed");
}

// Main destructor
QBtSession::~QBtSession() {
    qDebug("BTSession destructor IN");
    delete m_speedMonitor;
    qDebug("Deleted the torrent speed monitor");
    // Do some BT related saving
    saveSessionState();
    saveFastResumeData();
    // Delete our objects
    if (m_tracker)
        delete m_tracker;
    if (BigRatioTimer)
        delete BigRatioTimer;
    if (filterParser)
        delete filterParser;
    delete downloader;
    if (bd_scheduler)
        delete bd_scheduler;
    delete m_alertDispatcher;
    delete m_torrentStatistics;
    qDebug("Deleting the session");
    delete s;
    qDebug("BTSession destructor OUT");
#ifndef DISABLE_GUI
    if (m_shutdownAct != NO_SHUTDOWN) {
        qDebug() << "Sending computer shutdown/suspend/hibernate signal...";
        misc::shutdownComputer(m_shutdownAct);
    }
#endif
}

void QBtSession::preAllocateAllFiles(bool b) {
    const bool change = (preAllocateAll != b);
    if (change) {
        qDebug("PreAllocateAll changed, reloading all torrents!");
        preAllocateAll = b;
    }
}

void QBtSession::processBigRatios() {
    qDebug("Process big ratios...");
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        const QTorrentHandle h(*torrentIT);
        if (!h.is_valid()) continue;
        if (h.is_seed()) {
            const QString hash = h.hash();
            const qreal ratio = getRealRatio(h.status(torrent_handle::query_accurate_download_counters));
            qreal ratio_limit = TorrentPersistentData::instance()->getRatioLimit(hash);
            if (ratio_limit == TorrentPersistentData::USE_GLOBAL_RATIO)
                ratio_limit = global_ratio_limit;
            if (ratio_limit == TorrentPersistentData::NO_RATIO_LIMIT)
                continue;
            qDebug("Ratio: %f (limit: %f)", ratio, ratio_limit);
            Q_ASSERT(ratio_limit >= 0.f);
            if (ratio <= MAX_RATIO && ratio >= ratio_limit) {
                Logger* const logger = Logger::instance();
                if (high_ratio_action == REMOVE_ACTION) {
                    logger->addMessage(tr("%1 reached the maximum ratio you set.").arg(h.name()));
                    logger->addMessage(tr("Removing torrent %1...").arg(h.name()));
                    deleteTorrent(hash);
                } else {
                    // Pause it
                    if (!h.is_paused()) {
                        logger->addMessage(tr("%1 reached the maximum ratio you set.").arg(h.name()));
                        logger->addMessage(tr("Pausing torrent %1...").arg(h.name()));
                        pauseTorrent(hash);
                    }
                }
                //emit torrent_ratio_deleted(fileName);
            }
        }
    }
}

void QBtSession::setDownloadLimit(QString hash, long val) {
    QTorrentHandle h = getTorrentHandle(hash);
    if (h.is_valid()) {
        h.set_download_limit(val);
    }
}

void QBtSession::setUploadLimit(QString hash, long val) {
    qDebug("Set upload limit rate to %ld", val);
    QTorrentHandle h = getTorrentHandle(hash);
    if (h.is_valid()) {
        h.set_upload_limit(val);
    }
}

void QBtSession::handleDownloadFailure(QString url, QString reason) {
    emit downloadFromUrlFailure(url, reason);
    // Clean up
    const QUrl qurl = QUrl::fromEncoded(url.toUtf8());
    url_skippingDlg.removeOne(qurl);
    savepathLabel_fromurl.remove(qurl);
#ifndef DISABLE_GUI
    addpaused_fromurl.remove(qurl);
#endif
}

void QBtSession::handleMagnetRedirect(const QString &url_new, const QString &url_old) {
    if (url_skippingDlg.contains(url_old)) {
        url_skippingDlg.removeOne(url_old);
        QPair<QString, QString> savePath_label;
        if (savepathLabel_fromurl.contains(url_old)) {
            savePath_label = savepathLabel_fromurl.take(url_old);
        }
#ifndef DISABLE_GUI
        RssDownloadRule::AddPausedState state = RssDownloadRule::USE_GLOBAL;
        if (addpaused_fromurl.contains(url_old)) {
            state = addpaused_fromurl.take(url_old);
        }
#endif
        addMagnetSkipAddDlg(url_new, savePath_label.first, savePath_label.second,
#ifndef DISABLE_GUI
                            state,
#endif
                            url_old);
    }
    else
        addMagnetInteractive(url_new);
}

void QBtSession::setQueueingEnabled(bool enable) {
    if (queueingEnabled != enable) {
        qDebug("Queueing system is changing state...");
        queueingEnabled = enable;
    }
}

// Set BT session configuration
void QBtSession::configureSession() {
    qDebug("Configuring session");
    Preferences* const pref = Preferences::instance();

    const unsigned short old_listenPort = getListenPort();
    const unsigned short new_listenPort = pref->getSessionPort();
    if (old_listenPort != new_listenPort) {
        qDebug("Session port changes in program preferences: %d -> %d", old_listenPort, new_listenPort);
        setListeningPort(new_listenPort);
    }

    // Downloads
    // * Save path
    defaultSavePath = pref->getSavePath();
    if (pref->isTempPathEnabled()) {
        setDefaultTempPath(pref->getTempPath());
    } else {
        setDefaultTempPath(QString::null);
    }
    setAppendLabelToSavePath(pref->appendTorrentLabel());
    setAppendqBExtension(pref->useIncompleteFilesExtension());
    preAllocateAllFiles(pref->preAllocateAllFiles());
    // * Torrent export directory
    const bool torrentExportEnabled = pref->isTorrentExportEnabled();
    if (m_torrentExportEnabled != torrentExportEnabled) {
        m_torrentExportEnabled = torrentExportEnabled;
        if (m_torrentExportEnabled) {
            qDebug("Torrent export is enabled, exporting the current torrents");
            exportTorrentFiles(pref->getTorrentExportDir());
        }
    }
    // * Finished Torrent export directory
    const bool finishedTorrentExportEnabled = pref->isFinishedTorrentExportEnabled();
    if (m_finishedTorrentExportEnabled != finishedTorrentExportEnabled)
        m_finishedTorrentExportEnabled = finishedTorrentExportEnabled;
    // Connection
    // * Global download limit
    const bool alternative_speeds = pref->isAltBandwidthEnabled();
    int down_limit;
    if (alternative_speeds)
        down_limit = pref->getAltGlobalDownloadLimit();
    else
        down_limit = pref->getGlobalDownloadLimit();
    if (down_limit <= 0) {
        // Download limit disabled
        setDownloadRateLimit(-1);
    } else {
        // Enabled
        setDownloadRateLimit(down_limit*1024);
    }
    int up_limit;
    if (alternative_speeds)
        up_limit = pref->getAltGlobalUploadLimit();
    else
        up_limit = pref->getGlobalUploadLimit();
    // * Global Upload limit
    if (up_limit <= 0) {
        // Upload limit disabled
        setUploadRateLimit(-1);
    } else {
        // Enabled
        setUploadRateLimit(up_limit*1024);
    }
    if (pref->isSchedulerEnabled()) {
        if (!bd_scheduler) {
            bd_scheduler = new BandwidthScheduler(this);
            connect(bd_scheduler, SIGNAL(switchToAlternativeMode(bool)), this, SLOT(useAlternativeSpeedsLimit(bool)));
        }
        bd_scheduler->start();
    } else {
        delete bd_scheduler;
    }
#ifndef DISABLE_GUI
    // Resolve countries
    qDebug("Loading country resolution settings");
    const bool new_resolv_countries = pref->resolvePeerCountries();
    if (resolve_countries != new_resolv_countries) {
        qDebug("in country resolution settings");
        resolve_countries = new_resolv_countries;
        if (resolve_countries && !geoipDBLoaded) {
            qDebug("Loading geoip database");
            GeoIPManager::loadDatabase(s);
            geoipDBLoaded = true;
        }
        // Update torrent handles
        std::vector<torrent_handle> torrents = s->get_torrents();

        std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
        std::vector<torrent_handle>::iterator torrentITend = torrents.end();
        for ( ; torrentIT != torrentITend; ++torrentIT) {
            QTorrentHandle h = QTorrentHandle(*torrentIT);
            if (h.is_valid())
                h.resolve_countries(resolve_countries);
        }
    }
#endif
    // * UPnP / NAT-PMP
    Logger* const logger = Logger::instance();
    if (pref->isUPnPEnabled()) {
        enableUPnP(true);
        logger->addMessage(tr("UPnP / NAT-PMP support [ON]"), Log::INFO);
    } else {
        enableUPnP(false);
        logger->addMessage(tr("UPnP / NAT-PMP support [OFF]"), Log::INFO);
    }
    // * Session settings
    session_settings sessionSettings = s->settings();
    sessionSettings.user_agent = "qBittorrent " VERSION;
    logger->addMessage(tr("HTTP User-Agent is %1").arg(misc::toQString(sessionSettings.user_agent)));

    sessionSettings.apply_ip_filter_to_trackers = pref->isFilteringTrackerEnabled();
    sessionSettings.upnp_ignore_nonrouters = true;
    sessionSettings.use_dht_as_fallback = false;
    // Disable support for SSL torrents for now
    sessionSettings.ssl_listen = 0;
    // To prevent ISPs from blocking seeding
    sessionSettings.lazy_bitfields = true;
    // Speed up exit
    sessionSettings.stop_tracker_timeout = 1;
    //sessionSettings.announce_to_all_trackers = true;
    sessionSettings.auto_scrape_interval = 1200; // 20 minutes
    bool announce_to_all = pref->announceToAllTrackers();
    sessionSettings.announce_to_all_trackers = announce_to_all;
    sessionSettings.announce_to_all_tiers = announce_to_all;
    sessionSettings.auto_scrape_min_interval = 900; // 15 minutes
    int cache_size = pref->diskCacheSize();
    sessionSettings.cache_size = cache_size ? cache_size * 64 : -1;
    sessionSettings.cache_expiry = pref->diskCacheTTL();
    qDebug() << "Using a disk cache size of" << cache_size << "MiB";
    session_settings::io_buffer_mode_t mode = pref->osCache() ? session_settings::enable_os_cache : session_settings::disable_os_cache;
    sessionSettings.disk_io_read_mode = mode;
    sessionSettings.disk_io_write_mode = mode;
    resumeDataTimer.setInterval(pref->saveResumeDataInterval() * 60 * 1000);
    sessionSettings.anonymous_mode = pref->isAnonymousModeEnabled();
    if (sessionSettings.anonymous_mode) {
        logger->addMessage(tr("Anonymous mode [ON]"), Log::INFO);
    } else {
        logger->addMessage(tr("Anonymous mode [OFF]"), Log::INFO);
    }
    // Queueing System
    if (pref->isQueueingSystemEnabled()) {
        int max_downloading = pref->getMaxActiveDownloads();
        int max_active = pref->getMaxActiveTorrents();

        if (max_downloading > -1)
            sessionSettings.active_downloads = max_downloading + HiddenData::getDownloadingSize();
        else
            sessionSettings.active_downloads = max_downloading;

        if (max_active > -1)
            sessionSettings.active_limit = max_active + HiddenData::getDownloadingSize();
        else
            sessionSettings.active_limit = max_active;

        sessionSettings.active_seeds = pref->getMaxActiveUploads();
        sessionSettings.dont_count_slow_torrents = pref->ignoreSlowTorrentsForQueueing();
        setQueueingEnabled(true);
    } else {
        sessionSettings.active_downloads = -1;
        sessionSettings.active_seeds = -1;
        sessionSettings.active_limit = -1;
        setQueueingEnabled(false);
    }
    sessionSettings.active_tracker_limit = -1;
    sessionSettings.active_dht_limit = -1;
    sessionSettings.active_lsd_limit = -1;
    // Outgoing ports
    sessionSettings.outgoing_ports = std::make_pair(pref->outgoingPortsMin(), pref->outgoingPortsMax());
    // Ignore limits on LAN
    qDebug() << "Ignore limits on LAN" << pref->getIgnoreLimitsOnLAN();
    sessionSettings.ignore_limits_on_local_network = pref->getIgnoreLimitsOnLAN();
    // Include overhead in transfer limits
    sessionSettings.rate_limit_ip_overhead = pref->includeOverheadInLimits();
    // IP address to announce to trackers
    QString announce_ip = pref->getNetworkAddress();
    if (!announce_ip.isEmpty())
        sessionSettings.announce_ip = announce_ip.toStdString();
    // Super seeding
    sessionSettings.strict_super_seeding = pref->isSuperSeedingEnabled();
    // * Max Half-open connections
    sessionSettings.half_open_limit = pref->getMaxHalfOpenConnections();
    // * Max connections limit
    sessionSettings.connections_limit = pref->getMaxConnecs();
    // * Global max upload slots
    sessionSettings.unchoke_slots_limit = pref->getMaxUploads();
    // uTP
    sessionSettings.enable_incoming_utp = pref->isuTPEnabled();
    sessionSettings.enable_outgoing_utp = pref->isuTPEnabled();
    // uTP rate limiting
    sessionSettings.rate_limit_utp = pref->isuTPRateLimited();
    if (sessionSettings.rate_limit_utp)
        sessionSettings.mixed_mode_algorithm = session_settings::prefer_tcp;
    else
        sessionSettings.mixed_mode_algorithm = session_settings::peer_proportional;
    sessionSettings.connection_speed = 20; //default is 10
#if LIBTORRENT_VERSION_NUM >= 10000
    if (pref->isProxyEnabled())
        sessionSettings.force_proxy = pref->getForceProxy();
    else
        sessionSettings.force_proxy = false;
#endif
    sessionSettings.no_connect_privileged_ports = false;
    sessionSettings.seed_choking_algorithm = session_settings::fastest_upload;
    qDebug() << "Settings SessionSettings";
    setSessionSettings(sessionSettings);
    // Bittorrent
    // * Max connections per torrent limit
    setMaxConnectionsPerTorrent(pref->getMaxConnecsPerTorrent());
    // * Max uploads per torrent limit
    setMaxUploadsPerTorrent(pref->getMaxUploadsPerTorrent());
    // * DHT
    enableDHT(pref->isDHTEnabled());
    // * PeX
    if (PeXEnabled) {
        logger->addMessage(tr("PeX support [ON]"), Log::INFO);
    } else {
        logger->addMessage(tr("PeX support [OFF]"), Log::CRITICAL);
    }
    if (PeXEnabled != pref->isPeXEnabled()) {
        logger->addMessage(tr("Restart is required to toggle PeX support"), Log::CRITICAL);
    }
    // * LSD
    if (pref->isLSDEnabled()) {
        enableLSD(true);
        logger->addMessage(tr("Local Peer Discovery support [ON]"), Log::INFO);
    } else {
        enableLSD(false);
        logger->addMessage(tr("Local Peer Discovery support [OFF]"), Log::INFO);
    }
    // * Encryption
    const int encryptionState = pref->getEncryptionSetting();
    // The most secure, rc4 only so that all streams and encrypted
    pe_settings encryptionSettings;
    encryptionSettings.allowed_enc_level = pe_settings::rc4;
    encryptionSettings.prefer_rc4 = true;
    switch(encryptionState) {
    case 0: //Enabled
        encryptionSettings.out_enc_policy = pe_settings::enabled;
        encryptionSettings.in_enc_policy = pe_settings::enabled;
        logger->addMessage(tr("Encryption support [ON]"), Log::INFO);
        break;
    case 1: // Forced
        encryptionSettings.out_enc_policy = pe_settings::forced;
        encryptionSettings.in_enc_policy = pe_settings::forced;
        logger->addMessage(tr("Encryption support [FORCED]"), Log::INFO);
        break;
    default: // Disabled
        encryptionSettings.out_enc_policy = pe_settings::disabled;
        encryptionSettings.in_enc_policy = pe_settings::disabled;
        logger->addMessage(tr("Encryption support [OFF]"), Log::INFO);
    }
    applyEncryptionSettings(encryptionSettings);
    // * Maximum ratio
    high_ratio_action = pref->getMaxRatioAction();
    setGlobalMaxRatio(pref->getGlobalMaxRatio());
    updateRatioTimer();
    // Ip Filter
    FilterParserThread::processFilterList(s, pref->bannedIPs());
    if (pref->isFilteringEnabled()) {
        enableIPFilter(pref->getFilter());
    }else{
        disableIPFilter();
    }
    // * Proxy settings
    proxy_settings proxySettings;
    if (pref->isProxyEnabled()) {
        qDebug("Enabling P2P proxy");
        proxySettings.hostname = pref->getProxyIp().toStdString();
        qDebug("hostname is %s", proxySettings.hostname.c_str());
        proxySettings.port = pref->getProxyPort();
        qDebug("port is %d", proxySettings.port);
        if (pref->isProxyAuthEnabled()) {
            proxySettings.username = pref->getProxyUsername().toStdString();
            proxySettings.password = pref->getProxyPassword().toStdString();
            qDebug("username is %s", proxySettings.username.c_str());
            qDebug("password is %s", proxySettings.password.c_str());
        }
    }
    switch(pref->getProxyType()) {
    case Proxy::HTTP:
        qDebug("type: http");
        proxySettings.type = proxy_settings::http;
        break;
    case Proxy::HTTP_PW:
        qDebug("type: http_pw");
        proxySettings.type = proxy_settings::http_pw;
        break;
    case Proxy::SOCKS4:
        proxySettings.type = proxy_settings::socks4;
        break;
    case Proxy::SOCKS5:
        qDebug("type: socks5");
        proxySettings.type = proxy_settings::socks5;
        break;
    case Proxy::SOCKS5_PW:
        qDebug("type: socks5_pw");
        proxySettings.type = proxy_settings::socks5_pw;
        break;
    default:
        proxySettings.type = proxy_settings::none;
    }
    setProxySettings(proxySettings);
    // Tracker
    if (pref->isTrackerEnabled()) {
        if (!m_tracker) {
            m_tracker = new QTracker(this);
        }
        if (m_tracker->start()) {
            logger->addMessage(tr("Embedded Tracker [ON]"), Log::INFO);
        } else {
            logger->addMessage(tr("Failed to start the embedded tracker!"), Log::CRITICAL);
        }
    } else {
        logger->addMessage(tr("Embedded Tracker [OFF]"));
        if (m_tracker)
            delete m_tracker;
    }
    // * Scan dirs
    const QStringList scan_dirs = pref->getScanDirs();
    QList<bool> downloadInDirList = pref->getDownloadInScanDirs();
    while(scan_dirs.size() > downloadInDirList.size()) {
        downloadInDirList << false;
    }
    int i = 0;
    foreach (const QString &dir, scan_dirs) {
        qDebug() << "Adding scan dir" << dir << downloadInDirList.at(i);
        m_scanFolders->addPath(dir, downloadInDirList.at(i));
        ++i;
    }
    qDebug("Session configured");
}

void QBtSession::useAlternativeSpeedsLimit(bool alternative) {
    qDebug() << Q_FUNC_INFO << alternative;
    // Save new state to remember it on startup
    Preferences* const pref = Preferences::instance();
    // Stop the scheduler when the user has manually changed the bandwidth mode
    if (!pref->isSchedulerEnabled())
        delete bd_scheduler;
    pref->setAltBandwidthEnabled(alternative);
    // Apply settings to the bittorrent session
    int down_limit = alternative ? pref->getAltGlobalDownloadLimit() : pref->getGlobalDownloadLimit();
    if (down_limit <= 0) {
        down_limit = -1;
    } else {
        down_limit *= 1024;
    }
    setDownloadRateLimit(down_limit);
    // Upload rate
    int up_limit = alternative ? pref->getAltGlobalUploadLimit() : pref->getGlobalUploadLimit();
    if (up_limit <= 0) {
        up_limit = -1;
    } else {
        up_limit *= 1024;
    }
    setUploadRateLimit(up_limit);
    // Notify
    emit alternativeSpeedsModeChanged(alternative);
}

// Return the torrent handle, given its hash
QTorrentHandle QBtSession::getTorrentHandle(const QString &hash) const {
    return QTorrentHandle(s->find_torrent(QStringToSha1(hash)));
}

bool QBtSession::hasActiveTorrents() const {
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        const QTorrentHandle h(*torrentIT);
        if (h.is_valid() && !h.is_paused() && !h.is_queued())
            return true;
    }
    return false;
}

bool QBtSession::hasDownloadingTorrents() const {
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        if (torrentIT->is_valid()) {
            try {
                const torrent_status status = torrentIT->status();
                if (status.state != torrent_status::finished && status.state != torrent_status::seeding
                    && !(status.paused && !status.auto_managed))
                    return true;
            } catch(std::exception) {}
        }
    }
    return false;
}

void QBtSession::banIP(QString ip) {
    FilterParserThread::processFilterList(s, QStringList(ip));
    Preferences::instance()->banIP(ip);
}

// Delete a torrent from the session, given its hash
// permanent = true means that the torrent will be removed from the hard-drive too
void QBtSession::deleteTorrent(const QString &hash, bool delete_local_files) {
    qDebug("Deleting torrent with hash: %s", qPrintable(hash));
    const QTorrentHandle h = getTorrentHandle(hash);
    if (!h.is_valid()) {
        qDebug("/!\\ Error: Invalid handle");
        return;
    }
    emit torrentAboutToBeRemoved(h);
    qDebug("h is valid, getting name or hash...");
    QString fileName;
    if (h.has_metadata())
        fileName = h.name();
    else
        fileName = h.hash();
    // Remove it from session
    if (delete_local_files) {
        if (h.has_metadata()) {
            QDir save_dir(h.save_path());
            if (save_dir != QDir(defaultSavePath) && (defaultTempPath.isEmpty() || save_dir != QDir(defaultTempPath))) {
                savePathsToRemove[hash] = save_dir.absolutePath();
                qDebug() << "Save path to remove (async): " << save_dir.absolutePath();
            }
        }
        s->remove_torrent(h, session::delete_files);
    } else {
        QStringList uneeded_files;
        if (h.has_metadata())
            uneeded_files = h.absolute_files_path_uneeded();
        s->remove_torrent(h);
        // Remove unneeded and incomplete files
        foreach (const QString &uneeded_file, uneeded_files) {
            qDebug("Removing uneeded file: %s", qPrintable(uneeded_file));
            fsutils::forceRemove(uneeded_file);
            const QString parent_folder = fsutils::branchPath(uneeded_file);
            qDebug("Attempt to remove parent folder (if empty): %s", qPrintable(parent_folder));
            QDir().rmpath(parent_folder);
        }
    }
    // Remove it from torrent backup directory
    QDir torrentBackup(fsutils::BTBackupLocation());
    QStringList filters;
    filters << hash+".*";
    const QStringList files = torrentBackup.entryList(filters, QDir::Files, QDir::Unsorted);
    foreach (const QString &file, files) {
        fsutils::forceRemove(torrentBackup.absoluteFilePath(file));
    }
    TorrentPersistentData::instance()->deletePersistentData(hash);
    TorrentTempData::deleteTempData(hash);
    HiddenData::deleteData(hash);
    // Remove tracker errors
    trackersInfos.remove(hash);
    if (delete_local_files)
        Logger::instance()->addMessage(tr("'%1' was removed from transfer list and hard disk.", "'xxx.avi' was removed...").arg(fileName));
    else
        Logger::instance()->addMessage(tr("'%1' was removed from transfer list.", "'xxx.avi' was removed...").arg(fileName));
    qDebug("Torrent deleted.");
}

void QBtSession::pauseAllTorrents() {
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        try {
            QTorrentHandle h = QTorrentHandle(*torrentIT);
            if (!h.is_paused()) {
                h.pause();
                emit pausedTorrent(h);
            }
        } catch(invalid_handle&) {}
    }
}

std::vector<torrent_handle> QBtSession::getTorrents() const {
    return s->get_torrents();
}

void QBtSession::resumeAllTorrents() {
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        try {
            QTorrentHandle h = QTorrentHandle(*torrentIT);
            if (h.is_paused()) {
                h.resume();
                emit resumedTorrent(h);
            }
        } catch(invalid_handle&) {}
    }
}

void QBtSession::pauseTorrent(const QString &hash) {
    QTorrentHandle h = getTorrentHandle(hash);
    if (!h.is_paused()) {
        h.pause();
        emit pausedTorrent(h);
    }
}

void QBtSession::resumeTorrent(const QString &hash, const bool force) {
    QTorrentHandle h = getTorrentHandle(hash);
    if (h.is_paused() || (h.is_forced() != force)) {
        h.resume(force);
        emit resumedTorrent(h);
    }
}

bool QBtSession::loadFastResumeData(const QString &hash, std::vector<char> &buf) {
    const QString fastresume_path = QDir(fsutils::BTBackupLocation()).absoluteFilePath(hash+QString(".fastresume"));
    qDebug("Trying to load fastresume data: %s", qPrintable(fastresume_path));
    QFile fastresume_file(fastresume_path);
    if (fastresume_file.size() <= 0)
        return false;
    if (!fastresume_file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray content = fastresume_file.readAll();
    const int content_size = content.size();
    Q_ASSERT(content_size > 0);
    buf.resize(content_size);
    memcpy(&buf[0], content.data(), content_size);
    fastresume_file.close();
    return true;
}

void QBtSession::loadTorrentSettings(QTorrentHandle& h) {
    Preferences* const pref = Preferences::instance();
    // Connections limit per torrent
    h.set_max_connections(pref->getMaxConnecsPerTorrent());
    // Uploads limit per torrent
    h.set_max_uploads(pref->getMaxUploadsPerTorrent());
#ifndef DISABLE_GUI
    // Resolve countries
    h.resolve_countries(resolve_countries);
#endif
}

QTorrentHandle QBtSession::addMagnetUri(QString magnet_uri, bool resumed, bool fromScanDir, const QString &filePath)
{
    Q_UNUSED(fromScanDir);
    Q_UNUSED(filePath);
    Preferences* const pref = Preferences::instance();
    Logger* const logger = Logger::instance();
    QTorrentHandle h;
    add_torrent_params p;
    libtorrent::error_code ec;

    libtorrent::parse_magnet_uri(magnet_uri.toUtf8().constData(), p, ec);
    if (ec) {
        logger->addMessage(tr("Couldn't parse this Magnet URI: '%1'").arg(magnet_uri));
        return h;
    }
    const QString hash(misc::toQString(p.info_hash));
    if (hash.isEmpty()) {
        logger->addMessage(tr("'%1' is not a valid magnet URI.").arg(magnet_uri));
        return h;
    }
    const QDir torrentBackup(fsutils::BTBackupLocation());
    if (resumed) {
        // Load metadata
        const QString torrent_path = torrentBackup.absoluteFilePath(hash+".torrent");
        if (QFile::exists(torrent_path))
            return addTorrent(torrent_path, false, QString::null, true);
    }
    qDebug("Adding a magnet URI: %s", qPrintable(hash));
    Q_ASSERT(magnet_uri.startsWith("magnet:", Qt::CaseInsensitive));

    // limit h_ex scope
    {
        // Check for duplicate torrent
        QTorrentHandle h_ex = QTorrentHandle(s->find_torrent(p.info_hash));
        if (h_ex.is_valid()) {
            qDebug("/!\\ Torrent is already in download list");
            logger->addMessage(tr("'%1' is already in download list.", "e.g: 'xxx.avi' is already in download list.").arg(magnet_uri));
            // Check if the torrent contains trackers or url seeds we don't know about
            // and add them
            mergeTorrents(h_ex, magnet_uri);
            return h;
        }
    }

    initializeAddTorrentParams(hash, p);

    // Get save path
    QString savePath;
    if (!resumed && savepathLabel_fromurl.contains(magnet_uri)) {
        QPair<QString, QString> savePath_label = savepathLabel_fromurl.take(magnet_uri);
        if(!savePath_label.first.isEmpty())
            savePath = savePath_label.first;
        // Remember label
        if(!savePath_label.second.isEmpty())
            TorrentTempData::setLabel(hash, savePath_label.second);
    }
    if (savePath.isEmpty())
        savePath = getSavePath(hash, false);
    if (!defaultTempPath.isEmpty() && !TorrentPersistentData::instance()->isSeed(hash)) {
        qDebug("addMagnetURI: Temp folder is enabled.");
        QString torrent_tmp_path = defaultTempPath;
        p.save_path = fsutils::toNativePath(torrent_tmp_path).toUtf8().constData();
        // Check if save path exists, creating it otherwise
        if (!QDir(torrent_tmp_path).exists())
            QDir().mkpath(torrent_tmp_path);
        qDebug("addTorrent: using save_path: %s", qPrintable(torrent_tmp_path));
    } else {
        p.save_path = fsutils::toNativePath(savePath).toUtf8().constData();
        // Check if save path exists, creating it otherwise
        if (!QDir(savePath).exists()) QDir().mkpath(savePath);
        qDebug("addTorrent: using save_path: %s", qPrintable(savePath));
    }

    qDebug("Adding magnet URI: %s", qPrintable(magnet_uri));

    // Adding torrent to Bittorrent session
    try {
        h =  QTorrentHandle(s->add_torrent(p));
    }catch(std::exception &e) {
        qDebug("Error: %s", e.what());
    }
    // Check if it worked
    if (!h.is_valid()) {
        // No need to keep on, it failed.
        qDebug("/!\\ Error: Invalid handle");
        return h;
    }
    Q_ASSERT(h.hash() == hash);

    loadTorrentSettings(h);

    // Load filtered files
    bool add_paused = pref->addTorrentsInPause();
    if (!resumed) {
        if (TorrentTempData::hasTempData(hash))
            add_paused = TorrentTempData::isAddPaused(hash);
        loadTorrentTempData(h, savePath, true);
    }
    if (HiddenData::hasData(hash) && pref->isQueueingSystemEnabled()) {
        //Internally increase the queue limits to ensure that the magnet is started
        libtorrent::session_settings sessionSettings(s->settings());
        int max_downloading = pref->getMaxActiveDownloads();
        int max_active = pref->getMaxActiveTorrents();
        if (max_downloading > -1)
            sessionSettings.active_downloads = max_downloading + HiddenData::getDownloadingSize();
        else
            sessionSettings.active_downloads = max_downloading;
        if (max_active > -1)
            sessionSettings.active_limit = max_active + HiddenData::getDownloadingSize();
        else
            sessionSettings.active_limit = max_active;
        s->set_settings(sessionSettings);
        h.queue_position_top();
    }
    if (!add_paused || HiddenData::hasData(hash)) {
        // Start torrent because it was added in paused state
        h.resume();
    }
    // Send torrent addition signal
    logger->addMessage(tr("'%1' added to download list.", "'/home/y/xxx.torrent' was added to download list.").arg(magnet_uri));
    if (!HiddenData::hasData(hash))
        emit addedTorrent(h);

    return h;
}

// Add a torrent to the Bittorrent session
QTorrentHandle QBtSession::addTorrent(QString path, bool fromScanDir, QString from_url, bool resumed, bool imported) {
    QTorrentHandle h;
    Preferences* const pref = Preferences::instance();
    Logger* const logger = Logger::instance();

    // Check if BT_backup directory exists
    const QDir torrentBackup(fsutils::BTBackupLocation());
    if (!torrentBackup.exists()) {
        // If temporary file, remove it
        if (!from_url.isNull() || fromScanDir)
            fsutils::forceRemove(path);
        return h;
    }

    // Fix the input path if necessary
    path = fsutils::fromNativePath(path);
#ifdef Q_OS_WIN
    // Windows hack
    if (!path.endsWith(".torrent"))
        if (QFile::rename(path, path+".torrent")) path += ".torrent";
#endif
    if (path.startsWith("file:", Qt::CaseInsensitive))
        path = QUrl::fromEncoded(path.toLocal8Bit()).toLocalFile();
    if (path.isEmpty()) return h;

    Q_ASSERT(!misc::isUrl(path));

    qDebug("Adding %s to download list", qPrintable(path));
    boost::intrusive_ptr<torrent_info> t;
    try {
        qDebug() << "Loading torrent at" << path;
        // Getting torrent file informations
        std::vector<char> buffer;
        lazy_entry entry;
        libtorrent::error_code ec;
        misc::loadBencodedFile(path, buffer, entry, ec);
        t = new torrent_info(entry);
        if (!t->is_valid())
            throw std::exception();
    } catch(std::exception& e) {
        if (!from_url.isNull()) {
            logger->addMessage(tr("Unable to decode torrent file: '%1'", "e.g: Unable to decode torrent file: '/home/y/xxx.torrent'").arg(from_url), Log::CRITICAL);
            logger->addMessage(misc::toQStringU(e.what()), Log::CRITICAL);
            //emit invalidTorrent(from_url);
            fsutils::forceRemove(path);
        }else{
            logger->addMessage(tr("Unable to decode torrent file: '%1'", "e.g: Unable to decode torrent file: '/home/y/xxx.torrent'").arg(fsutils::toNativePath(path)), Log::CRITICAL);
            //emit invalidTorrent(path);
        }
        logger->addMessage(tr("This file is either corrupted or this isn't a torrent."), Log::CRITICAL);
        if (fromScanDir) {
            // Remove file
            fsutils::forceRemove(path);
        }
        return h;
    }

    const QString hash = misc::toQString(t->info_hash());

    qDebug(" -> Hash: %s", qPrintable(hash));
    qDebug(" -> Name: %s", t->name().c_str());

    // Check for duplicate
    if (s->find_torrent(t->info_hash()).is_valid()) {
        qDebug("/!\\ Torrent is already in download list");
        // Update info Bar
        if (!from_url.isNull()) {
            logger->addMessage(tr("'%1' is already in download list.", "e.g: 'xxx.avi' is already in download list.").arg(from_url));
        }else{
            logger->addMessage(tr("'%1' is already in download list.", "e.g: 'xxx.avi' is already in download list.").arg(fsutils::toNativePath(path)));
        }
        // Check if the torrent contains trackers or url seeds we don't know about
        // and add them
        QTorrentHandle h_ex = getTorrentHandle(hash);
        mergeTorrents(h_ex, t);

        // Delete file if temporary
        if (!from_url.isNull() || fromScanDir)
            fsutils::forceRemove(path);
        return h;
    }

    // Check number of files
    if (t->num_files() < 1) {
        logger->addMessage(tr("Error: The torrent %1 does not contain any file.").arg(misc::toQStringU(t->name())));
        // Delete file if temporary
        if (!from_url.isNull() || fromScanDir)
            fsutils::forceRemove(path);
        return h;
    }

    // Actually add the torrent
    add_torrent_params p;
    initializeAddTorrentParams(hash, p);
    p.ti = t;

    // Get fast resume data if existing
    bool fastResume = false;
    std::vector<char> buf; // Needs to stay in the function scope
    if (resumed) {
        if (loadFastResumeData(hash, buf)) {
            fastResume = true;
#if LIBTORRENT_VERSION_NUM < 10000
            p.resume_data = &buf;
#else
            p.resume_data = buf;
#endif
            qDebug("Successfully loaded fast resume data");
        }
    }

    recoverPersistentData(hash, buf);
    QString savePath;
    if (!from_url.isEmpty() && savepathLabel_fromurl.contains(QUrl::fromEncoded(from_url.toUtf8()))) {
        // Enforcing the save path defined before URL download (from RSS for example)
        QPair<QString, QString> savePath_label = savepathLabel_fromurl.take(QUrl::fromEncoded(from_url.toUtf8()));
        if (savePath_label.first.isEmpty())
            savePath = getSavePath(hash, fromScanDir, path);
        else
            savePath = savePath_label.first;
        // Remember label
        TorrentTempData::setLabel(hash, savePath_label.second);
    } else {
        savePath = getSavePath(hash, fromScanDir, path, imported);
    }
    if (!imported && !defaultTempPath.isEmpty() && !TorrentPersistentData::instance()->isSeed(hash)) {
        qDebug("addTorrent::Temp folder is enabled.");
        QString torrent_tmp_path = defaultTempPath;
        p.save_path = fsutils::toNativePath(torrent_tmp_path).toUtf8().constData();
        // Check if save path exists, creating it otherwise
        if (!QDir(torrent_tmp_path).exists()) QDir().mkpath(torrent_tmp_path);
        qDebug("addTorrent: using save_path: %s", qPrintable(torrent_tmp_path));
    } else {
        p.save_path = fsutils::toNativePath(savePath).toUtf8().constData();
        // Check if save path exists, creating it otherwise
        if (!QDir(savePath).exists()) QDir().mkpath(savePath);
        qDebug("addTorrent: using save_path: %s", qPrintable(savePath));
    }

    // Adding torrent to Bittorrent session
    try {
        h =  QTorrentHandle(s->add_torrent(p));
    }catch(std::exception &e) {
        qDebug("Error: %s", e.what());
    }
    // Check if it worked
    if (!h.is_valid()) {
        qDebug("/!\\ Error: Invalid handle");
        // If temporary file, remove it
        if (!from_url.isNull() || fromScanDir)
            fsutils::forceRemove(path);
        return h;
    }

    loadTorrentSettings(h);

    bool add_paused = pref->addTorrentsInPause();
    if (!resumed) {
        qDebug("This is a NEW torrent (first time)...");
        if (TorrentTempData::hasTempData(hash))
            add_paused = TorrentTempData::isAddPaused(hash);

        loadTorrentTempData(h, savePath, false);

        // Append .!qB to incomplete files
        if (appendqBExtension)
            appendqBextensionToTorrent(h, true);

        // Backup torrent file
        const QString newFile = torrentBackup.absoluteFilePath(hash + ".torrent");
        if (path != newFile)
            QFile::copy(path, newFile);
        // Copy the torrent file to the export folder
        if (m_torrentExportEnabled)
            exportTorrentFile(h);
    }

    if (!fastResume && !add_paused) {
        // Start torrent because it was added in paused state
        h.resume();
    }

    // If temporary file, remove it
    if (!from_url.isNull() || fromScanDir)
        fsutils::forceRemove(path);

    // Display console message
    if (!from_url.isNull()) {
        if (fastResume)
            logger->addMessage(tr("'%1' resumed. (fast resume)", "'/home/y/xxx.torrent' was resumed. (fast resume)").arg(from_url));
        else
            logger->addMessage(tr("'%1' added to download list.", "'/home/y/xxx.torrent' was added to download list.").arg(from_url));
    }else{
        if (fastResume)
            logger->addMessage(tr("'%1' resumed. (fast resume)", "'/home/y/xxx.torrent' was resumed. (fast resume)").arg(fsutils::toNativePath(path)));
        else
            logger->addMessage(tr("'%1' added to download list.", "'/home/y/xxx.torrent' was added to download list.").arg(fsutils::toNativePath(path)));
    }

    // Send torrent addition signal
    emit addedTorrent(h);
    return h;
}

void QBtSession::exportTorrentFile(const QTorrentHandle& h, TorrentExportFolder folder) {
    Q_ASSERT((folder == RegularTorrentExportFolder && m_torrentExportEnabled) ||
             (folder == FinishedTorrentExportFolder && m_finishedTorrentExportEnabled));
    QString torrent_path = QDir(fsutils::BTBackupLocation()).absoluteFilePath(h.hash()+".torrent");
    QDir exportPath(folder == RegularTorrentExportFolder ? Preferences::instance()->getTorrentExportDir() : Preferences::instance()->getFinishedTorrentExportDir());
    if (exportPath.exists() || exportPath.mkpath(exportPath.absolutePath())) {
        QString new_torrent_path = exportPath.absoluteFilePath(h.name()+".torrent");
        if (QFile::exists(new_torrent_path) && fsutils::sameFiles(torrent_path, new_torrent_path)) {
            // Append hash to torrent name to make it unique
            new_torrent_path = exportPath.absoluteFilePath(h.name()+"-"+h.hash()+".torrent");
        }
        QFile::copy(torrent_path, new_torrent_path);
    }
}

void QBtSession::initializeAddTorrentParams(const QString &hash, add_torrent_params &p) {
    // Seeding mode
    // Skip checking and directly start seeding (new in libtorrent v0.15)
    if (TorrentTempData::isSeedingMode(hash))
        p.flags |= add_torrent_params::flag_seed_mode;
    else
        p.flags &= ~add_torrent_params::flag_seed_mode;

    // Preallocation mode
    if (preAllocateAll)
        p.storage_mode = storage_mode_allocate;
    else
        p.storage_mode = storage_mode_sparse;

    // Priorities
    /*if (TorrentTempData::hasTempData(hash)) {
    std::vector<int> fp;
    TorrentTempData::getFilesPriority(hash, fp);
    if (!fp.empty()) {
      std::vector<boost::uint8_t> *fp_conv = new std::vector<boost::uint8_t>();
      for (uint i=0; i<fp.size(); ++i) {
        fp_conv->push_back(fp[i]);
      }
      p.file_priorities = fp_conv;
    }
  }*/

    // Start in pause
    p.flags |= add_torrent_params::flag_paused;
    p.flags &= ~add_torrent_params::flag_duplicate_is_error; // Already checked
    p.flags &= ~add_torrent_params::flag_auto_managed; // Because it is added in paused state
}

void QBtSession::loadTorrentTempData(QTorrentHandle &h, QString savePath, bool magnet) {
    qDebug("loadTorrentTempdata() - ENTER");
    const QString hash = h.hash();
    // Sequential download
    if (TorrentTempData::hasTempData(hash)) {
        // sequential download
        h.set_sequential_download(TorrentTempData::isSequential(hash));

        // The following is useless for newly added magnet
        if (!magnet) {
            // Files priorities
            vector<int> fp;
            TorrentTempData::getFilesPriority(hash, fp);
            h.prioritize_files(fp);

            // Prioritize first/last piece
            h.prioritize_first_last_piece(TorrentTempData::isSequential(hash));

            // Update file names
            const QStringList files_path = TorrentTempData::getFilesPath(hash);
            QDir  base_dir(h.save_path());
            if (files_path.size() == h.num_files()) {
                bool force_recheck = false;
                for (int i=0; i<h.num_files(); ++i) {
                    const QString &path = files_path.at(i);
                    if (!force_recheck && base_dir.exists(path))
                        force_recheck = true;
                    qDebug("Renaming file to %s", qPrintable(path));
                    h.rename_file(i, path);
                }
                // Force recheck
                if (force_recheck) h.force_recheck();
            }
        }
    }
    // Save persistent data for new torrent
    qDebug("Saving torrent persistant data");
    if (defaultTempPath.isEmpty())
        TorrentPersistentData::instance()->saveTorrentPersistentData(h, QString::null, magnet);
    else
        TorrentPersistentData::instance()->saveTorrentPersistentData(h, fsutils::fromNativePath(savePath), magnet);
}

void QBtSession::mergeTorrents(const QTorrentHandle &h, const QString &magnet_uri)
{
    QStringList trackers;
    QStringList urlSeeds;
    add_torrent_params p;
    boost::system::error_code ec;

    parse_magnet_uri(magnet_uri.toUtf8().constData(), p, ec);

    for (std::vector<std::string>::const_iterator i = p.trackers.begin(), e = p.trackers.end(); i != e; ++i)
        trackers.push_back(misc::toQStringU(*i));

#if LIBTORRENT_VERSION_NUM >= 10000
    for (std::vector<std::string>::const_iterator i = p.url_seeds.begin(), e = p.url_seeds.end(); i != e; ++i)
        urlSeeds.push_back(misc::toQStringU(*i));
#endif

    mergeTorrents_impl(h, trackers, urlSeeds);
}

void QBtSession::mergeTorrents(const QTorrentHandle &h, const boost::intrusive_ptr<torrent_info> t) {
    QStringList trackers;
    QStringList urlSeeds;

    foreach (const announce_entry& newTracker, t->trackers())
        trackers.append(misc::toQStringU(newTracker.url));

    foreach (const web_seed_entry& newUrlSeed, t->web_seeds())
        urlSeeds.append(misc::toQStringU(newUrlSeed.url));

    mergeTorrents_impl(h, trackers, urlSeeds);
}

void QBtSession::mergeTorrents_impl(const QTorrentHandle &h, const QStringList &trackers, const QStringList &urlSeeds)
{
    if (!h.is_valid())
        return;

    QString hash = h.hash();
    QString name = h.name();
    QStringList addedTrackers;
    const std::vector<announce_entry> existingTrackers = h.trackers();
    const QStringList existingUrlSeeds = h.url_seeds();

    foreach (const QString &tracker, trackers) {
        QUrl trackerUrl(tracker);
        bool found = false;

        foreach (const announce_entry &existingTracker, existingTrackers) {
            QUrl existingTrackerUrl(misc::toQStringU(existingTracker.url));
            if (trackerUrl == existingTrackerUrl) {
                found = true;
                break;
            }
        }

        if (!found) {
            h.add_tracker(announce_entry(tracker.toUtf8().constData()));
            addedTrackers.append(tracker);
            Logger::instance()->addMessage(tr("Tracker '%1' was added to torrent '%2'").arg(tracker).arg(name));
        }
    }

    if (!addedTrackers.empty())
        emit trackersAdded(addedTrackers, hash);

    if (existingTrackers.empty() && !h.trackers().empty())
        emit trackerlessChange(false, hash);

    foreach (const QString &urlSeed, urlSeeds) {
        QUrl urlSeedUrl(urlSeed);
        bool found = false;

        foreach (const QString &existingUrlSeed, existingUrlSeeds) {
            QUrl existingUrlSeedUrl(existingUrlSeed);
            if (urlSeedUrl == existingUrlSeedUrl) {
                found = true;
                break;
            }
        }

        if (!found) {
            h.add_url_seed(urlSeed);
            Logger::instance()->addMessage(tr("URL seed '%1' was added to torrent '%2'").arg(urlSeed).arg(name));
        }
    }

    h.force_reannounce();
    emit reloadTrackersAndUrlSeeds(h);
}

void QBtSession::exportTorrentFiles(QString path) {
    Q_ASSERT(m_torrentExportEnabled);
    QDir exportDir(path);
    if (!exportDir.exists()) {
        if (!exportDir.mkpath(exportDir.absolutePath())) {
            std::cerr << "Error: Could not create torrent export directory: " << qPrintable(exportDir.absolutePath()) << std::endl;
            return;
        }
    }
    QDir torrentBackup(fsutils::BTBackupLocation());
    std::vector<torrent_handle> handles = s->get_torrents();

    std::vector<torrent_handle>::iterator itr=handles.begin();
    std::vector<torrent_handle>::iterator itrend=handles.end();
    for ( ; itr != itrend; ++itr) {
        const QTorrentHandle h(*itr);
        if (!h.is_valid()) {
            std::cerr << "Torrent Export: torrent is invalid, skipping..." << std::endl;
            continue;
        }
        const QString src_path(torrentBackup.absoluteFilePath(h.hash()+".torrent"));
        if (QFile::exists(src_path)) {
            QString dst_path = exportDir.absoluteFilePath(h.name()+".torrent");
            if (QFile::exists(dst_path)) {
                if (!fsutils::sameFiles(src_path, dst_path)) {
                    dst_path = exportDir.absoluteFilePath(h.name()+"-"+h.hash()+".torrent");
                } else {
                    qDebug("Torrent Export: Destination file exists, skipping...");
                    continue;
                }
            }
            qDebug("Export Torrent: %s -> %s", qPrintable(src_path), qPrintable(dst_path));
            QFile::copy(src_path, dst_path);
        } else {
            std::cerr << "Error: could not export torrent "<< qPrintable(h.hash()) << ", maybe it has not metadata yet." <<std::endl;
        }
    }
}

void QBtSession::setMaxConnectionsPerTorrent(int max) {
    qDebug() << Q_FUNC_INFO << max;
    // Apply this to all session torrents
    std::vector<torrent_handle> handles = s->get_torrents();

    std::vector<torrent_handle>::const_iterator it = handles.begin();
    std::vector<torrent_handle>::const_iterator itend = handles.end();
    for ( ; it != itend; ++it) {
        if (!it->is_valid())
            continue;
        try {
            it->set_max_connections(max);
        } catch(std::exception) {}
    }
}

void QBtSession::setMaxUploadsPerTorrent(int max) {
    qDebug() << Q_FUNC_INFO << max;
    // Apply this to all session torrents
    std::vector<torrent_handle> handles = s->get_torrents();

    std::vector<torrent_handle>::const_iterator it = handles.begin();
    std::vector<torrent_handle>::const_iterator itend = handles.end();
    for ( ; it != itend; ++it) {
        if (!it->is_valid())
            continue;
        try {
            it->set_max_uploads(max);
        } catch(std::exception) {}
    }
}

void QBtSession::enableUPnP(bool b) {
    Preferences* const pref = Preferences::instance();
    if (b) {
        qDebug("Enabling UPnP / NAT-PMP");
#if LIBTORRENT_VERSION_NUM < 10000
        m_upnp = s->start_upnp();
        m_natpmp = s->start_natpmp();
#else
        s->start_upnp();
        s->start_natpmp();
#endif
        // TODO: Remove dependency from WebUI
        // Use UPnP/NAT-PMP for Web UI too
        if (pref->isWebUiEnabled() && pref->useUPnPForWebUIPort()) {
            const qint16 port = pref->getWebUiPort();
#if LIBTORRENT_VERSION_NUM < 10000
            m_upnp->add_mapping(upnp::tcp, port, port);
            m_natpmp->add_mapping(natpmp::tcp, port, port);
#else
            s->add_port_mapping(session::tcp, port, port);
#endif
        }
    } else {
        qDebug("Disabling UPnP / NAT-PMP");
        s->stop_upnp();
        s->stop_natpmp();

#if LIBTORRENT_VERSION_NUM < 10000
        m_upnp = 0;
        m_natpmp = 0;
#endif
    }
}

void QBtSession::enableLSD(bool b) {
    if (b) {
        if (!LSDEnabled) {
            qDebug("Enabling Local Peer Discovery");
            s->start_lsd();
            LSDEnabled = true;
        }
    } else {
        if (LSDEnabled) {
            qDebug("Disabling Local Peer Discovery");
            s->stop_lsd();
            LSDEnabled = false;
        }
    }
}

void QBtSession::loadSessionState() {
    const QString state_path = fsutils::cacheLocation()+"/"+QString::fromUtf8("ses_state");
    if (!QFile::exists(state_path)) return;
    if (QFile(state_path).size() == 0) {
        // Remove empty invalid state file
        fsutils::forceRemove(state_path);
        return;
    }
    std::vector<char> in;
    lazy_entry e;
    libtorrent::error_code ec;
    misc::loadBencodedFile(state_path, in, e, ec);
    if (!ec)
        s->load_state(e);
}

void QBtSession::saveSessionState() {
    qDebug("Saving session state to disk...");
    const QString state_path = fsutils::cacheLocation()+"/"+QString::fromUtf8("ses_state");
    entry session_state;
    s->save_state(session_state);
    vector<char> out;
    bencode(back_inserter(out), session_state);
    QFile session_file(state_path);
    if (!out.empty() && session_file.open(QIODevice::WriteOnly)) {
        session_file.write(&out[0], out.size());
        session_file.close();
    }
}

// Enable DHT
void QBtSession::enableDHT(bool b) {
    Logger* const logger = Logger::instance();
    if (b) {
        if (!DHTEnabled) {
            try {
                qDebug() << "Starting DHT...";
                Q_ASSERT(!s->is_dht_running());
                s->start_dht();
                s->add_dht_router(std::make_pair(std::string("router.bittorrent.com"), 6881));
                s->add_dht_router(std::make_pair(std::string("router.utorrent.com"), 6881));
                s->add_dht_router(std::make_pair(std::string("dht.transmissionbt.com"), 6881));
                s->add_dht_router(std::make_pair(std::string("dht.aelitis.com"), 6881)); // Vuze
                DHTEnabled = true;
                logger->addMessage(tr("DHT support [ON]"), Log::INFO);
                qDebug("DHT enabled");
            }
            catch(std::exception &e) {
                qDebug("Could not enable DHT, reason: %s", e.what());
                logger->addMessage(tr("DHT support [OFF]. Reason: %1").arg(misc::toQStringU(e.what())), Log::CRITICAL);
            }
        }
    }
    else {
        if (DHTEnabled) {
            DHTEnabled = false;
            s->stop_dht();
            logger->addMessage(tr("DHT support [OFF]"), Log::INFO);
            qDebug("DHT disabled");
        }
    }
}

qreal QBtSession::getRealRatio(const libtorrent::torrent_status &status) const {
    libtorrent::size_type upload = status.all_time_upload;
    // special case for a seeder who lost its stats, also assume nobody will import a 99% done torrent
    libtorrent::size_type download = (status.all_time_download < status.total_done * 0.01) ? status.total_done : status.all_time_download;

    if (download == 0)
        return (upload == 0) ? 0.0 : MAX_RATIO;

    qreal ratio = upload / (qreal) download;
    Q_ASSERT(ratio >= 0.0);
    return (ratio > MAX_RATIO) ? MAX_RATIO : ratio;
}

// Called periodically
void QBtSession::saveTempFastResumeData() {
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        QTorrentHandle h = QTorrentHandle(*torrentIT);
        try {
            if (!h.is_valid() || !h.has_metadata() /*|| h.is_seed() || h.is_paused()*/) continue;
            if (!h.need_save_resume_data()) continue;
            if (h.state() == torrent_status::checking_files || h.state() == torrent_status::queued_for_checking || h.has_error()
                || TorrentPersistentData::instance()->getHasMissingFiles(h.hash())) continue;
            qDebug("Saving fastresume data for %s", qPrintable(h.name()));
            h.save_resume_data();
        }catch(std::exception &e) {}
    }
}

// Only save fast resume data for unfinished and unpaused torrents (Optimization)
// Called on exit
void QBtSession::saveFastResumeData() {
    qDebug("Saving fast resume data...");
    // Stop listening for alerts
    resumeDataTimer.stop();
    int num_resume_data = 0;
    // Pause session
    s->pause();
    std::vector<torrent_handle> torrents =  s->get_torrents();

    std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
    std::vector<torrent_handle>::iterator torrentITend = torrents.end();
    for ( ; torrentIT != torrentITend; ++torrentIT) {
        QTorrentHandle h = QTorrentHandle(*torrentIT);
        if (!h.is_valid())
            continue;
        try {
            if (isQueueingEnabled())
                TorrentPersistentData::instance()->savePriority(h);
            if (!h.has_metadata())
                continue;
            // Actually with should save fast resume data for paused files too
            //if (h.is_paused()) continue;
            if (h.state() == torrent_status::checking_files || h.state() == torrent_status::queued_for_checking || h.has_error()) continue;
            if (TorrentPersistentData::instance()->getHasMissingFiles(h.hash())) {
                TorrentPersistentData::instance()->setHasMissingFiles(h.hash(), false);
                continue;
            }
            h.save_resume_data();
            ++num_resume_data;
        } catch(libtorrent::invalid_handle&) {}
    }
    while (num_resume_data > 0) {
        std::vector<alert*> alerts;
        m_alertDispatcher->getPendingAlerts(alerts, 30*1000);
        if (alerts.empty()) {
            std::cerr << " aborting with " << num_resume_data << " outstanding "
                                                                 "torrents to save resume data for" << std::endl;
            break;
        }

        for (std::vector<alert*>::const_iterator i = alerts.begin(), end = alerts.end(); i != end; ++i)
        {
            alert const* a = *i;
            // Saving fastresume data can fail
            save_resume_data_failed_alert const* rda = dynamic_cast<save_resume_data_failed_alert const*>(a);
            if (rda) {
                --num_resume_data;
                try {
                    // Remove torrent from session
                    if (rda->handle.is_valid())
                        s->remove_torrent(rda->handle);
                }catch(libtorrent::libtorrent_exception) {}
                delete a;
                continue;
            }
            save_resume_data_alert const* rd = dynamic_cast<save_resume_data_alert const*>(a);
            if (!rd) {
                delete a;
                continue;
            }
            // Saving fast resume data was successful
            --num_resume_data;
            if (!rd->resume_data) {
                delete a;
                continue;
            }
            QDir torrentBackup(fsutils::BTBackupLocation());
            const QTorrentHandle h(rd->handle);
            if (!h.is_valid()) {
                delete a;
                continue;
            }
            try {
                // Remove old fastresume file if it exists
                backupPersistentData(h.hash(), rd->resume_data);
                vector<char> out;
                bencode(back_inserter(out), *rd->resume_data);
                const QString filepath = torrentBackup.absoluteFilePath(h.hash()+".fastresume");
                QFile resume_file(filepath);
                if (resume_file.exists())
                    fsutils::forceRemove(filepath);
                if (!out.empty() && resume_file.open(QIODevice::WriteOnly)) {
                    resume_file.write(&out[0], out.size());
                    resume_file.close();
                }
                // Remove torrent from session
                s->remove_torrent(rd->handle);
            } catch(libtorrent::invalid_handle&) {}

            delete a;
        }
    }
}

void QBtSession::addTorrentsFromScanFolder(QStringList &pathList)
{
    foreach (const QString &file, pathList) {
        qDebug("File %s added", qPrintable(file));
        if (file.endsWith(".magnet")) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly)) {
                qDebug("Failed to open magnet file: %s", qPrintable(f.errorString()));
            } else {
                const QString link = QString::fromLocal8Bit(f.readAll());
                addMagnetUri(link, false, true, file);
                f.remove();
            }
            continue;
        }
        try {
            std::vector<char> buffer;
            lazy_entry entry;
            libtorrent::error_code ec;
            misc::loadBencodedFile(file, buffer, entry, ec);
            torrent_info t(entry);
            if (t.is_valid())
                addTorrent(file, true);
        } catch(std::exception&) {
            qDebug("Ignoring incomplete torrent file: %s", qPrintable(file));
        }
    }
}

void QBtSession::setDefaultSavePath(const QString &savepath) {
    if (savepath.isEmpty())
        return;

    defaultSavePath = fsutils::fromNativePath(savepath);
}

void QBtSession::setDefaultTempPath(const QString &temppath) {
    if (QDir(defaultTempPath) == QDir(temppath))
        return;

    if (temppath.isEmpty()) {
        // Disabling temp dir
        // Moving all torrents to their destination folder
        std::vector<torrent_handle> torrents = s->get_torrents();

        std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
        std::vector<torrent_handle>::iterator torrentITend = torrents.end();
        for ( ; torrentIT != torrentITend; ++torrentIT) {
            QTorrentHandle h = QTorrentHandle(*torrentIT);
            if (!h.is_valid()) continue;
            h.move_storage(getSavePath(h.hash()));
        }
    } else {
        qDebug("Enabling default temp path...");
        // Moving all downloading torrents to temporary save path
        std::vector<torrent_handle> torrents = s->get_torrents();

        std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
        std::vector<torrent_handle>::iterator torrentITend = torrents.end();
        for ( ; torrentIT != torrentITend; ++torrentIT) {
            QTorrentHandle h = QTorrentHandle(*torrentIT);
            if (!h.is_valid()) continue;
            if (!h.is_seed()) {
                qDebug("Moving torrent to its temp save path: %s", qPrintable(temppath));
                h.move_storage(temppath);
            }
        }
    }
    defaultTempPath = fsutils::fromNativePath(temppath);
}

void QBtSession::appendqBextensionToTorrent(const QTorrentHandle &h, bool append) {
    if (!h.is_valid() || !h.has_metadata()) return;
    std::vector<size_type> fp;
    h.file_progress(fp);
    for (int i=0; i<h.num_files(); ++i) {
        if (append) {
            const qulonglong file_size = h.filesize_at(i);
            if (file_size > 0 && (fp[i]/(double)file_size) < 1.) {
                const QString name = h.filepath_at(i);
                if (!name.endsWith(".!qB")) {
                    const QString new_name = name+".!qB";
                    qDebug("Renaming %s to %s", qPrintable(name), qPrintable(new_name));
                    h.rename_file(i, new_name);
                }
            }
        } else {
            QString name = h.filepath_at(i);
            if (name.endsWith(".!qB")) {
                const QString old_name = name;
                name.chop(4);
                qDebug("Renaming %s to %s", qPrintable(old_name), qPrintable(name));
                h.rename_file(i, name);
            }
        }
    }
}

void QBtSession::changeLabelInTorrentSavePath(const QTorrentHandle &h, QString old_label, QString new_label) {
    if (!h.is_valid()) return;
    if (!appendLabelToSavePath) return;
    QString old_save_path = fsutils::fromNativePath(TorrentPersistentData::instance()->getSavePath(h.hash()));
    if (!old_save_path.startsWith(defaultSavePath)) return;
    QString new_save_path = fsutils::updateLabelInSavePath(defaultSavePath, old_save_path, old_label, new_label);
    if (new_save_path != old_save_path) {
        // Move storage
        qDebug("Moving storage to %s", qPrintable(new_save_path));
        QDir().mkpath(new_save_path);
        h.move_storage(new_save_path);
    }
}

void QBtSession::appendLabelToTorrentSavePath(const QTorrentHandle& h) {
    if (!h.is_valid()) return;
    const TorrentPersistentData* const TorPersistent = TorrentPersistentData::instance();
    const QString label = TorPersistent->getLabel(h.hash());
    if (label.isEmpty()) return;
    // Current save path
    QString old_save_path = fsutils::fromNativePath(TorPersistent->getSavePath(h.hash()));
    QString new_save_path = fsutils::updateLabelInSavePath(defaultSavePath, old_save_path, "", label);
    if (old_save_path != new_save_path) {
        // Move storage
        QDir().mkpath(new_save_path);
        h.move_storage(new_save_path);
    }
}

void QBtSession::setAppendLabelToSavePath(bool append) {
    if (appendLabelToSavePath != append) {
        appendLabelToSavePath = !appendLabelToSavePath;
        if (appendLabelToSavePath) {
            // Move torrents storage to sub folder with label name
            std::vector<torrent_handle> torrents = s->get_torrents();

            std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
            std::vector<torrent_handle>::iterator torrentITend = torrents.end();
            for ( ; torrentIT != torrentITend; ++torrentIT) {
                QTorrentHandle h = QTorrentHandle(*torrentIT);
                appendLabelToTorrentSavePath(h);
            }
        }
    }
}

void QBtSession::setAppendqBExtension(bool append) {
    if (appendqBExtension != append) {
        appendqBExtension = !appendqBExtension;
        // append or remove .!qB extension for incomplete files
        std::vector<torrent_handle> torrents = s->get_torrents();

        std::vector<torrent_handle>::iterator torrentIT = torrents.begin();
        std::vector<torrent_handle>::iterator torrentITend = torrents.end();
        for ( ; torrentIT != torrentITend; ++torrentIT) {
            QTorrentHandle h = QTorrentHandle(*torrentIT);
            appendqBextensionToTorrent(h, appendqBExtension);
        }
    }
}

// Set the ports range in which is chosen the port the Bittorrent
// session will listen to
void QBtSession::setListeningPort(int port) {
    qDebug() << Q_FUNC_INFO << port;
    Preferences* const pref = Preferences::instance();
    Logger* const logger = Logger::instance();
    std::pair<int,int> ports(port, port);
    libtorrent::error_code ec;
    const QString iface_name = pref->getNetworkInterface();
    const bool listen_ipv6 = pref->getListenIPv6();
    if (iface_name.isEmpty()) {
        logger->addMessage(tr("qBittorrent is trying to listen on any interface port: %1", "e.g: qBittorrent is trying to listen on any interface port: TCP/6881").arg(QString::number(port)), Log::INFO);
        s->listen_on(ports, ec, 0, session::listen_no_system_port);

        if (ec)
            logger->addMessage(tr("qBittorrent failed to listen on any interface port: %1. Reason: %2", "e.g: qBittorrent failed to listen on any interface port: TCP/6881. Reason: no such interface" ).arg(QString::number(port)).arg(misc::toQStringU(ec.message())), Log::CRITICAL);

        return;
    }
    // Attempt to listen on provided interface
    const QNetworkInterface network_iface = QNetworkInterface::interfaceFromName(iface_name);
    if (!network_iface.isValid()) {
        qDebug("Invalid network interface: %s", qPrintable(iface_name));
        logger->addMessage(tr("The network interface defined is invalid: %1").arg(iface_name), Log::CRITICAL);
        return;
    }
    QString ip;
    qDebug("This network interface has %d IP addresses", network_iface.addressEntries().size());
    foreach (const QNetworkAddressEntry &entry, network_iface.addressEntries()) {
        if ((!listen_ipv6 && (entry.ip().protocol() == QAbstractSocket::IPv6Protocol))
            || (listen_ipv6 && (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)))
            continue;
        qDebug("Trying to listen on IP %s (%s)", qPrintable(entry.ip().toString()), qPrintable(iface_name));
        s->listen_on(ports, ec, entry.ip().toString().toLatin1().constData(), session::listen_no_system_port);
        if (!ec) {
            ip = entry.ip().toString();
            logger->addMessage(tr("qBittorrent is trying to listen on interface %1 port: %2", "e.g: qBittorrent is trying to listen on interface 192.168.0.1 port: TCP/6881").arg(ip).arg(QString::number(port)), Log::INFO);
            return;
        }
    }
    logger->addMessage(tr("qBittorrent didn't find an %1 local address to listen on", "qBittorrent didn't find an IPv4 local address to listen on").arg(listen_ipv6 ? "IPv6" : "IPv4"), Log::CRITICAL);
}

// Set download rate limit
// -1 to disable
void QBtSession::setDownloadRateLimit(long rate) {
    qDebug() << Q_FUNC_INFO << rate;
    Q_ASSERT(rate == -1 || rate >= 0);
    session_settings settings = s->settings();
    settings.download_rate_limit = rate;
    s->set_settings(settings);
}

// Set upload rate limit
// -1 to disable
void QBtSession::setUploadRateLimit(long rate) {
    qDebug() << Q_FUNC_INFO << rate;
    Q_ASSERT(rate == -1 || rate >= 0);
    session_settings settings = s->settings();
    settings.upload_rate_limit = rate;
    s->set_settings(settings);
}

// Torrents will a ratio superior to the given value will
// be automatically deleted
void QBtSession::setGlobalMaxRatio(qreal ratio) {
    if (ratio < 0) ratio = -1.;
    if (global_ratio_limit != ratio) {
        global_ratio_limit = ratio;
        qDebug("* Set global deleteRatio to %.1f", global_ratio_limit);
        updateRatioTimer();
    }
}

void QBtSession::setMaxRatioPerTorrent(const QString &hash, qreal ratio)
{
    if (ratio < 0)
        ratio = -1;
    if (ratio > MAX_RATIO)
        ratio = MAX_RATIO;
    qDebug("* Set individual max ratio for torrent %s to %.1f.",
           qPrintable(hash), ratio);
    TorrentPersistentData::instance()->setRatioLimit(hash, ratio);
    updateRatioTimer();
}

void QBtSession::removeRatioPerTorrent(const QString &hash)
{
    qDebug("* Remove individual max ratio for torrent %s.", qPrintable(hash));
    TorrentPersistentData::instance()->setRatioLimit(hash, TorrentPersistentData::USE_GLOBAL_RATIO);
    updateRatioTimer();
}

qreal QBtSession::getMaxRatioPerTorrent(const QString &hash, bool *usesGlobalRatio) const
{
    qreal ratio_limit = TorrentPersistentData::instance()->getRatioLimit(hash);
    if (ratio_limit == TorrentPersistentData::USE_GLOBAL_RATIO) {
        ratio_limit = global_ratio_limit;
        if (usesGlobalRatio)
            *usesGlobalRatio = true;
    } else {
        if (usesGlobalRatio)
            *usesGlobalRatio = false;
    }
    return ratio_limit;
}

void QBtSession::updateRatioTimer()
{
    if (global_ratio_limit == -1 && !TorrentPersistentData::instance()->hasPerTorrentRatioLimit()) {
        if (BigRatioTimer->isActive())
            BigRatioTimer->stop();
    } else if (!BigRatioTimer->isActive()) {
        BigRatioTimer->start();
    }
}

// Enable IP Filtering
void QBtSession::enableIPFilter(const QString &filter_path, bool force) {
    qDebug("Enabling IPFilter");
    if (!filterParser) {
        filterParser = new FilterParserThread(this, s);
        connect(filterParser.data(), SIGNAL(IPFilterParsed(int)), SLOT(handleIPFilterParsed(int)));
        connect(filterParser.data(), SIGNAL(IPFilterError()), SLOT(handleIPFilterError()));
    }
    if (filterPath.isEmpty() || filterPath != fsutils::fromNativePath(filter_path) || force) {
        filterPath = fsutils::fromNativePath(filter_path);
        filterParser->processFilterFile(fsutils::fromNativePath(filter_path));
    }
}

// Disable IP Filtering
void QBtSession::disableIPFilter() {
    qDebug("Disabling IPFilter");
    s->set_ip_filter(ip_filter());
    if (filterParser) {
        disconnect(filterParser.data(), 0, this, 0);
        delete filterParser;
    }
    filterPath = "";
}

void QBtSession::recursiveTorrentDownload(const QTorrentHandle &h)
{
    try {
        for (int i=0; i<h.num_files(); ++i) {
            const QString torrent_relpath = h.filepath_at(i);
            if (torrent_relpath.endsWith(".torrent")) {
                Logger::instance()->addMessage(tr("Recursive download of file %1 embedded in torrent %2", "Recursive download of test.torrent embedded in torrent test2").arg(fsutils::toNativePath(torrent_relpath)).arg(h.name()));
                const QString torrent_fullpath = h.save_path()+"/"+torrent_relpath;

                std::vector<char> buffer;
                lazy_entry entry;
                libtorrent::error_code ec;
                misc::loadBencodedFile(torrent_fullpath, buffer, entry, ec);
                boost::intrusive_ptr<torrent_info> t = new torrent_info(entry);
                const QString sub_hash = misc::toQString(t->info_hash());
                // Passing the save path along to the sub torrent file
                TorrentTempData::setSavePath(sub_hash, h.save_path());
                addTorrent(torrent_fullpath);
            }
        }
    }
    catch(std::exception&) {
        qDebug("Caught error loading torrent");
    }
}

void QBtSession::autoRunExternalProgram(const QTorrentHandle &h) {
    if (!h.is_valid()) return;
    QString program = Preferences::instance()->getAutoRunProgram().trimmed();
    if (program.isEmpty()) return;
    // Replace %f by torrent path
    QString torrent_path;
    if (h.num_files() == 1)
        torrent_path = h.firstFileSavePath();
    else
        torrent_path = h.save_path();
    program.replace("%f", torrent_path);
    // Replace %n by torrent name
    program.replace("%n", h.name());
    QProcess::startDetached(program);
}

void QBtSession::sendNotificationEmail(const QTorrentHandle &h) {
    libtorrent::torrent_status status = h.status(torrent_handle::query_accurate_download_counters);
    // Prepare mail content
    QString content = tr("Torrent name: %1").arg(h.name()) + "\n";
    content += tr("Torrent size: %1").arg(misc::friendlyUnit(status.total_wanted)) + "\n";
    content += tr("Save path: %1").arg(TorrentPersistentData::instance()->getSavePath(h.hash())) + "\n\n";
    content += tr("The torrent was downloaded in %1.", "The torrent was downloaded in 1 hour and 20 seconds").arg(misc::userFriendlyDuration(status.active_time)) + "\n\n\n";
    content += tr("Thank you for using qBittorrent.") + "\n";
    // Send the notification email
    Smtp *sender = new Smtp(this);
    sender->sendMail("notification@qbittorrent.org", Preferences::instance()->getMailNotificationEmail(), tr("[qBittorrent] %1 has finished downloading").arg(h.name()), content);
}

// Read alerts sent by the Bittorrent session
void QBtSession::readAlerts() {

    typedef std::vector<alert*> alerts_t;
    alerts_t alerts;
    m_alertDispatcher->getPendingAlertsNoWait(alerts);

    for (alerts_t::const_iterator i = alerts.begin(), end = alerts.end(); i != end; ++i) {
        handleAlert(*i);
        delete *i;
    }
}

void QBtSession::handleAlert(libtorrent::alert* a) {
    try {
        switch (a->type()) {
        case torrent_finished_alert::alert_type:
            handleTorrentFinishedAlert(static_cast<torrent_finished_alert*>(a));
            break;
        case save_resume_data_alert::alert_type:
            handleSaveResumeDataAlert(static_cast<save_resume_data_alert*>(a));
            break;
        case file_renamed_alert::alert_type:
            handleFileRenamedAlert(static_cast<file_renamed_alert*>(a));
            break;
        case torrent_deleted_alert::alert_type:
            handleTorrentDeletedAlert(static_cast<torrent_deleted_alert*>(a));
            break;
        case storage_moved_alert::alert_type:
            handleStorageMovedAlert(static_cast<storage_moved_alert*>(a));
            break;
        case storage_moved_failed_alert::alert_type:
            handleStorageMovedFailedAlert(static_cast<storage_moved_failed_alert*>(a));
            break;
        case metadata_received_alert::alert_type:
            handleMetadataReceivedAlert(static_cast<metadata_received_alert*>(a));
            break;
        case file_error_alert::alert_type:
            handleFileErrorAlert(static_cast<file_error_alert*>(a));
            break;
        case file_completed_alert::alert_type:
            handleFileCompletedAlert(static_cast<file_completed_alert*>(a));
            break;
        case torrent_paused_alert::alert_type:
            handleTorrentPausedAlert(static_cast<torrent_paused_alert*>(a));
            break;
        case tracker_error_alert::alert_type:
            handleTrackerErrorAlert(static_cast<tracker_error_alert*>(a));
            break;
        case tracker_reply_alert::alert_type:
            handleTrackerReplyAlert(static_cast<tracker_reply_alert*>(a));
            break;
        case tracker_warning_alert::alert_type:
            handleTrackerWarningAlert(static_cast<tracker_warning_alert*>(a));
            break;
        case portmap_error_alert::alert_type:
            handlePortmapWarningAlert(static_cast<portmap_error_alert*>(a));
            break;
        case portmap_alert::alert_type:
            handlePortmapAlert(static_cast<portmap_alert*>(a));
            break;
        case peer_blocked_alert::alert_type:
            handlePeerBlockedAlert(static_cast<peer_blocked_alert*>(a));
            break;
        case peer_ban_alert::alert_type:
            handlePeerBanAlert(static_cast<peer_ban_alert*>(a));
            break;
        case fastresume_rejected_alert::alert_type:
            handleFastResumeRejectedAlert(static_cast<fastresume_rejected_alert*>(a));
            break;
        case url_seed_alert::alert_type:
            handleUrlSeedAlert(static_cast<url_seed_alert*>(a));
            break;
        case listen_succeeded_alert::alert_type:
            handleListenSucceededAlert(static_cast<listen_succeeded_alert*>(a));
            break;
        case listen_failed_alert::alert_type:
            handleListenFailedAlert(static_cast<listen_failed_alert*>(a));
            break;
        case torrent_checked_alert::alert_type:
            handleTorrentCheckedAlert(static_cast<torrent_checked_alert*>(a));
            break;
        case external_ip_alert::alert_type:
            handleExternalIPAlert(static_cast<external_ip_alert*>(a));
            break;
        case state_update_alert::alert_type:
            handleStateUpdateAlert(static_cast<state_update_alert*>(a));
            break;
        case stats_alert::alert_type:
            handleStatsAlert(static_cast<stats_alert*>(a));
            break;
        }
    } catch (const std::exception& e) {
        qWarning() << "Caught exception in readAlerts(): " << misc::toQStringU(e.what());
    }
}

void QBtSession::handleTorrentFinishedAlert(libtorrent::torrent_finished_alert* p) {
    QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        const QString hash = h.hash();
        qDebug("Got a torrent finished alert for %s", qPrintable(h.name()));
        // Remove .!qB extension if necessary
        if (appendqBExtension)
            appendqBextensionToTorrent(h, false);

        TorrentPersistentData* const TorPersistent = TorrentPersistentData::instance();
        const bool was_already_seeded = TorPersistent->isSeed(hash);
        qDebug("Was already seeded: %d", was_already_seeded);
        if (!was_already_seeded) {
            h.save_resume_data();
            qDebug("Checking if the torrent contains torrent files to download");
            // Check if there are torrent files inside
            for (int i=0; i<h.num_files(); ++i) {
                const QString torrent_relpath = h.filepath_at(i);
                qDebug() << "File path:" << torrent_relpath;
                if (torrent_relpath.endsWith(".torrent", Qt::CaseInsensitive)) {
                    qDebug("Found possible recursive torrent download.");
                    const QString torrent_fullpath = h.save_path()+"/"+torrent_relpath;
                    qDebug("Full subtorrent path is %s", qPrintable(torrent_fullpath));
                    try {
                        std::vector<char> buffer;
                        lazy_entry entry;
                        libtorrent::error_code ec;
                        misc::loadBencodedFile(torrent_fullpath, buffer, entry, ec);
                        boost::intrusive_ptr<torrent_info> t = new torrent_info(entry);
                        if (t->is_valid()) {
                            qDebug("emitting recursiveTorrentDownloadPossible()");
                            emit recursiveTorrentDownloadPossible(h);
                            break;
                        }
                    }
                    catch(std::exception&) {
                        qDebug("Caught error loading torrent");
                        Logger::instance()->addMessage(tr("Unable to decode %1 torrent file.").arg(fsutils::toNativePath(torrent_fullpath)), Log::CRITICAL);
                    }
                }
            }
            // Move to download directory if necessary
            if (!defaultTempPath.isEmpty()) {
                // Check if directory is different
                const QDir current_dir(h.save_path());
                const QDir save_dir(getSavePath(hash));
                if (current_dir != save_dir) {
                    qDebug("Moving torrent from the temp folder");
                    h.move_storage(save_dir.absolutePath());
                }
            }
            // Remember finished state
            qDebug("Saving seed status");
            TorPersistent->saveSeedStatus(h);
            // Recheck if the user asked to
            Preferences* const pref = Preferences::instance();
            if (pref->recheckTorrentsOnCompletion()) {
                h.force_recheck();
            }
            qDebug("Emitting finishedTorrent() signal");
            emit finishedTorrent(h);
            qDebug("Received finished alert for %s", qPrintable(h.name()));

            // AutoRun program
            if (pref->isAutoRunEnabled())
                autoRunExternalProgram(h);
            // Move .torrent file to another folder
            if (pref->isFinishedTorrentExportEnabled())
                exportTorrentFile(h, FinishedTorrentExportFolder);
            // Mail notification
            if (pref->isMailNotificationEnabled())
                sendNotificationEmail(h);

#ifndef DISABLE_GUI
            // Auto-Shutdown
            bool will_shutdown = (pref->shutdownWhenDownloadsComplete() ||
                                  pref->shutdownqBTWhenDownloadsComplete() ||
                                  pref->suspendWhenDownloadsComplete() ||
                                  pref->hibernateWhenDownloadsComplete())
                    && !hasDownloadingTorrents();

            if (will_shutdown) {
                bool suspend = pref->suspendWhenDownloadsComplete();
                bool hibernate = pref->hibernateWhenDownloadsComplete();
                bool shutdown = pref->shutdownWhenDownloadsComplete();
                // Confirm shutdown
                shutDownAction action = NO_SHUTDOWN;

                if (suspend)
                    action = SUSPEND_COMPUTER;
                else if (hibernate)
                    action = HIBERNATE_COMPUTER;
                else if (shutdown)
                    action = SHUTDOWN_COMPUTER;
                if (!ShutdownConfirmDlg::askForConfirmation(action))
                    return;

                // Actually shut down
                if (suspend || hibernate || shutdown) {
                    qDebug("Preparing for auto-shutdown because all downloads are complete!");
                    // Disabling it for next time
                    pref->setShutdownWhenDownloadsComplete(false);
                    pref->setSuspendWhenDownloadsComplete(false);
                    pref->setHibernateWhenDownloadsComplete(false);
                    // Make sure preferences are synced before exiting
                    m_shutdownAct = action;
                }
                qDebug("Exiting the application");
                qApp->exit();
                return;
            }
#endif // DISABLE_GUI
        }
    }
}

void QBtSession::handleSaveResumeDataAlert(libtorrent::save_resume_data_alert* p) {
    const QDir torrentBackup(fsutils::BTBackupLocation());
    const QTorrentHandle h(p->handle);
    if (h.is_valid() && p->resume_data) {
        const QString filepath = torrentBackup.absoluteFilePath(h.hash()+".fastresume");
        QFile resume_file(filepath);
        if (resume_file.exists())
            fsutils::forceRemove(filepath);
        qDebug("Saving fastresume data in %s", qPrintable(filepath));
        backupPersistentData(h.hash(), p->resume_data);
        vector<char> out;
        bencode(back_inserter(out), *p->resume_data);
        if (!out.empty() && resume_file.open(QIODevice::WriteOnly)) {
            resume_file.write(&out[0], out.size());
            resume_file.close();
        }
    }
}

void QBtSession::handleFileRenamedAlert(libtorrent::file_renamed_alert* p) {
    QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        if (h.num_files() > 1) {
            // Check if folders were renamed
            QStringList old_path_parts = h.orig_filepath_at(p->index).split("/");
            old_path_parts.removeLast();
            QString old_path = old_path_parts.join("/");
            QStringList new_path_parts = fsutils::fromNativePath(misc::toQStringU(p->name)).split("/");
            new_path_parts.removeLast();
            if (!new_path_parts.isEmpty() && old_path != new_path_parts.join("/")) {
                qDebug("Old_path(%s) != new_path(%s)", qPrintable(old_path), qPrintable(new_path_parts.join("/")));
                old_path = h.save_path()+"/"+old_path;
                qDebug("Detected folder renaming, attempt to delete old folder: %s", qPrintable(old_path));
                QDir().rmpath(old_path);
            }
        } else {
            // Single-file torrent
            // Renaming a file corresponds to changing the save path
            emit savePathChanged(h);
        }
    }
}

void QBtSession::handleTorrentDeletedAlert(libtorrent::torrent_deleted_alert* p) {
    qDebug("A torrent was deleted from the hard disk, attempting to remove the root folder too...");
    QString hash = misc::toQString(p->info_hash);
    if (!hash.isEmpty()) {
        if (savePathsToRemove.contains(hash)) {
            const QString dirpath = savePathsToRemove.take(hash);
            qDebug() << "Removing save path: " << dirpath << "...";
            bool ok = fsutils::smartRemoveEmptyFolderTree(dirpath);
            Q_UNUSED(ok);
            qDebug() << "Folder was removed: " << ok;
        }
    } else {
        // Fallback
        qDebug() << "hash is empty, use fallback to remove save path";
        foreach (const QString& key, savePathsToRemove.keys()) {
            // Attempt to delete
            if (QDir().rmdir(savePathsToRemove[key])) {
                savePathsToRemove.remove(key);
            }
        }
    }
}

void QBtSession::handleStorageMovedAlert(libtorrent::storage_moved_alert* p) {
    QTorrentHandle h(p->handle);
    if (!h.is_valid()) {
        qWarning("invalid handle received in storage_moved_alert");
        return;
    }

    QString hash = h.hash();

    if (!TorrentTempData::isMoveInProgress(hash)) {
        qWarning("unexpected storage_moved_alert received");
        return;
    }

    QString new_save_path = fsutils::fromNativePath(misc::toQStringU(p->path.c_str()));
    if (new_save_path != fsutils::fromNativePath(TorrentTempData::getNewPath(hash))) {
        qWarning("new path received in handleStorageMovedAlert() doesn't match a path in a queue");
        return;
    }

    QString oldPath = fsutils::fromNativePath(TorrentTempData::getOldPath(hash));

    qDebug("Torrent is successfully moved from %s to %s", qPrintable(oldPath), qPrintable(new_save_path));

    // Attempt to remove old folder if empty
    QDir old_save_dir(oldPath);
    if (old_save_dir != QDir(defaultSavePath) && old_save_dir != QDir(defaultTempPath)) {
        qDebug("Attempting to remove %s", qPrintable(oldPath));
        QDir().rmpath(oldPath);
    }
    if (defaultTempPath.isEmpty() || !new_save_path.startsWith(defaultTempPath)) {
        qDebug("Storage has been moved, updating save path to %s", qPrintable(new_save_path));
        TorrentPersistentData::instance()->saveSavePath(h.hash(), new_save_path);
    }
    emit savePathChanged(h);
    //h.force_recheck();

    QString queued = TorrentTempData::getQueuedPath(hash);
    if (!queued.isEmpty()) {
        TorrentTempData::finishMove(hash);
        h.move_storage(queued);
    }
    else {
        TorrentTempData::finishMove(hash);
    }
}

void QBtSession::handleStorageMovedFailedAlert(libtorrent::storage_moved_failed_alert* p) {

    QTorrentHandle h(p->handle);
    if (!h.is_valid()) {
        qWarning("invalid handle received in storage_moved_failed_alert");
        return;
    }

    QString hash = h.hash();

    if (!TorrentTempData::isMoveInProgress(hash)) {
        qWarning("unexpected storage_moved_alert received");
        return;
    }

    Logger* const logger = Logger::instance();
    logger->addMessage(tr("Could not move torrent: '%1'. Reason: %2").arg(h.name()).arg(misc::toQStringU(p->message())), Log::CRITICAL);

    QString queued = TorrentTempData::getQueuedPath(hash);
    if (!queued.isEmpty()) {
        TorrentTempData::finishMove(hash);
        logger->addMessage(tr("Attempting to move torrent: '%1' to path: '%2'.").arg(h.name()).arg(fsutils::toNativePath(queued)));
        h.move_storage(queued);
    }
    else {
        TorrentTempData::finishMove(hash);
    }
}

void QBtSession::handleMetadataReceivedAlert(libtorrent::metadata_received_alert* p) {
    QTorrentHandle h(p->handle);
    Preferences* const pref = Preferences::instance();
    if (h.is_valid()) {
        QString hash(h.hash());
        if (HiddenData::hasData(hash)) {
            HiddenData::gotMetadata(hash);
            if (pref->isQueueingSystemEnabled()) {
                //Internally decrease the queue limits to ensure that that other queued items aren't started
                libtorrent::session_settings sessionSettings(s->settings());
                int max_downloading = pref->getMaxActiveDownloads();
                int max_active = pref->getMaxActiveTorrents();
                if (max_downloading > -1)
                    sessionSettings.active_downloads = max_downloading + HiddenData::getDownloadingSize();
                else
                    sessionSettings.active_downloads = max_downloading;
                if (max_active > -1)
                    sessionSettings.active_limit = max_active + HiddenData::getDownloadingSize();
                else
                    sessionSettings.active_limit = max_active;
                s->set_settings(sessionSettings);
            }
            h.pause();
        }
        qDebug("Received metadata for %s", qPrintable(h.hash()));
        // Save metadata
        const QDir torrentBackup(fsutils::BTBackupLocation());
        if (!QFile::exists(torrentBackup.absoluteFilePath(h.hash()+QString(".torrent"))))
            h.save_torrent_file(torrentBackup.absoluteFilePath(h.hash()+QString(".torrent")));
        // Copy the torrent file to the export folder
        if (m_torrentExportEnabled)
            exportTorrentFile(h);
        // Append .!qB to incomplete files
        if (appendqBExtension)
            appendqBextensionToTorrent(h, true);

        if (!HiddenData::hasData(hash))
            emit metadataReceived(h);
        else
            emit metadataReceivedHidden(h);

        if (h.is_paused() && !HiddenData::hasData(hash)) {
            // XXX: Unfortunately libtorrent-rasterbar does not send a torrent_paused_alert
            // and the torrent can be paused when metadata is received
            emit pausedTorrent(h);
        }
    }
}

void QBtSession::handleFileErrorAlert(libtorrent::file_error_alert* p) {
    QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        h.pause();
        std::cerr << "File Error: " << p->message().c_str() << std::endl;
        Logger* const logger = Logger::instance();
        logger->addMessage(tr("An I/O error occurred, '%1' paused.").arg(h.name()));
        logger->addMessage(tr("Reason: %1").arg(misc::toQStringU(p->message())));
        if (h.is_valid()) {
            emit fullDiskError(h, misc::toQStringU(p->message()));
            //h.pause();
            emit pausedTorrent(h);
        }
    }
}

void QBtSession::handleFileCompletedAlert(libtorrent::file_completed_alert* p) {
    QTorrentHandle h(p->handle);
    qDebug("A file completed download in torrent %s", qPrintable(h.name()));
    if (appendqBExtension) {
        qDebug("appendqBTExtension is true");
        QString name = h.filepath_at(p->index);
        if (name.endsWith(".!qB")) {
            const QString old_name = name;
            name.chop(4);
            qDebug("Renaming %s to %s", qPrintable(old_name), qPrintable(name));
            h.rename_file(p->index, name);
        }
    }
}

void QBtSession::handleTorrentPausedAlert(libtorrent::torrent_paused_alert* p) {
    if (p->handle.is_valid()) {
        QTorrentHandle h(p->handle);
        if (!HiddenData::hasData(h.hash())) {
            if (!h.has_error() && !TorrentPersistentData::instance()->getHasMissingFiles(h.hash()))
                h.save_resume_data();
            emit pausedTorrent(h);
        }
    }
}

void QBtSession::handleTrackerErrorAlert(libtorrent::tracker_error_alert* p) {
    // Level: fatal
    QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        const QString hash = h.hash();
        // Authentication
        if (p->status_code != 401) {
            qDebug("Received a tracker error for %s: %s", p->url.c_str(), p->msg.c_str());
            const QString tracker_url = misc::toQString(p->url);
            QHash<QString, TrackerInfos> trackers_data = trackersInfos.value(hash, QHash<QString, TrackerInfos>());
            TrackerInfos data = trackers_data.value(tracker_url, TrackerInfos(tracker_url));
            data.last_message = misc::toQStringU(p->msg);
            trackers_data.insert(tracker_url, data);
            trackersInfos[hash] = trackers_data;
        }
        else {
            emit trackerAuthenticationRequired(h);
        }
        emit trackerError(hash, misc::toQStringU(p->url));
    }
}

void QBtSession::handleTrackerReplyAlert(libtorrent::tracker_reply_alert* p) {
    const QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        qDebug("Received a tracker reply from %s (Num_peers=%d)", p->url.c_str(), p->num_peers);
        // Connection was successful now. Remove possible old errors
        const QString hash = h.hash();
        QHash<QString, TrackerInfos> trackers_data = trackersInfos.value(hash, QHash<QString, TrackerInfos>());
        const QString tracker_url = misc::toQString(p->url);
        TrackerInfos data = trackers_data.value(tracker_url, TrackerInfos(tracker_url));
        data.last_message = ""; // Reset error/warning message
        data.num_peers = p->num_peers;
        trackers_data.insert(tracker_url, data);
        trackersInfos[hash] = trackers_data;
        emit trackerSuccess(hash, misc::toQStringU(p->url));
    }
}

void QBtSession::handleTrackerWarningAlert(libtorrent::tracker_warning_alert* p) {
    const QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        // Connection was successful now but there is a warning message
        const QString hash = h.hash();
        QHash<QString, TrackerInfos> trackers_data = trackersInfos.value(hash, QHash<QString, TrackerInfos>());
        const QString tracker_url = misc::toQString(p->url);
        TrackerInfos data = trackers_data.value(tracker_url, TrackerInfos(tracker_url));
        data.last_message = misc::toQStringU(p->msg); // Store warning message
        trackers_data.insert(tracker_url, data);
        trackersInfos[hash] = trackers_data;
        qDebug("Received a tracker warning from %s: %s", p->url.c_str(), p->msg.c_str());
        emit trackerWarning(hash, misc::toQStringU(p->url));
    }
}

void QBtSession::handlePortmapWarningAlert(libtorrent::portmap_error_alert* p) {
    Logger::instance()->addMessage(tr("UPnP/NAT-PMP: Port mapping failure, message: %1").arg(misc::toQStringU(p->message())), Log::CRITICAL);
}

void QBtSession::handlePortmapAlert(libtorrent::portmap_alert* p) {
    qDebug("UPnP Success, msg: %s", p->message().c_str());
    Logger::instance()->addMessage(tr("UPnP/NAT-PMP: Port mapping successful, message: %1").arg(misc::toQStringU(p->message())), Log::INFO);
}

void QBtSession::handlePeerBlockedAlert(libtorrent::peer_blocked_alert* p)
{
    boost::system::error_code ec;
    string ip = p->ip.to_string(ec);
#if LIBTORRENT_VERSION_NUM < 10000
    if (!ec)
        Logger::instance()->addPeer(QString::fromLatin1(ip.c_str()), true);
#else
    QString reason;
    switch (p->reason) {
    case peer_blocked_alert::ip_filter:
        reason = tr("due to IP filter.", "this peer was blocked due to ip filter.");
        break;
    case peer_blocked_alert::port_filter:
        reason = tr("due to port filter.", "this peer was blocked due to port filter.");
        break;
    case peer_blocked_alert::i2p_mixed:
        reason = tr("due to i2p mixed mode restrictions.", "this peer was blocked due to i2p mixed mode restrictions.");
        break;
    case peer_blocked_alert::privileged_ports:
        reason = tr("because it has a low port.", "this peer was blocked because it has a low port.");
        break;
    case peer_blocked_alert::utp_disabled:
        reason = trUtf8("because %1 is disabled.", "this peer was blocked because uTP is disabled.").arg(QString::fromUtf8(C_UTP)); // don't translate μTP
        break;
    case peer_blocked_alert::tcp_disabled:
        reason = tr("because %1 is disabled.", "this peer was blocked because TCP is disabled.").arg("TCP"); // don't translate TCP
        break;
    }

    if (!ec)
        Logger::instance()->addPeer(QString::fromLatin1(ip.c_str()), true, reason);
#endif
}

void QBtSession::handlePeerBanAlert(libtorrent::peer_ban_alert* p) {
    boost::system::error_code ec;
    string ip = p->ip.address().to_string(ec);
    if (!ec)
        Logger::instance()->addPeer(QString::fromLatin1(ip.c_str()), false);
}

void QBtSession::handleFastResumeRejectedAlert(libtorrent::fastresume_rejected_alert* p) {
    Logger* const logger = Logger::instance();
    QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        qDebug("/!\\ Fast resume failed for %s, reason: %s", qPrintable(h.name()), p->message().c_str());
        if (p->error.value() == errors::mismatching_file_size) {
            // Mismatching file size (files were probably moved)
            const QString hash = h.hash();
            logger->addMessage(tr("File sizes mismatch for torrent %1, pausing it.").arg(h.name()), Log::CRITICAL);
            TorrentPersistentData::instance()->setHasMissingFiles(h.hash(), true);
            pauseTorrent(hash);
        } else {
            logger->addMessage(tr("Fast resume data was rejected for torrent %1, checking again...").arg(h.name()), Log::CRITICAL);
            logger->addMessage(tr("Reason: %1").arg(misc::toQStringU(p->message())));
        }
    }
}

void QBtSession::handleUrlSeedAlert(libtorrent::url_seed_alert* p) {
    Logger::instance()->addMessage(tr("URL seed lookup failed for url: %1, message: %2").arg(misc::toQString(p->url)).arg(misc::toQStringU(p->message())), Log::CRITICAL);
}

void QBtSession::handleListenSucceededAlert(libtorrent::listen_succeeded_alert *p) {
    boost::system::error_code ec;
    QString proto = "TCP";
#if LIBTORRENT_VERSION_NUM >= 10000
    if (p->sock_type == listen_succeeded_alert::udp)
        proto = "UDP";
    else if (p->sock_type == listen_succeeded_alert::tcp)
        proto = "TCP";
    else if (p->sock_type == listen_succeeded_alert::tcp_ssl)
        proto = "TCP_SSL";
#endif
    qDebug() << "Successfully listening on " << proto << p->endpoint.address().to_string(ec).c_str() << "/" << p->endpoint.port();
    Logger::instance()->addMessage(tr("qBittorrent is successfully listening on interface %1 port: %2/%3", "e.g: qBittorrent is successfully listening on interface 192.168.0.1 port: TCP/6881").arg(p->endpoint.address().to_string(ec).c_str()).arg(proto).arg(QString::number(p->endpoint.port())), Log::INFO);
    // Force reannounce on all torrents because some trackers blacklist some ports
    std::vector<torrent_handle> torrents = s->get_torrents();

    std::vector<torrent_handle>::iterator it = torrents.begin();
    std::vector<torrent_handle>::iterator itend = torrents.end();
    for ( ; it != itend; ++it) {
        it->force_reannounce();
    }
}

void QBtSession::handleListenFailedAlert(libtorrent::listen_failed_alert *p) {
    boost::system::error_code ec;
    QString proto = "TCP";
#if LIBTORRENT_VERSION_NUM >= 10000
    if (p->sock_type == listen_failed_alert::udp)
        proto = "UDP";
    else if (p->sock_type == listen_failed_alert::tcp)
        proto = "TCP";
    else if (p->sock_type == listen_failed_alert::tcp_ssl)
        proto = "TCP_SSL";
    else if (p->sock_type == listen_failed_alert::i2p)
        proto = "I2P";
    else if (p->sock_type == listen_failed_alert::socks5)
        proto = "SOCKS5";
#endif
    qDebug() << "Failed listening on " << proto << p->endpoint.address().to_string(ec).c_str() << "/" << p->endpoint.port();
    Logger::instance()->addMessage(tr("qBittorrent failed listening on interface %1 port: %2/%3. Reason: %4", "e.g: qBittorrent failed listening on interface 192.168.0.1 port: TCP/6881. Reason: already in use").arg(p->endpoint.address().to_string(ec).c_str()).arg(proto).arg(QString::number(p->endpoint.port())).arg(misc::toQStringU(p->error.message())), Log::CRITICAL);

}

void QBtSession::handleTorrentCheckedAlert(libtorrent::torrent_checked_alert* p) {
    QTorrentHandle h(p->handle);
    if (h.is_valid()) {
        const QString hash = h.hash();
        qDebug("%s have just finished checking", qPrintable(hash));
        // Save seed status
        TorrentPersistentData::instance()->saveSeedStatus(h);
        // Move to temp directory if necessary
        if (!h.is_seed() && !defaultTempPath.isEmpty()) {
            // Check if directory is different
            const QDir current_dir(h.save_path());
            const QDir save_dir(getSavePath(h.hash()));
            if (current_dir == save_dir) {
                qDebug("Moving the torrent to the temp directory...");
                QString torrent_tmp_path = defaultTempPath;
                h.move_storage(torrent_tmp_path);
            }
        }
        emit torrentFinishedChecking(h);
        if (torrentsToPausedAfterChecking.contains(hash)) {
            torrentsToPausedAfterChecking.removeOne(hash);
            h.pause();
            emit pausedTorrent(h);
        }
    }
}

void QBtSession::handleExternalIPAlert(libtorrent::external_ip_alert *p) {
    boost::system::error_code ec;
    Logger::instance()->addMessage(tr("External IP: %1", "e.g. External IP: 192.168.0.1").arg(p->external_address.to_string(ec).c_str()), Log::INFO);
}

void QBtSession::handleStateUpdateAlert(libtorrent::state_update_alert *p) {
    emit stateUpdate(p->status);
}

void QBtSession::handleStatsAlert(libtorrent::stats_alert *p) {
    emit statsReceived(*p);
}

void QBtSession::recheckTorrent(const QString &hash) {
    QTorrentHandle h = getTorrentHandle(hash);
    if (h.is_valid() && h.has_metadata()) {
        if (h.is_paused()) {
            if (!torrentsToPausedAfterChecking.contains(h.hash())) {
                torrentsToPausedAfterChecking << h.hash();
                h.resume();
            }
        }
        h.force_recheck();
    }
}

QHash<QString, TrackerInfos> QBtSession::getTrackersInfo(const QString &hash) const {
    return trackersInfos.value(hash, QHash<QString, TrackerInfos>());
}

int QBtSession::getListenPort() const {
    qDebug() << Q_FUNC_INFO << s->listen_port();
    return s->listen_port();
}

session_status QBtSession::getSessionStatus() const {
    return s->status();
}

void QBtSession::applyEncryptionSettings(pe_settings se) {
    qDebug("Applying encryption settings");
    s->set_pe_settings(se);
}

// Set Proxy
void QBtSession::setProxySettings(proxy_settings proxySettings) {
    qDebug() << Q_FUNC_INFO;

    proxySettings.proxy_peer_connections = Preferences::instance()->proxyPeerConnections();
    s->set_proxy(proxySettings);

    // Define environment variable
    QString proxy_str;
    switch(proxySettings.type) {
    case proxy_settings::http_pw:
        proxy_str = "http://"+misc::toQString(proxySettings.username)+":"+misc::toQString(proxySettings.password)+"@"+misc::toQString(proxySettings.hostname)+":"+QString::number(proxySettings.port);
        break;
    case proxy_settings::http:
        proxy_str = "http://"+misc::toQString(proxySettings.hostname)+":"+QString::number(proxySettings.port);
        break;
    case proxy_settings::socks5:
        proxy_str = misc::toQString(proxySettings.hostname)+":"+QString::number(proxySettings.port);
        break;
    case proxy_settings::socks5_pw:
        proxy_str = misc::toQString(proxySettings.username)+":"+misc::toQString(proxySettings.password)+"@"+misc::toQString(proxySettings.hostname)+":"+QString::number(proxySettings.port);
        break;
    default:
        qDebug("Disabling HTTP communications proxy");
        qputenv("http_proxy", QByteArray());
        qputenv("sock_proxy", QByteArray());
        return;
    }
    // We need this for urllib in search engine plugins
    qDebug("HTTP communications proxy string: %s", qPrintable(proxy_str));
    if (proxySettings.type == proxy_settings::socks5 || proxySettings.type == proxy_settings::socks5_pw)
        qputenv("sock_proxy", proxy_str.toLocal8Bit());
    else
        qputenv("http_proxy", proxy_str.toLocal8Bit());
}

// Set BT session settings (user_agent)
void QBtSession::setSessionSettings(const session_settings &sessionSettings) {
    qDebug("Set session settings");
    s->set_settings(sessionSettings);
}

QString QBtSession::getSavePath(const QString &hash, bool fromScanDir, QString filePath, bool imported) {
    QString savePath;
    if (TorrentTempData::hasTempData(hash)) {
        savePath = fsutils::fromNativePath(TorrentTempData::getSavePath(hash));
        if (savePath.isEmpty()) {
            savePath = defaultSavePath;
        }
        if (!imported && appendLabelToSavePath) {
            qDebug("appendLabelToSavePath is true");
            const QString label = TorrentTempData::getLabel(hash);
            if (!label.isEmpty()) {
                savePath = fsutils::updateLabelInSavePath(defaultSavePath, savePath, "", label);
            }
        }
        qDebug("getSavePath, got save_path from temp data: %s", qPrintable(savePath));
    } else {
        savePath = fsutils::fromNativePath(TorrentPersistentData::instance()->getSavePath(hash));
        qDebug("SavePath got from persistant data is %s", qPrintable(savePath));
        if (savePath.isEmpty()) {
            if (fromScanDir && m_scanFolders->downloadInTorrentFolder(filePath)) {
                savePath = QFileInfo(filePath).dir().path();
            } else {
                savePath = defaultSavePath;
            }
        }
        if (!fromScanDir && appendLabelToSavePath) {
            const QString label = TorrentPersistentData::instance()->getLabel(hash);
            if (!label.isEmpty()) {
                qDebug("Torrent label is %s", qPrintable(label));
                savePath = fsutils::updateLabelInSavePath(defaultSavePath, savePath, "", label);
            }
        }
        qDebug("getSavePath, got save_path from persistent data: %s", qPrintable(savePath));
    }
    // Clean path
    savePath = fsutils::expandPathAbs(savePath);
    if (!savePath.endsWith("/"))
        savePath += "/";
    return savePath;
}

// Take an url string to a torrent file,
// download the torrent file to a tmp location, then
// add it to download list
void QBtSession::downloadFromUrl(const QString &url, const QList<QNetworkCookie>& cookies)
{
    Logger::instance()->addMessage(tr("Downloading '%1', please wait...", "e.g: Downloading 'xxx.torrent', please wait...").arg(url));
    // Launch downloader thread
    downloader->downloadTorrentUrl(url, cookies);
}

void QBtSession::downloadFromURLList(const QStringList& urls) {
    foreach (const QString &url, urls) {
        downloadFromUrl(url);
    }
}

void QBtSession::addMagnetInteractive(const QString& uri)
{
    emit newMagnetLink(uri);
}

#ifndef DISABLE_GUI
void QBtSession::addMagnetSkipAddDlg(const QString& uri, const QString& save_path, const QString& label,
                                     const RssDownloadRule::AddPausedState &aps, const QString &uri_old) {
#else
void QBtSession::addMagnetSkipAddDlg(const QString& uri, const QString& save_path, const QString& label, const QString &uri_old) {
#endif
    if (!save_path.isEmpty() || !label.isEmpty())
        savepathLabel_fromurl[uri] = qMakePair(fsutils::fromNativePath(save_path), label);

#ifndef DISABLE_GUI
    QString hash = misc::magnetUriToHash(uri);
    switch (aps) {
    case RssDownloadRule::ALWAYS_PAUSED:
        TorrentTempData::setAddPaused(hash, true);
        break;
    case RssDownloadRule::NEVER_PAUSED:
        TorrentTempData::setAddPaused(hash, false);
        break;
    case RssDownloadRule::USE_GLOBAL:
    default:;
        // Use global preferences
    }
#endif

    addMagnetUri(uri, false);
    emit newDownloadedTorrentFromRss(uri_old.isEmpty() ? uri : uri_old);
}

#ifndef DISABLE_GUI
void QBtSession::downloadUrlAndSkipDialog(QString url, QString save_path, QString label,
                                          const QList<QNetworkCookie>& cookies, const RssDownloadRule::AddPausedState &aps) {
#else
void QBtSession::downloadUrlAndSkipDialog(QString url, QString save_path, QString label, const QList<QNetworkCookie>& cookies) {
#endif
    //emit aboutToDownloadFromUrl(url);
    const QUrl qurl = QUrl::fromEncoded(url.toUtf8());
    if (!save_path.isEmpty() || !label.isEmpty())
        savepathLabel_fromurl[qurl] = qMakePair(fsutils::fromNativePath(save_path), label);

#ifndef DISABLE_GUI
    if (aps != RssDownloadRule::USE_GLOBAL)
        addpaused_fromurl[qurl] = aps;
#endif
    url_skippingDlg << qurl;
    // Launch downloader thread
    downloader->downloadTorrentUrl(url, cookies);
}

// Add to Bittorrent session the downloaded torrent file
void QBtSession::processDownloadedFile(QString url, QString file_path) {
    const int index = url_skippingDlg.indexOf(QUrl::fromEncoded(url.toUtf8()));
    if (index < 0) {
        // Add file to torrent download list
        file_path = fsutils::fromNativePath(file_path);
#ifdef Q_OS_WIN
        // Windows hack
        if (!file_path.endsWith(".torrent", Qt::CaseInsensitive)) {
            Q_ASSERT(QFile::exists(file_path));
            qDebug("Torrent name does not end with .torrent, from %s", qPrintable(file_path));
            if (QFile::rename(file_path, file_path+".torrent")) {
                file_path += ".torrent";
            } else {
                qDebug("Failed to rename torrent file!");
            }
        }
        qDebug("Downloading torrent at path: %s", qPrintable(file_path));
#endif
        emit newDownloadedTorrent(file_path, url);
    } else {
        url_skippingDlg.removeAt(index);

#ifndef DISABLE_GUI
        libtorrent::error_code ec;
        // Get hash
        libtorrent::torrent_info ti(file_path.toStdString(), ec);
        QString hash;

        if (!ec) {
            hash = misc::toQString(ti.info_hash());
            RssDownloadRule::AddPausedState aps = addpaused_fromurl[url];
            addpaused_fromurl.remove(url);
            switch (aps) {
            case RssDownloadRule::ALWAYS_PAUSED:
                TorrentTempData::setAddPaused(hash, true);
                break;
            case RssDownloadRule::NEVER_PAUSED:
                TorrentTempData::setAddPaused(hash, false);
                break;
            case RssDownloadRule::USE_GLOBAL:
            default:;
                // Use global preferences
            }
        }
#endif

        addTorrent(file_path, false, url, false);
        emit newDownloadedTorrentFromRss(url);
    }
}

// Return current download rate for the BT
// session. Payload means that it only take into
// account "useful" part of the rate
qreal QBtSession::getPayloadDownloadRate() const {
    return s->status().payload_download_rate;
}

// Return current upload rate for the BT
// session. Payload means that it only take into
// account "useful" part of the rate
qreal QBtSession::getPayloadUploadRate() const {
    return s->status().payload_upload_rate;
}

// Will fast resume torrents in
// backup directory
void QBtSession::startUpTorrents() {
    qDebug("Resuming unfinished torrents");
    const QDir torrentBackup(fsutils::BTBackupLocation());
    const TorrentPersistentData* const TorPersistent = TorrentPersistentData::instance();
    const QStringList known_torrents = TorPersistent->knownTorrents();

    // Safety measure because some people reported torrent loss since
    // we switch the v1.5 way of resuming torrents on startup
    QStringList filters;
    filters << "*.torrent";
    const QStringList torrents_on_hd = torrentBackup.entryList(filters, QDir::Files, QDir::Unsorted);
    foreach (QString hash, torrents_on_hd) {
        hash.chop(8); // remove trailing .torrent
        if (!known_torrents.contains(hash)) {
            qDebug("found torrent with hash: %s on hard disk", qPrintable(hash));
            std::cerr << "ERROR Detected!!! Adding back torrent " << qPrintable(hash) << " which got lost for some reason." << std::endl;
            addTorrent(torrentBackup.path()+"/"+hash+".torrent", false, QString(), true);
        }
    }
    // End of safety measure

    qDebug("Starting up torrents");
    if (isQueueingEnabled()) {
        priority_queue<QPair<int, QString>, vector<QPair<int, QString> >, std::greater<QPair<int, QString> > > torrent_queue;
        foreach (const QString &hash, known_torrents) {
            const int prio = TorPersistent->getPriority(hash);
            torrent_queue.push(qMakePair(prio, hash));
        }
        qDebug("Priority_queue size: %ld", (long)torrent_queue.size());
        // Resume downloads
        while(!torrent_queue.empty()) {
            const QString hash = torrent_queue.top().second;
            torrent_queue.pop();
            qDebug("Starting up torrent %s", qPrintable(hash));
            if (TorPersistent->isMagnet(hash)) {
                addMagnetUri(TorPersistent->getMagnetUri(hash), true);
            } else {
                addTorrent(torrentBackup.path()+"/"+hash+".torrent", false, QString(), true);
            }
        }
    } else {
        // Resume downloads
        foreach (const QString &hash, known_torrents) {
            qDebug("Starting up torrent %s", qPrintable(hash));
            if (TorPersistent->isMagnet(hash))
                addMagnetUri(TorPersistent->getMagnetUri(hash), true);
            else
                addTorrent(torrentBackup.path()+"/"+hash+".torrent", false, QString(), true);
        }
    }
    qDebug("Unfinished torrents resumed");
}

QBtSession * QBtSession::instance()
{
    if (!m_instance) {
        m_instance = new QBtSession;
    }
    return m_instance;
}

void QBtSession::drop()
{
    if (m_instance) {
        delete m_instance;
        m_instance = 0;
    }
}

qlonglong QBtSession::getETA(const QString &hash, const libtorrent::torrent_status &status) const
{
    return m_speedMonitor->getETA(hash, status);
}

quint64 QBtSession::getAlltimeDL() const {
    return m_torrentStatistics->getAlltimeDL();
}

quint64 QBtSession::getAlltimeUL() const {
    return m_torrentStatistics->getAlltimeUL();
}

void QBtSession::postTorrentUpdate() {
    s->post_torrent_updates();
}

void QBtSession::handleIPFilterParsed(int ruleCount)
{
    Logger::instance()->addMessage(tr("Successfully parsed the provided IP filter: %1 rules were applied.", "%1 is a number").arg(ruleCount));
    emit ipFilterParsed(false, ruleCount);
}

void QBtSession::handleIPFilterError()
{
    Logger::instance()->addMessage(tr("Error: Failed to parse the provided IP filter."), Log::CRITICAL);
    emit ipFilterParsed(true, 0);
}

void QBtSession::recoverPersistentData(const QString &hash, const std::vector<char> &buf) {
    TorrentPersistentData* const TorPersistent = TorrentPersistentData::instance();
    if (TorPersistent->isKnownTorrent(hash) || TorrentTempData::hasTempData(hash) || buf.empty())
        return;

    libtorrent::lazy_entry fast;
    libtorrent::error_code ec;

    libtorrent::lazy_bdecode(&(buf.front()), &(buf.back()), fast, ec);
    if (fast.type() != libtorrent::lazy_entry::dict_t && !ec)
        return;

    QString savePath = fsutils::fromNativePath(QString::fromUtf8(fast.dict_find_string_value("qBt-savePath").c_str()));
    qreal ratioLimit = QString::fromUtf8(fast.dict_find_string_value("qBt-ratioLimit").c_str()).toDouble();
    QDateTime addedDate = QDateTime::fromTime_t(fast.dict_find_int_value("added_time"));
    QString label = QString::fromUtf8(fast.dict_find_string_value("qBt-label").c_str());
    int priority = fast.dict_find_int_value("qBt-queuePosition");
    bool seedStatus = fast.dict_find_int_value("qBt-seedStatus");

    TorPersistent->saveSavePath(hash, savePath);
    TorPersistent->setRatioLimit(hash, ratioLimit);
    TorPersistent->setAddedDate(hash, addedDate);
    TorPersistent->saveLabel(hash, label);
    TorPersistent->savePriority(hash, priority);
    TorPersistent->saveSeedStatus(hash, seedStatus);
}

void QBtSession::backupPersistentData(const QString &hash, boost::shared_ptr<libtorrent::entry> data) {
    const TorrentPersistentData* const TorPersistent = TorrentPersistentData::instance();
    (*data)["qBt-savePath"] = fsutils::fromNativePath(TorPersistent->getSavePath(hash)).toUtf8().constData();
    (*data)["qBt-ratioLimit"] = QString::number(TorPersistent->getRatioLimit(hash)).toUtf8().constData();
    (*data)["qBt-label"] = TorPersistent->getLabel(hash).toUtf8().constData();
    (*data)["qBt-queuePosition"] = TorPersistent->getPriority(hash);
    (*data)["qBt-seedStatus"] = (int)TorPersistent->isSeed(hash);
}

void QBtSession::unhideMagnet(const QString &hash) {
    Preferences* const pref = Preferences::instance();
    HiddenData::deleteData(hash);
    QString save_path = getSavePath(hash, false); //appends label if necessary
    QTorrentHandle h(getTorrentHandle(hash));

    if (!h.is_valid()) {
        if (pref->isQueueingSystemEnabled()) {
            //Internally decrease the queue limits to ensure that other queued items aren't started
            libtorrent::session_settings sessionSettings(s->settings());
            int max_downloading = pref->getMaxActiveDownloads();
            int max_active = pref->getMaxActiveTorrents();
            if (max_downloading > -1)
                sessionSettings.active_downloads = max_downloading + HiddenData::getDownloadingSize();
            else
                sessionSettings.active_downloads = max_downloading;
            if (max_active > -1)
                sessionSettings.active_limit = max_active + HiddenData::getDownloadingSize();
            else
                sessionSettings.active_limit = max_active;
            s->set_settings(sessionSettings);
        }
        TorrentTempData::deleteTempData(hash);
        return;
    }

    bool add_paused = pref->addTorrentsInPause();
    if (TorrentTempData::hasTempData(hash)) {
        add_paused = TorrentTempData::isAddPaused(hash);
    }

    if (!h.has_metadata()) {
        if (pref->isQueueingSystemEnabled()) {
            //Internally decrease the queue limits to ensure that other queued items aren't started
            libtorrent::session_settings sessionSettings(s->settings());
            int max_downloading = pref->getMaxActiveDownloads();
            int max_active = pref->getMaxActiveTorrents();
            if (max_downloading > -1)
                sessionSettings.active_downloads = max_downloading + HiddenData::getDownloadingSize();
            else
                sessionSettings.active_downloads = max_downloading;
            if (max_active > -1)
                sessionSettings.active_limit = max_active + HiddenData::getDownloadingSize();
            else
                sessionSettings.active_limit = max_active;
            s->set_settings(sessionSettings);
        }
        if (add_paused)
            h.pause();
    }

    h.queue_position_bottom();
    loadTorrentTempData(h, h.save_path(), !h.has_metadata()); //TempData are deleted by a call to TorrentPersistentData::instance()->saveTorrentPersistentData()
    if (!add_paused)
        h.resume();
    h.move_storage(save_path);

    emit addedTorrent(h);
}


void QBtSession::addTrackersAndUrlSeeds(const QString &hash, const QStringList &trackers, const QStringList& urlSeeds)
{
    QTorrentHandle h = getTorrentHandle(hash);
    if (h.is_valid())
        mergeTorrents_impl(h, trackers, urlSeeds);
}

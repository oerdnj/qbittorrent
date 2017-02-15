
/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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

#include "session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegExp>
#include <QString>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <queue>
#include <vector>

#include <libtorrent/alert_types.hpp>
#if LIBTORRENT_VERSION_NUM >= 10100
#include <libtorrent/bdecode.hpp>
#endif
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/lt_trackers.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/identify_client.hpp>
#include <libtorrent/ip_filter.hpp>
#if LIBTORRENT_VERSION_NUM < 10100
#include <libtorrent/lazy_entry.hpp>
#endif
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>

#include "base/logger.h"
#include "base/net/downloadhandler.h"
#include "base/net/downloadmanager.h"
#include "base/net/portforwarder.h"
#include "base/net/proxyconfigurationmanager.h"
#include "base/torrentfileguard.h"
#include "base/torrentfilter.h"
#include "base/unicodestrings.h"
#include "base/utils/misc.h"
#include "base/utils/fs.h"
#include "base/utils/string.h"
#include "cachestatus.h"
#include "magneturi.h"
#include "private/filterparserthread.h"
#include "private/statistics.h"
#include "private/bandwidthscheduler.h"
#include "private/resumedatasavingmanager.h"
#include "sessionstatus.h"
#include "torrenthandle.h"
#include "tracker.h"
#include "trackerentry.h"

static const char PEER_ID[] = "qB";
static const char RESUME_FOLDER[] = "BT_backup";
static const char USER_AGENT[] = "qBittorrent " VERSION;

namespace libt = libtorrent;
using namespace BitTorrent;

namespace
{
    bool readFile(const QString &path, QByteArray &buf);
    bool loadTorrentResumeData(const QByteArray &data, AddTorrentData &torrentData, int &prio, MagnetUri &magnetUri);

    void torrentQueuePositionUp(const libt::torrent_handle &handle);
    void torrentQueuePositionDown(const libt::torrent_handle &handle);
    void torrentQueuePositionTop(const libt::torrent_handle &handle);
    void torrentQueuePositionBottom(const libt::torrent_handle &handle);

    inline SettingsStorage *settings() { return  SettingsStorage::instance(); }

    QStringMap map_cast(const QVariantMap &map)
    {
        QStringMap result;
        foreach (const QString &key, map.keys())
            result[key] = map.value(key).toString();
        return result;
    }

    QVariantMap map_cast(const QStringMap &map)
    {
        QVariantMap result;
        foreach (const QString &key, map.keys())
            result[key] = map.value(key);
        return result;
    }

    QString normalizePath(const QString &path)
    {
        QString tmp = Utils::Fs::fromNativePath(path.trimmed());
        if (!tmp.isEmpty() && !tmp.endsWith('/'))
            return tmp + '/';
        return tmp;
    }

    QString normalizeSavePath(QString path, const QString &defaultPath = Utils::Fs::QDesktopServicesDownloadLocation())
    {
        path = path.trimmed();
        if (path.isEmpty())
            path = Utils::Fs::fromNativePath(defaultPath.trimmed());

        return normalizePath(path);
    }

    QStringMap expandCategories(const QStringMap &categories)
    {
        QStringMap expanded = categories;

        foreach (const QString &category, categories.keys()) {
            foreach (const QString &subcat, Session::expandCategory(category)) {
                if (!expanded.contains(subcat))
                    expanded[subcat] = "";
            }
        }

        return expanded;
    }

    QStringList findAllFiles(const QString &dirPath)
    {
        QStringList files;
        QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();

        return files;
    }

    template <typename T>
    struct LowerLimited
    {
        LowerLimited(T limit, T ret)
            : m_limit(limit)
            , m_ret(ret)
        {
        }

        explicit LowerLimited(T limit)
            : LowerLimited(limit, limit)
        {
        }

        T operator()(T val)
        {
            return val <= m_limit ? m_ret : val;
        }

    private:
        const T m_limit;
        const T m_ret;
    };

    template <typename T>
    LowerLimited<T> lowerLimited(T limit) { return LowerLimited<T>(limit); }

    template <typename T>
    LowerLimited<T> lowerLimited(T limit, T ret) { return LowerLimited<T>(limit, ret); }
}

// Session

Session *Session::m_instance = nullptr;

#define BITTORRENT_KEY(name) "BitTorrent/" name
#define BITTORRENT_SESSION_KEY(name) BITTORRENT_KEY("Session/") name

Session::Session(QObject *parent)
    : QObject(parent)
    , m_deferredConfigureScheduled(false)
    , m_IPFilteringChanged(false)
#if LIBTORRENT_VERSION_NUM >= 10100
    , m_listenInterfaceChanged(true)
#endif
    , m_isDHTEnabled(BITTORRENT_SESSION_KEY("DHTEnabled"), true)
    , m_isLSDEnabled(BITTORRENT_SESSION_KEY("LSDEnabled"), true)
    , m_isPeXEnabled(BITTORRENT_SESSION_KEY("PeXEnabled"), true)
    , m_isTrackerExchangeEnabled(BITTORRENT_SESSION_KEY("TrackerExchangeEnabled"), false)
    , m_isIPFilteringEnabled(BITTORRENT_SESSION_KEY("IPFilteringEnabled"), false)
    , m_isTrackerFilteringEnabled(BITTORRENT_SESSION_KEY("TrackerFilteringEnabled"), false)
    , m_IPFilterFile(BITTORRENT_SESSION_KEY("IPFilter"))
    , m_announceToAllTrackers(BITTORRENT_SESSION_KEY("AnnounceToAllTrackers"), true)
    , m_diskCacheSize(BITTORRENT_SESSION_KEY("DiskCacheSize"), 0)
    , m_diskCacheTTL(BITTORRENT_SESSION_KEY("DiskCacheTTL"), 60)
    , m_useOSCache(BITTORRENT_SESSION_KEY("UseOSCache"), true)
    , m_isAnonymousModeEnabled(BITTORRENT_SESSION_KEY("AnonymousModeEnabled"), false)
    , m_isQueueingEnabled(BITTORRENT_SESSION_KEY("QueueingSystemEnabled"), true)
    , m_maxActiveDownloads(BITTORRENT_SESSION_KEY("MaxActiveDownloads"), 3, lowerLimited(-1))
    , m_maxActiveUploads(BITTORRENT_SESSION_KEY("MaxActiveUploads"), 3, lowerLimited(-1))
    , m_maxActiveTorrents(BITTORRENT_SESSION_KEY("MaxActiveTorrents"), 5, lowerLimited(-1))
    , m_ignoreSlowTorrentsForQueueing(BITTORRENT_SESSION_KEY("IgnoreSlowTorrentsForQueueing"), false)
    , m_outgoingPortsMin(BITTORRENT_SESSION_KEY("OutgoingPortsMin"), 0)
    , m_outgoingPortsMax(BITTORRENT_SESSION_KEY("OutgoingPortsMax"), 0)
    , m_ignoreLimitsOnLAN(BITTORRENT_SESSION_KEY("IgnoreLimitsOnLAN"), true)
    , m_includeOverheadInLimits(BITTORRENT_SESSION_KEY("IncludeOverheadInLimits"), false)
    , m_announceIP(BITTORRENT_SESSION_KEY("AnnounceIP"))
    , m_isSuperSeedingEnabled(BITTORRENT_SESSION_KEY("SuperSeedingEnabled"), false)
    , m_maxConnections(BITTORRENT_SESSION_KEY("MaxConnections"), 500, lowerLimited(0, -1))
    , m_maxHalfOpenConnections(BITTORRENT_SESSION_KEY("MaxHalfOpenConnections"), 20, lowerLimited(0, -1))
    , m_maxUploads(BITTORRENT_SESSION_KEY("MaxUploads"), -1, lowerLimited(0, -1))
    , m_maxConnectionsPerTorrent(BITTORRENT_SESSION_KEY("MaxConnectionsPerTorrent"), 100, lowerLimited(0, -1))
    , m_maxUploadsPerTorrent(BITTORRENT_SESSION_KEY("MaxUploadsPerTorrent"), -1, lowerLimited(0, -1))
    , m_isUTPEnabled(BITTORRENT_SESSION_KEY("uTPEnabled"), true)
    , m_isUTPRateLimited(BITTORRENT_SESSION_KEY("uTPRateLimited"), true)
    , m_isAddTrackersEnabled(BITTORRENT_SESSION_KEY("AddTrackersEnabled"), false)
    , m_additionalTrackers(BITTORRENT_SESSION_KEY("AdditionalTrackers"))
    , m_globalMaxRatio(BITTORRENT_SESSION_KEY("GlobalMaxRatio"), -1, [](qreal r) { return r < 0 ? -1. : r;})
    , m_isAddTorrentPaused(BITTORRENT_SESSION_KEY("AddTorrentPaused"), false)
    , m_isAppendExtensionEnabled(BITTORRENT_SESSION_KEY("AddExtensionToIncompleteFiles"), false)
    , m_refreshInterval(BITTORRENT_SESSION_KEY("RefreshInterval"), 1500)
    , m_isPreallocationEnabled(BITTORRENT_SESSION_KEY("Preallocation"), false)
    , m_torrentExportDirectory(BITTORRENT_SESSION_KEY("TorrentExportDirectory"))
    , m_finishedTorrentExportDirectory(BITTORRENT_SESSION_KEY("FinishedTorrentExportDirectory"))
    , m_globalDownloadSpeedLimit(BITTORRENT_SESSION_KEY("GlobalDLSpeedLimit"), 0, lowerLimited(0))
    , m_globalUploadSpeedLimit(BITTORRENT_SESSION_KEY("GlobalUPSpeedLimit"), 0, lowerLimited(0))
    , m_altGlobalDownloadSpeedLimit(BITTORRENT_SESSION_KEY("AlternativeGlobalDLSpeedLimit"), 10, lowerLimited(0))
    , m_altGlobalUploadSpeedLimit(BITTORRENT_SESSION_KEY("AlternativeGlobalUPSpeedLimit"), 10, lowerLimited(0))
    , m_isAltGlobalSpeedLimitEnabled(BITTORRENT_SESSION_KEY("UseAlternativeGlobalSpeedLimit"), false)
    , m_isBandwidthSchedulerEnabled(BITTORRENT_SESSION_KEY("BandwidthSchedulerEnabled"), false)
    , m_saveResumeDataInterval(BITTORRENT_SESSION_KEY("SaveResumeDataInterval"), 3)
    , m_port(BITTORRENT_SESSION_KEY("Port"), 8999)
    , m_useRandomPort(BITTORRENT_SESSION_KEY("UseRandomPort"), false)
    , m_networkInterface(BITTORRENT_SESSION_KEY("Interface"))
    , m_networkInterfaceName(BITTORRENT_SESSION_KEY("InterfaceName"))
    , m_networkInterfaceAddress(BITTORRENT_SESSION_KEY("InterfaceAddress"))
    , m_isIPv6Enabled(BITTORRENT_SESSION_KEY("IPv6Enabled"), false)
    , m_encryption(BITTORRENT_SESSION_KEY("Encryption"), 0)
    , m_isForceProxyEnabled(BITTORRENT_SESSION_KEY("ForceProxy"), true)
    , m_isProxyPeerConnectionsEnabled(BITTORRENT_SESSION_KEY("ProxyPeerConnections"), false)
    , m_storedCategories(BITTORRENT_SESSION_KEY("Categories"))
    , m_maxRatioAction(BITTORRENT_SESSION_KEY("MaxRatioAction"), Pause)
    , m_defaultSavePath(BITTORRENT_SESSION_KEY("DefaultSavePath"), Utils::Fs::QDesktopServicesDownloadLocation(), normalizePath)
    , m_tempPath(BITTORRENT_SESSION_KEY("TempPath"), defaultSavePath() + "temp/", normalizePath)
    , m_isSubcategoriesEnabled(BITTORRENT_SESSION_KEY("SubcategoriesEnabled"), false)
    , m_isTempPathEnabled(BITTORRENT_SESSION_KEY("TempPathEnabled"), false)
    , m_isAutoTMMDisabledByDefault(BITTORRENT_SESSION_KEY("DisableAutoTMMByDefault"), true)
    , m_isDisableAutoTMMWhenCategoryChanged(BITTORRENT_SESSION_KEY("DisableAutoTMMTriggers/CategoryChanged"), false)
    , m_isDisableAutoTMMWhenDefaultSavePathChanged(BITTORRENT_SESSION_KEY("DisableAutoTMMTriggers/DefaultSavePathChanged"), true)
    , m_isDisableAutoTMMWhenCategorySavePathChanged(BITTORRENT_SESSION_KEY("DisableAutoTMMTriggers/CategorySavePathChanged"), true)
    , m_isTrackerEnabled(BITTORRENT_KEY("TrackerEnabled"), false)
    , m_bannedIPs("State/BannedIPs")
    , m_wasPexEnabled(m_isPeXEnabled)
    , m_wasTrackerExchangeEnabled(m_isTrackerExchangeEnabled)
    , m_numResumeData(0)
    , m_extraLimit(0)
    , m_useProxy(false)
{
    Logger* const logger = Logger::instance();

    initResumeFolder();

    m_bigRatioTimer = new QTimer(this);
    m_bigRatioTimer->setInterval(10000);
    connect(m_bigRatioTimer, SIGNAL(timeout()), SLOT(processBigRatios()));

    // Set severity level of libtorrent session
    int alertMask = libt::alert::error_notification
                    | libt::alert::peer_notification
                    | libt::alert::port_mapping_notification
                    | libt::alert::storage_notification
                    | libt::alert::tracker_notification
                    | libt::alert::status_notification
                    | libt::alert::ip_block_notification
                    | libt::alert::progress_notification
                    | libt::alert::stats_notification
                    ;

#if LIBTORRENT_VERSION_NUM < 10100
    libt::fingerprint fingerprint(PEER_ID, VERSION_MAJOR, VERSION_MINOR, VERSION_BUGFIX, VERSION_BUILD);
    std::string peerId = fingerprint.to_string();
    const ushort port = this->port();
    std::pair<int, int> ports(port, port);
    const QString ip = getListeningIPs().first();
    m_nativeSession = new libt::session(fingerprint, ports, ip.isEmpty() ? 0 : ip.toLatin1().constData(), 0, alertMask);

    libt::session_settings sessionSettings = m_nativeSession->settings();
    sessionSettings.user_agent = USER_AGENT;
    sessionSettings.upnp_ignore_nonrouters = true;
    sessionSettings.use_dht_as_fallback = false;
    // Disable support for SSL torrents for now
    sessionSettings.ssl_listen = 0;
    // To prevent ISPs from blocking seeding
    sessionSettings.lazy_bitfields = true;
    // Speed up exit
    sessionSettings.stop_tracker_timeout = 1;
    sessionSettings.auto_scrape_interval = 1200; // 20 minutes
    sessionSettings.auto_scrape_min_interval = 900; // 15 minutes
    sessionSettings.connection_speed = 20; // default is 10
    sessionSettings.no_connect_privileged_ports = false;
    sessionSettings.seed_choking_algorithm = libt::session_settings::fastest_upload;
    configure(sessionSettings);
    m_nativeSession->set_settings(sessionSettings);
    configureListeningInterface();
    m_nativeSession->set_alert_dispatch([this](std::auto_ptr<libt::alert> alertPtr)
    {
        dispatchAlerts(alertPtr.release());
    });
#else
    std::string peerId = libt::generate_fingerprint(PEER_ID, VERSION_MAJOR, VERSION_MINOR, VERSION_BUGFIX, VERSION_BUILD);
    libt::settings_pack pack;
    pack.set_int(libt::settings_pack::alert_mask, alertMask);
    pack.set_str(libt::settings_pack::peer_fingerprint, peerId);
    pack.set_bool(libt::settings_pack::listen_system_port_fallback, false);
    pack.set_str(libt::settings_pack::user_agent, USER_AGENT);
    pack.set_bool(libt::settings_pack::upnp_ignore_nonrouters, true);
    pack.set_bool(libt::settings_pack::use_dht_as_fallback, false);
    // Disable support for SSL torrents for now
    pack.set_int(libt::settings_pack::ssl_listen, 0);
    // To prevent ISPs from blocking seeding
    pack.set_bool(libt::settings_pack::lazy_bitfields, true);
    // Speed up exit
    pack.set_int(libt::settings_pack::stop_tracker_timeout, 1);
    pack.set_int(libt::settings_pack::auto_scrape_interval, 1200); // 20 minutes
    pack.set_int(libt::settings_pack::auto_scrape_min_interval, 900); // 15 minutes
    pack.set_int(libt::settings_pack::connection_speed, 20); // default is 10
    pack.set_bool(libt::settings_pack::no_connect_privileged_ports, false);
    pack.set_int(libt::settings_pack::seed_choking_algorithm, libt::settings_pack::fastest_upload);
    configure(pack);

    m_nativeSession = new libt::session(pack, 0);
    m_nativeSession->set_alert_notify([this]()
    {
        QMetaObject::invokeMethod(this, "readAlerts", Qt::QueuedConnection);
    });
#endif

    // Enabling plugins
    //m_nativeSession->add_extension(&libt::create_metadata_plugin);
    m_nativeSession->add_extension(&libt::create_ut_metadata_plugin);
    if (isTrackerExchangeEnabled())
        m_nativeSession->add_extension(&libt::create_lt_trackers_plugin);
    if (isPeXEnabled())
        m_nativeSession->add_extension(&libt::create_ut_pex_plugin);
    m_nativeSession->add_extension(&libt::create_smart_ban_plugin);

    logger->addMessage(tr("Peer ID: ") + Utils::String::fromStdString(peerId));
    logger->addMessage(tr("HTTP User-Agent is '%1'").arg(USER_AGENT));
    logger->addMessage(tr("DHT support [%1]").arg(isDHTEnabled() ? tr("ON") : tr("OFF")), Log::INFO);
    logger->addMessage(tr("Local Peer Discovery support [%1]").arg(isLSDEnabled() ? tr("ON") : tr("OFF")), Log::INFO);
    logger->addMessage(tr("PeX support [%1]").arg(isPeXEnabled() ? tr("ON") : tr("OFF")), Log::INFO);
    logger->addMessage(tr("Anonymous mode [%1]").arg(isAnonymousModeEnabled() ? tr("ON") : tr("OFF")), Log::INFO);
    logger->addMessage(tr("Encryption support [%1]")
                       .arg(encryption() == 0 ? tr("ON") : encryption() == 1 ? tr("FORCED") : tr("OFF"))
                       , Log::INFO);

    if (isIPFilteringEnabled())
        enableIPFilter();
    // Add the banned IPs
    processBannedIPs();

    m_categories = map_cast(m_storedCategories);
    if (isSubcategoriesEnabled()) {
        // if subcategories support changed manually
        m_categories = expandCategories(m_categories);
        m_storedCategories = map_cast(m_categories);
    }

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(refreshInterval());
    connect(m_refreshTimer, SIGNAL(timeout()), SLOT(refresh()));
    m_refreshTimer->start();

    // Regular saving of fastresume data
    m_resumeDataTimer = new QTimer(this);
    m_resumeDataTimer->setInterval(saveResumeDataInterval() * 60 * 1000);
    connect(m_resumeDataTimer, SIGNAL(timeout()), SLOT(generateResumeData()));

    m_statistics = new Statistics(this);

    updateRatioTimer();
    populateAdditionalTrackers();

    enableTracker(isTrackerEnabled());

    connect(Net::ProxyConfigurationManager::instance(), SIGNAL(proxyConfigurationChanged()), SLOT(configureDeferred()));

    // Network configuration monitor
    connect(&m_networkManager, SIGNAL(onlineStateChanged(bool)), SLOT(networkOnlineStateChanged(bool)));
    connect(&m_networkManager, SIGNAL(configurationAdded(const QNetworkConfiguration&)), SLOT(networkConfigurationChange(const QNetworkConfiguration&)));
    connect(&m_networkManager, SIGNAL(configurationRemoved(const QNetworkConfiguration&)), SLOT(networkConfigurationChange(const QNetworkConfiguration&)));
    connect(&m_networkManager, SIGNAL(configurationChanged(const QNetworkConfiguration&)), SLOT(networkConfigurationChange(const QNetworkConfiguration&)));

    m_ioThread = new QThread(this);
    m_resumeDataSavingManager = new ResumeDataSavingManager(m_resumeFolderPath);
    m_resumeDataSavingManager->moveToThread(m_ioThread);
    connect(m_ioThread, SIGNAL(finished()), m_resumeDataSavingManager, SLOT(deleteLater()));
    m_ioThread->start();
    m_resumeDataTimer->start();

    // initialize PortForwarder instance
    Net::PortForwarder::initInstance(m_nativeSession);

    qDebug("* BitTorrent Session constructed");
    startUpTorrents();
}

bool Session::isDHTEnabled() const
{
    return m_isDHTEnabled;
}

void Session::setDHTEnabled(bool enabled)
{
    if (enabled != m_isDHTEnabled) {
        m_isDHTEnabled = enabled;
        configureDeferred();
        Logger::instance()->addMessage(
                    tr("DHT support [%1]").arg(enabled ? tr("ON") : tr("OFF")), Log::INFO);
    }
}

bool Session::isLSDEnabled() const
{
    return m_isLSDEnabled;
}

void Session::setLSDEnabled(bool enabled)
{
    if (enabled != m_isLSDEnabled) {
        m_isLSDEnabled = enabled;
        configureDeferred();
        Logger::instance()->addMessage(
                    tr("Local Peer Discovery support [%1]").arg(enabled ? tr("ON") : tr("OFF"))
                    , Log::INFO);
    }
}

bool Session::isPeXEnabled() const
{
    return m_isPeXEnabled;
}

void Session::setPeXEnabled(bool enabled)
{
    m_isPeXEnabled = enabled;
    if (m_wasPexEnabled != enabled)
        Logger::instance()->addMessage(tr("Restart is required to toggle PeX support"), Log::WARNING);
}

bool Session::isTrackerExchangeEnabled() const
{
    return m_isTrackerExchangeEnabled;
}

void Session::setTrackerExchangeEnabled(bool enabled)
{
    m_isTrackerExchangeEnabled = enabled;
    if (m_wasTrackerExchangeEnabled != enabled)
        Logger::instance()->addMessage(tr("Restart is required to toggle Tracker Exchange support"), Log::WARNING);
}

bool Session::isTempPathEnabled() const
{
    return m_isTempPathEnabled;
}

void Session::setTempPathEnabled(bool enabled)
{
    if (enabled != isTempPathEnabled()) {
        m_isTempPathEnabled = enabled;
        foreach (TorrentHandle *const torrent, m_torrents)
            torrent->handleTempPathChanged();
    }
}

bool Session::isAppendExtensionEnabled() const
{
    return m_isAppendExtensionEnabled;
}

void Session::setAppendExtensionEnabled(bool enabled)
{
    if (isAppendExtensionEnabled() != enabled) {
        // append or remove .!qB extension for incomplete files
        foreach (TorrentHandle *const torrent, m_torrents)
            torrent->handleAppendExtensionToggled();

        m_isAppendExtensionEnabled = enabled;
    }
}

uint Session::refreshInterval() const
{
    return m_refreshInterval;
}

void Session::setRefreshInterval(uint value)
{
    if (value != refreshInterval()) {
        m_refreshTimer->setInterval(value);
        m_refreshInterval = value;
    }
}

bool Session::isPreallocationEnabled() const
{
    return m_isPreallocationEnabled;
}

void Session::setPreallocationEnabled(bool enabled)
{
    m_isPreallocationEnabled = enabled;
}

QString Session::torrentExportDirectory() const
{
    return Utils::Fs::fromNativePath(m_torrentExportDirectory);
}

void Session::setTorrentExportDirectory(QString path)
{
    path = Utils::Fs::fromNativePath(path);
    if (path != torrentExportDirectory())
        m_torrentExportDirectory = path;
}

QString Session::finishedTorrentExportDirectory() const
{
    return Utils::Fs::fromNativePath(m_finishedTorrentExportDirectory);
}

void Session::setFinishedTorrentExportDirectory(QString path)
{
    path = Utils::Fs::fromNativePath(path);
    if (path != finishedTorrentExportDirectory())
        m_finishedTorrentExportDirectory = path;
}

QString Session::defaultSavePath() const
{
    return Utils::Fs::fromNativePath(m_defaultSavePath);
}

QString Session::tempPath() const
{
    return Utils::Fs::fromNativePath(m_tempPath);
}

QString Session::torrentTempPath(const InfoHash &hash) const
{
    return tempPath()
            + static_cast<QString>(hash).left(7)
            + "/";
}

bool Session::isValidCategoryName(const QString &name)
{
    QRegExp re(R"(^([^\\\/]|[^\\\/]([^\\\/]|\/(?=[^\/]))*[^\\\/])$)");
    if (!name.isEmpty() && (re.indexIn(name) != 0)) {
        qDebug() << "Incorrect category name:" << name;
        return false;
    }

    return true;
}

QStringList Session::expandCategory(const QString &category)
{
    QStringList result;
    if (!isValidCategoryName(category))
        return result;

    int index = 0;
    while ((index = category.indexOf('/', index)) >= 0) {
        result << category.left(index);
        ++index;
    }
    result << category;

    return result;
}

QStringList Session::categories() const
{
    return m_categories.keys();
}

QString Session::categorySavePath(const QString &categoryName) const
{
    QString basePath = m_defaultSavePath;
    if (categoryName.isEmpty()) return basePath;

    QString path = m_categories.value(categoryName);
    if (path.isEmpty()) // use implicit save path
        path = Utils::Fs::toValidFileSystemName(categoryName, true);

    if (!QDir::isAbsolutePath(path))
        path.prepend(basePath);

    return normalizeSavePath(path);
}

bool Session::addCategory(const QString &name, const QString &savePath)
{
    if (name.isEmpty()) return false;
    if (!isValidCategoryName(name) || m_categories.contains(name))
        return false;

    if (isSubcategoriesEnabled()) {
        foreach (const QString &parent, expandCategory(name)) {
            if ((parent != name) && !m_categories.contains(parent)) {
                m_categories[parent] = "";
                emit categoryAdded(parent);
            }
        }
    }

    m_categories[name] = savePath;
    m_storedCategories = map_cast(m_categories);
    emit categoryAdded(name);

    return true;
}

bool Session::editCategory(const QString &name, const QString &savePath)
{
    if (!m_categories.contains(name)) return false;
    if (categorySavePath(name) == savePath) return false;

    m_categories[name] = savePath;
    if (isDisableAutoTMMWhenCategorySavePathChanged()) {
        foreach (TorrentHandle *const torrent, torrents())
            if (torrent->category() == name)
                torrent->setAutoTMMEnabled(false);
    }
    else {
        foreach (TorrentHandle *const torrent, torrents())
            if (torrent->category() == name)
                torrent->handleCategorySavePathChanged();
    }

    return true;
}

bool Session::removeCategory(const QString &name)
{
    foreach (TorrentHandle *const torrent, torrents())
        if (torrent->belongsToCategory(name))
            torrent->setCategory("");

    // remove stored category and its subcategories if exist
    bool result = false;
    if (isSubcategoriesEnabled()) {
        // remove subcategories
        QString test = name + "/";
        foreach (const QString &category, m_categories.keys()) {
            if (category.startsWith(test)) {
                m_categories.remove(category);
                result = true;
                emit categoryRemoved(category);
            }
        }
    }

    result = (m_categories.remove(name) > 0) || result;

    if (result) {
        // update stored categories
        m_storedCategories = map_cast(m_categories);
        emit categoryRemoved(name);
    }

    return result;
}

bool Session::isSubcategoriesEnabled() const
{
    return m_isSubcategoriesEnabled;
}

void Session::setSubcategoriesEnabled(bool value)
{
    if (isSubcategoriesEnabled() == value) return;

    if (value) {
        // expand categories to include all parent categories
        m_categories = expandCategories(m_categories);
        // update stored categories
        m_storedCategories = map_cast(m_categories);
    }
    else {
        // reload categories
        m_categories = map_cast(m_storedCategories);
    }

    m_isSubcategoriesEnabled = value;
    emit subcategoriesSupportChanged();
}

bool Session::isAutoTMMDisabledByDefault() const
{
    return m_isAutoTMMDisabledByDefault;
}

void Session::setAutoTMMDisabledByDefault(bool value)
{
    m_isAutoTMMDisabledByDefault = value;
}

bool Session::isDisableAutoTMMWhenCategoryChanged() const
{
    return m_isDisableAutoTMMWhenCategoryChanged;
}

void Session::setDisableAutoTMMWhenCategoryChanged(bool value)
{
    m_isDisableAutoTMMWhenCategoryChanged = value;
}

bool Session::isDisableAutoTMMWhenDefaultSavePathChanged() const
{
    return m_isDisableAutoTMMWhenDefaultSavePathChanged;
}

void Session::setDisableAutoTMMWhenDefaultSavePathChanged(bool value)
{
    m_isDisableAutoTMMWhenDefaultSavePathChanged = value;
}

bool Session::isDisableAutoTMMWhenCategorySavePathChanged() const
{
    return m_isDisableAutoTMMWhenCategorySavePathChanged;
}

void Session::setDisableAutoTMMWhenCategorySavePathChanged(bool value)
{
    m_isDisableAutoTMMWhenCategorySavePathChanged = value;
}

bool Session::isAddTorrentPaused() const
{
    return m_isAddTorrentPaused;
}

void Session::setAddTorrentPaused(bool value)
{
    m_isAddTorrentPaused = value;
}

bool Session::isTrackerEnabled() const
{
    return m_isTrackerEnabled;
}

void Session::setTrackerEnabled(bool enabled)
{
    if (isTrackerEnabled() != enabled) {
        enableTracker(enabled);
        m_isTrackerEnabled = enabled;
    }
}

qreal Session::globalMaxRatio() const
{
    return m_globalMaxRatio;
}

// Torrents will a ratio superior to the given value will
// be automatically deleted
void Session::setGlobalMaxRatio(qreal ratio)
{
    if (ratio < 0)
        ratio = -1.;

    if (ratio != globalMaxRatio()) {
        m_globalMaxRatio = ratio;
        updateRatioTimer();
    }
}

// Main destructor
Session::~Session()
{
    // Do some BT related saving
    saveResumeData();

    // We must delete FilterParserThread
    // before we delete libtorrent::session
    if (m_filterParser)
        delete m_filterParser;

    // We must delete PortForwarderImpl before
    // we delete libtorrent::session
    Net::PortForwarder::freeInstance();

    qDebug("Deleting the session");
    delete m_nativeSession;

    m_ioThread->quit();
    m_ioThread->wait();

    m_resumeFolderLock.close();
    m_resumeFolderLock.remove();
}

void Session::initInstance()
{
    if (!m_instance) {
        m_instance = new Session;

        // BandwidthScheduler::start() depends on Session being fully constructed
        if (m_instance->isBandwidthSchedulerEnabled())
            m_instance->enableBandwidthScheduler();
    }
}

void Session::freeInstance()
{
    if (m_instance) {
        delete m_instance;
        m_instance = 0;
    }
}

Session *Session::instance()
{
    return m_instance;
}

void Session::adjustLimits()
{
    if (isQueueingSystemEnabled()) {
#if LIBTORRENT_VERSION_NUM < 10100
        libt::session_settings sessionSettings(m_nativeSession->settings());
        adjustLimits(sessionSettings);
        m_nativeSession->set_settings(sessionSettings);
#else
        libt::settings_pack settingsPack = m_nativeSession->get_settings();
        adjustLimits(settingsPack);
        m_nativeSession->apply_settings(settingsPack);
#endif
    }
}

// Set BitTorrent session configuration
void Session::configure()
{
    qDebug("Configuring session");
    if (!m_deferredConfigureScheduled) return; // Obtaining the lock is expensive, let's check early
    QWriteLocker locker(&m_lock);
    if (!m_deferredConfigureScheduled) return; // something might have changed while we were getting the lock
#if LIBTORRENT_VERSION_NUM < 10100
    libt::session_settings sessionSettings = m_nativeSession->settings();
    configure(sessionSettings);
    m_nativeSession->set_settings(sessionSettings);
#else
    libt::settings_pack settingsPack = m_nativeSession->get_settings();
    configure(settingsPack);
    m_nativeSession->apply_settings(settingsPack);
#endif

    if (m_IPFilteringChanged) {
        if (isIPFilteringEnabled())
            enableIPFilter();
        else
            disableIPFilter();
        m_IPFilteringChanged = false;
    }

    m_deferredConfigureScheduled = false;
    qDebug("Session configured");
}

void Session::processBannedIPs()
{
    // First, import current filter
    libt::ip_filter filter = m_nativeSession->get_ip_filter();
    foreach (const QString &ip, m_bannedIPs.value()) {
        boost::system::error_code ec;
        libt::address addr = libt::address::from_string(ip.toLatin1().constData(), ec);
        Q_ASSERT(!ec);
        if (!ec)
            filter.add_rule(addr, addr, libt::ip_filter::blocked);
    }

    m_nativeSession->set_ip_filter(filter);
}

#if LIBTORRENT_VERSION_NUM >= 10100
void Session::adjustLimits(libt::settings_pack &settingsPack)
{
    //Internally increase the queue limits to ensure that the magnet is started
    int maxDownloads = maxActiveDownloads();
    int maxActive = maxActiveTorrents();

    settingsPack.set_int(libt::settings_pack::active_downloads
                         , maxDownloads > -1 ? maxDownloads + m_extraLimit : maxDownloads);
    settingsPack.set_int(libt::settings_pack::active_limit
                         , maxActive > -1 ? maxActive + m_extraLimit : maxActive);
}

void Session::configure(libtorrent::settings_pack &settingsPack)
{
    Logger* const logger = Logger::instance();

    if (m_listenInterfaceChanged) {
        const ushort port = this->port();
        std::pair<int, int> ports(port, port);
        settingsPack.set_int(libt::settings_pack::max_retry_port_bind, ports.second - ports.first);
        foreach (QString ip, getListeningIPs()) {
            libt::error_code ec;
            std::string interfacesStr;

            if (ip.isEmpty()) {
                ip = QLatin1String("0.0.0.0");
                interfacesStr = std::string((QString("%1:%2").arg(ip).arg(port)).toLatin1().constData());
                logger->addMessage(tr("qBittorrent is trying to listen on any interface port: %1"
                                      , "e.g: qBittorrent is trying to listen on any interface port: TCP/6881")
                                   .arg(QString::number(port))
                                   , Log::INFO);

                settingsPack.set_str(libt::settings_pack::listen_interfaces, interfacesStr);
                break;
            }

            libt::address addr = libt::address::from_string(ip.toLatin1().constData(), ec);
            if (!ec) {
                interfacesStr = std::string((addr.is_v6() ? QString("[%1]:%2") : QString("%1:%2"))
                                            .arg(ip).arg(port).toLatin1().constData());
                logger->addMessage(tr("qBittorrent is trying to listen on interface %1 port: %2"
                                      , "e.g: qBittorrent is trying to listen on interface 192.168.0.1 port: TCP/6881")
                                   .arg(ip).arg(port)
                                   , Log::INFO);
                settingsPack.set_str(libt::settings_pack::listen_interfaces, interfacesStr);
                break;
            }
        }

        m_listenInterfaceChanged = false;
    }

    const bool altSpeedLimitEnabled = isAltGlobalSpeedLimitEnabled();
    settingsPack.set_int(libt::settings_pack::download_rate_limit, altSpeedLimitEnabled ? altGlobalDownloadSpeedLimit() : globalDownloadSpeedLimit());
    settingsPack.set_int(libt::settings_pack::upload_rate_limit, altSpeedLimitEnabled ? altGlobalUploadSpeedLimit() : globalUploadSpeedLimit());

    // The most secure, rc4 only so that all streams are encrypted
    settingsPack.set_int(libt::settings_pack::allowed_enc_level, libt::settings_pack::pe_rc4);
    settingsPack.set_bool(libt::settings_pack::prefer_rc4, true);
    switch (encryption()) {
    case 0: //Enabled
        settingsPack.set_int(libt::settings_pack::out_enc_policy, libt::settings_pack::pe_enabled);
        settingsPack.set_int(libt::settings_pack::in_enc_policy, libt::settings_pack::pe_enabled);
        break;
    case 1: // Forced
        settingsPack.set_int(libt::settings_pack::out_enc_policy, libt::settings_pack::pe_forced);
        settingsPack.set_int(libt::settings_pack::in_enc_policy, libt::settings_pack::pe_forced);
        break;
    default: // Disabled
        settingsPack.set_int(libt::settings_pack::out_enc_policy, libt::settings_pack::pe_disabled);
        settingsPack.set_int(libt::settings_pack::in_enc_policy, libt::settings_pack::pe_disabled);
    }

    auto proxyManager = Net::ProxyConfigurationManager::instance();
    Net::ProxyConfiguration proxyConfig = proxyManager->proxyConfiguration();
    if (m_useProxy || (proxyConfig.type != Net::ProxyType::None)) {
        if (proxyConfig.type != Net::ProxyType::None) {
            settingsPack.set_str(libt::settings_pack::proxy_hostname, Utils::String::toStdString(proxyConfig.ip));
            settingsPack.set_int(libt::settings_pack::proxy_port, proxyConfig.port);
            if (proxyManager->isAuthenticationRequired()) {
                settingsPack.set_str(libt::settings_pack::proxy_username, Utils::String::toStdString(proxyConfig.username));
                settingsPack.set_str(libt::settings_pack::proxy_password, Utils::String::toStdString(proxyConfig.password));
            }
            settingsPack.set_bool(libt::settings_pack::proxy_peer_connections, isProxyPeerConnectionsEnabled());
        }

        switch (proxyConfig.type) {
        case Net::ProxyType::HTTP:
            settingsPack.set_int(libt::settings_pack::proxy_type, libt::settings_pack::http);
            break;
        case Net::ProxyType::HTTP_PW:
            settingsPack.set_int(libt::settings_pack::proxy_type, libt::settings_pack::http_pw);
            break;
        case Net::ProxyType::SOCKS4:
            settingsPack.set_int(libt::settings_pack::proxy_type, libt::settings_pack::socks4);
            break;
        case Net::ProxyType::SOCKS5:
            settingsPack.set_int(libt::settings_pack::proxy_type, libt::settings_pack::socks5);
            break;
        case Net::ProxyType::SOCKS5_PW:
            settingsPack.set_int(libt::settings_pack::proxy_type, libt::settings_pack::socks5_pw);
            break;
        default:
            settingsPack.set_int(libt::settings_pack::proxy_type, libt::settings_pack::none);
        }

        m_useProxy = (proxyConfig.type != Net::ProxyType::None);
    }
    settingsPack.set_bool(libt::settings_pack::force_proxy, m_useProxy ? isForceProxyEnabled() : false);

    const bool announceToAll = announceToAllTrackers();
    settingsPack.set_bool(libt::settings_pack::announce_to_all_trackers, announceToAll);
    settingsPack.set_bool(libt::settings_pack::announce_to_all_tiers, announceToAll);

    const int cacheSize = diskCacheSize();
    settingsPack.set_int(libt::settings_pack::cache_size, (cacheSize > 0) ? cacheSize * 64 : -1);
    settingsPack.set_int(libt::settings_pack::cache_expiry, diskCacheTTL());
    qDebug() << "Using a disk cache size of" << cacheSize << "MiB";

    libt::settings_pack::io_buffer_mode_t mode = useOSCache() ? libt::settings_pack::enable_os_cache
                                                              : libt::settings_pack::disable_os_cache;
    settingsPack.set_int(libt::settings_pack::disk_io_read_mode, mode);
    settingsPack.set_int(libt::settings_pack::disk_io_write_mode, mode);

    settingsPack.set_bool(libt::settings_pack::anonymous_mode, isAnonymousModeEnabled());

    // Queueing System
    if (isQueueingSystemEnabled()) {
        adjustLimits(settingsPack);

        settingsPack.set_int(libt::settings_pack::active_seeds, maxActiveUploads());
        settingsPack.set_bool(libt::settings_pack::dont_count_slow_torrents, ignoreSlowTorrentsForQueueing());
    }
    else {
        settingsPack.set_int(libt::settings_pack::active_downloads, -1);
        settingsPack.set_int(libt::settings_pack::active_seeds, -1);
        settingsPack.set_int(libt::settings_pack::active_limit, -1);
    }
    settingsPack.set_int(libt::settings_pack::active_tracker_limit, -1);
    settingsPack.set_int(libt::settings_pack::active_dht_limit, -1);
    settingsPack.set_int(libt::settings_pack::active_lsd_limit, -1);

    // Outgoing ports
    settingsPack.set_int(libt::settings_pack::outgoing_port, outgoingPortsMin());
    settingsPack.set_int(libt::settings_pack::num_outgoing_ports, outgoingPortsMax() - outgoingPortsMin() + 1);

    // Ignore limits on LAN
    settingsPack.set_bool(libt::settings_pack::ignore_limits_on_local_network, ignoreLimitsOnLAN());
    // Include overhead in transfer limits
    settingsPack.set_bool(libt::settings_pack::rate_limit_ip_overhead, includeOverheadInLimits());
    // IP address to announce to trackers
    settingsPack.set_str(libt::settings_pack::announce_ip, Utils::String::toStdString(announceIP()));
    // Super seeding
    settingsPack.set_bool(libt::settings_pack::strict_super_seeding, isSuperSeedingEnabled());
    // * Max Half-open connections
    settingsPack.set_int(libt::settings_pack::half_open_limit, maxHalfOpenConnections());
    // * Max connections limit
    settingsPack.set_int(libt::settings_pack::connections_limit, maxConnections());
    // * Global max upload slots
    settingsPack.set_int(libt::settings_pack::unchoke_slots_limit, maxUploads());
    // uTP
    settingsPack.set_bool(libt::settings_pack::enable_incoming_utp, isUTPEnabled());
    settingsPack.set_bool(libt::settings_pack::enable_outgoing_utp, isUTPEnabled());
    // uTP rate limiting
    settingsPack.set_bool(libt::settings_pack::rate_limit_utp, isUTPRateLimited());
    settingsPack.set_int(libt::settings_pack::mixed_mode_algorithm, isUTPRateLimited()
                         ? libt::settings_pack::prefer_tcp
                         : libt::settings_pack::peer_proportional);

    settingsPack.set_bool(libt::settings_pack::apply_ip_filter_to_trackers, isTrackerFilteringEnabled());

    settingsPack.set_bool(libt::settings_pack::enable_dht, isDHTEnabled());
    if (isDHTEnabled())
        settingsPack.set_str(libt::settings_pack::dht_bootstrap_nodes, "dht.libtorrent.org:25401,router.bittorrent.com:6881,router.utorrent.com:6881,dht.transmissionbt.com:6881,dht.aelitis.com:6881");
    settingsPack.set_bool(libt::settings_pack::enable_lsd, isLSDEnabled());
}

#else

void Session::adjustLimits(libt::session_settings &sessionSettings)
{
    //Internally increase the queue limits to ensure that the magnet is started
    int maxDownloads = maxActiveDownloads();
    int maxActive = maxActiveTorrents();

    sessionSettings.active_downloads = maxDownloads > -1 ? maxDownloads + m_extraLimit : maxDownloads;
    sessionSettings.active_limit = maxActive > -1 ? maxActive + m_extraLimit : maxActive;
}

void Session::configure(libtorrent::session_settings &sessionSettings)
{
    const bool altSpeedLimitEnabled = isAltGlobalSpeedLimitEnabled();
    sessionSettings.download_rate_limit = altSpeedLimitEnabled ? altGlobalDownloadSpeedLimit() : globalDownloadSpeedLimit();
    sessionSettings.upload_rate_limit = altSpeedLimitEnabled ? altGlobalUploadSpeedLimit() : globalUploadSpeedLimit();

    // The most secure, rc4 only so that all streams are encrypted
    libt::pe_settings encryptionSettings;
    encryptionSettings.allowed_enc_level = libt::pe_settings::rc4;
    encryptionSettings.prefer_rc4 = true;
    switch (encryption()) {
    case 0: //Enabled
        encryptionSettings.out_enc_policy = libt::pe_settings::enabled;
        encryptionSettings.in_enc_policy = libt::pe_settings::enabled;
        break;
    case 1: // Forced
        encryptionSettings.out_enc_policy = libt::pe_settings::forced;
        encryptionSettings.in_enc_policy = libt::pe_settings::forced;
        break;
    default: // Disabled
        encryptionSettings.out_enc_policy = libt::pe_settings::disabled;
        encryptionSettings.in_enc_policy = libt::pe_settings::disabled;
    }
    m_nativeSession->set_pe_settings(encryptionSettings);

    auto proxyManager = Net::ProxyConfigurationManager::instance();
    Net::ProxyConfiguration proxyConfig = proxyManager->proxyConfiguration();
    if (m_useProxy || (proxyConfig.type != Net::ProxyType::None)) {
        libt::proxy_settings proxySettings;
        if (proxyConfig.type != Net::ProxyType::None) {
            proxySettings.hostname = Utils::String::toStdString(proxyConfig.ip);
            proxySettings.port = proxyConfig.port;
            if (proxyManager->isAuthenticationRequired()) {
                proxySettings.username = Utils::String::toStdString(proxyConfig.username);
                proxySettings.password = Utils::String::toStdString(proxyConfig.password);
            }
            proxySettings.proxy_peer_connections = isProxyPeerConnectionsEnabled();
        }

        switch (proxyConfig.type) {
        case Net::ProxyType::HTTP:
            proxySettings.type = libt::proxy_settings::http;
            break;
        case Net::ProxyType::HTTP_PW:
            proxySettings.type = libt::proxy_settings::http_pw;
            break;
        case Net::ProxyType::SOCKS4:
            proxySettings.type = libt::proxy_settings::socks4;
            break;
        case Net::ProxyType::SOCKS5:
            proxySettings.type = libt::proxy_settings::socks5;
            break;
        case Net::ProxyType::SOCKS5_PW:
            proxySettings.type = libt::proxy_settings::socks5_pw;
            break;
        default:
            proxySettings.type = libt::proxy_settings::none;
        }

        m_nativeSession->set_proxy(proxySettings);
        m_useProxy = (proxyConfig.type != Net::ProxyType::None);
    }
    sessionSettings.force_proxy = m_useProxy ? isForceProxyEnabled() : false;

    bool announceToAll = announceToAllTrackers();
    sessionSettings.announce_to_all_trackers = announceToAll;
    sessionSettings.announce_to_all_tiers = announceToAll;
    int cacheSize = diskCacheSize();
    sessionSettings.cache_size = (cacheSize > 0) ? cacheSize * 64 : -1;
    sessionSettings.cache_expiry = diskCacheTTL();
    qDebug() << "Using a disk cache size of" << cacheSize << "MiB";
    libt::session_settings::io_buffer_mode_t mode = useOSCache() ? libt::session_settings::enable_os_cache
                                                                 : libt::session_settings::disable_os_cache;
    sessionSettings.disk_io_read_mode = mode;
    sessionSettings.disk_io_write_mode = mode;

    sessionSettings.anonymous_mode = isAnonymousModeEnabled();

    // Queueing System
    if (isQueueingSystemEnabled()) {
        adjustLimits(sessionSettings);

        sessionSettings.active_seeds = maxActiveUploads();
        sessionSettings.dont_count_slow_torrents = ignoreSlowTorrentsForQueueing();
    }
    else {
        sessionSettings.active_downloads = -1;
        sessionSettings.active_seeds = -1;
        sessionSettings.active_limit = -1;
    }
    sessionSettings.active_tracker_limit = -1;
    sessionSettings.active_dht_limit = -1;
    sessionSettings.active_lsd_limit = -1;

    // Outgoing ports
    sessionSettings.outgoing_ports = std::make_pair(outgoingPortsMin(), outgoingPortsMax());

    // Ignore limits on LAN
    sessionSettings.ignore_limits_on_local_network = ignoreLimitsOnLAN();
    // Include overhead in transfer limits
    sessionSettings.rate_limit_ip_overhead = includeOverheadInLimits();
    // IP address to announce to trackers
    sessionSettings.announce_ip = Utils::String::toStdString(announceIP());
    // Super seeding
    sessionSettings.strict_super_seeding = isSuperSeedingEnabled();
    // * Max Half-open connections
    sessionSettings.half_open_limit = maxHalfOpenConnections();
    // * Max connections limit
    sessionSettings.connections_limit = maxConnections();
    // * Global max upload slots
    sessionSettings.unchoke_slots_limit = maxUploads();
    // uTP
    sessionSettings.enable_incoming_utp = isUTPEnabled();
    sessionSettings.enable_outgoing_utp = isUTPEnabled();
    // uTP rate limiting
    sessionSettings.rate_limit_utp = isUTPRateLimited();
    sessionSettings.mixed_mode_algorithm = isUTPRateLimited()
                                           ? libt::session_settings::prefer_tcp
                                           : libt::session_settings::peer_proportional;

    sessionSettings.apply_ip_filter_to_trackers = isTrackerFilteringEnabled();

    if (isDHTEnabled()) {
        // Add first the routers and then start DHT.
        m_nativeSession->add_dht_router(std::make_pair(std::string("dht.libtorrent.org"), 25401));
        m_nativeSession->add_dht_router(std::make_pair(std::string("router.bittorrent.com"), 6881));
        m_nativeSession->add_dht_router(std::make_pair(std::string("router.utorrent.com"), 6881));
        m_nativeSession->add_dht_router(std::make_pair(std::string("dht.transmissionbt.com"), 6881));
        m_nativeSession->add_dht_router(std::make_pair(std::string("dht.aelitis.com"), 6881)); // Vuze
        m_nativeSession->start_dht();
    }
    else {
        m_nativeSession->stop_dht();
    }

    if (isLSDEnabled())
        m_nativeSession->start_lsd();
    else
        m_nativeSession->stop_lsd();
}
#endif

void Session::enableTracker(bool enable)
{
    Logger *const logger = Logger::instance();

    if (enable) {
        if (!m_tracker)
            m_tracker = new Tracker(this);

        if (m_tracker->start())
            logger->addMessage(tr("Embedded Tracker [ON]"), Log::INFO);
        else
            logger->addMessage(tr("Failed to start the embedded tracker!"), Log::CRITICAL);
    }
    else {
        logger->addMessage(tr("Embedded Tracker [OFF]"), Log::INFO);
        if (m_tracker)
            delete m_tracker;
    }
}

void Session::enableBandwidthScheduler()
{
    if (!m_bwScheduler) {
        m_bwScheduler = new BandwidthScheduler(this);
        connect(m_bwScheduler.data(), SIGNAL(switchToAlternativeMode(bool)), this, SLOT(switchToAlternativeMode(bool)));
    }
    m_bwScheduler->start();
}

void Session::populateAdditionalTrackers()
{
    m_additionalTrackerList.clear();
    foreach (QString tracker, additionalTrackers().split("\n")) {
        tracker = tracker.trimmed();
        if (!tracker.isEmpty())
            m_additionalTrackerList << tracker;
    }
}

void Session::processBigRatios()
{
    qDebug("Process big ratios...");

    qreal globalMaxRatio = this->globalMaxRatio();
    foreach (TorrentHandle *const torrent, m_torrents) {
        if (torrent->isSeed()
            && (torrent->ratioLimit() != TorrentHandle::NO_RATIO_LIMIT)
            && !torrent->isForced()) {
            const qreal ratio = torrent->realRatio();
            qreal ratioLimit = torrent->ratioLimit();
            if (ratioLimit == TorrentHandle::USE_GLOBAL_RATIO) {
                // If Global Max Ratio is really set...
                ratioLimit = globalMaxRatio;
                if (ratioLimit < 0) continue;
            }
            qDebug("Ratio: %f (limit: %f)", ratio, ratioLimit);
            Q_ASSERT(ratioLimit >= 0.f);

            if ((ratio <= TorrentHandle::MAX_RATIO) && (ratio >= ratioLimit)) {
                Logger* const logger = Logger::instance();
                if (maxRatioAction() == Remove) {
                    logger->addMessage(tr("'%1' reached the maximum ratio you set. Removing...").arg(torrent->name()));
                    deleteTorrent(torrent->hash());
                }
                else {
                    // Pause it
                    if (!torrent->isPaused()) {
                        logger->addMessage(tr("'%1' reached the maximum ratio you set. Pausing...").arg(torrent->name()));
                        torrent->pause();
                    }
                }
            }
        }
    }
}

void Session::handleDownloadFailed(const QString &url, const QString &reason)
{
    emit downloadFromUrlFailed(url, reason);
}

void Session::handleRedirectedToMagnet(const QString &url, const QString &magnetUri)
{
    addTorrent_impl(m_downloadedTorrents.take(url), MagnetUri(magnetUri));
}

void Session::switchToAlternativeMode(bool alternative)
{
    changeSpeedLimitMode_impl(alternative);
}

// Add to BitTorrent session the downloaded torrent file
void Session::handleDownloadFinished(const QString &url, const QString &filePath)
{
    emit downloadFromUrlFinished(url);
    addTorrent_impl(m_downloadedTorrents.take(url), MagnetUri(), TorrentInfo::loadFromFile(filePath));
    Utils::Fs::forceRemove(filePath); // remove temporary file
}

// Return the torrent handle, given its hash
TorrentHandle *Session::findTorrent(const InfoHash &hash) const
{
    return m_torrents.value(hash);
}

bool Session::hasActiveTorrents() const
{
    foreach (TorrentHandle *const torrent, m_torrents)
        if (TorrentFilter::ActiveTorrent.match(torrent))
            return true;

    return false;
}

bool Session::hasUnfinishedTorrents() const
{
    foreach (TorrentHandle *const torrent, m_torrents)
        if (!torrent->isSeed() && !torrent->isPaused())
            return true;

    return false;
}

void Session::banIP(const QString &ip)
{
    QStringList bannedIPs = m_bannedIPs;
    if (!bannedIPs.contains(ip)) {
        libt::ip_filter filter = m_nativeSession->get_ip_filter();
        boost::system::error_code ec;
        libt::address addr = libt::address::from_string(ip.toLatin1().constData(), ec);
        Q_ASSERT(!ec);
        if (ec) return;
        filter.add_rule(addr, addr, libt::ip_filter::blocked);
        m_nativeSession->set_ip_filter(filter);

        bannedIPs << ip;
        m_bannedIPs = bannedIPs;
    }
}

// Delete a torrent from the session, given its hash
// deleteLocalFiles = true means that the torrent will be removed from the hard-drive too
bool Session::deleteTorrent(const QString &hash, bool deleteLocalFiles)
{
    TorrentHandle *const torrent = m_torrents.take(hash);
    if (!torrent) return false;

    qDebug("Deleting torrent with hash: %s", qPrintable(torrent->hash()));
    emit torrentAboutToBeRemoved(torrent);

    // Remove it from session
    if (deleteLocalFiles) {
        m_savePathsToRemove[torrent->hash()] = torrent->rootPath(true);
        m_nativeSession->remove_torrent(torrent->nativeHandle(), libt::session::delete_files);
    }
    else {
        QStringList unwantedFiles;
        if (torrent->hasMetadata())
            unwantedFiles = torrent->absoluteFilePathsUnwanted();
#if LIBTORRENT_VERSION_NUM < 10100
        m_nativeSession->remove_torrent(torrent->nativeHandle());
#else
        m_nativeSession->remove_torrent(torrent->nativeHandle(), libt::session::delete_partfile);
#endif
        // Remove unwanted and incomplete files
        foreach (const QString &unwantedFile, unwantedFiles) {
            qDebug("Removing unwanted file: %s", qPrintable(unwantedFile));
            Utils::Fs::forceRemove(unwantedFile);
            const QString parentFolder = Utils::Fs::branchPath(unwantedFile);
            qDebug("Attempt to remove parent folder (if empty): %s", qPrintable(parentFolder));
            QDir().rmpath(parentFolder);
        }
    }

    // Remove it from torrent resume directory
    QDir resumeDataDir(m_resumeFolderPath);
    QStringList filters;
    filters << QString("%1.*").arg(torrent->hash());
    const QStringList files = resumeDataDir.entryList(filters, QDir::Files, QDir::Unsorted);
    foreach (const QString &file, files)
        Utils::Fs::forceRemove(resumeDataDir.absoluteFilePath(file));

    if (deleteLocalFiles)
        Logger::instance()->addMessage(tr("'%1' was removed from transfer list and hard disk.", "'xxx.avi' was removed...").arg(torrent->name()));
    else
        Logger::instance()->addMessage(tr("'%1' was removed from transfer list.", "'xxx.avi' was removed...").arg(torrent->name()));

    delete torrent;
    qDebug("Torrent deleted.");
    return true;
}

bool Session::cancelLoadMetadata(const InfoHash &hash)
{
    if (!m_loadedMetadata.contains(hash)) return false;

    m_loadedMetadata.remove(hash);
    libt::torrent_handle torrent = m_nativeSession->find_torrent(hash);
    if (!torrent.is_valid()) return false;

    if (!torrent.status(0).has_metadata) {
        // if hidden torrent is still loading metadata...
        --m_extraLimit;
        adjustLimits();
    }

    // Remove it from session
    m_nativeSession->remove_torrent(torrent, libt::session::delete_files);
    qDebug("Preloaded torrent deleted.");
    return true;
}

void Session::increaseTorrentsPriority(const QStringList &hashes)
{
    std::priority_queue<QPair<int, TorrentHandle *>,
            std::vector<QPair<int, TorrentHandle *> >,
            std::greater<QPair<int, TorrentHandle *> > > torrentQueue;

    // Sort torrents by priority
    foreach (const InfoHash &hash, hashes) {
        TorrentHandle *const torrent = m_torrents.value(hash);
        if (torrent && !torrent->isSeed())
            torrentQueue.push(qMakePair(torrent->queuePosition(), torrent));
    }

    // Increase torrents priority (starting with the ones with highest priority)
    while (!torrentQueue.empty()) {
        TorrentHandle *const torrent = torrentQueue.top().second;
        torrentQueuePositionUp(torrent->nativeHandle());
        torrentQueue.pop();
    }
}

void Session::decreaseTorrentsPriority(const QStringList &hashes)
{
    std::priority_queue<QPair<int, TorrentHandle *>,
            std::vector<QPair<int, TorrentHandle *> >,
            std::less<QPair<int, TorrentHandle *> > > torrentQueue;

    // Sort torrents by priority
    foreach (const InfoHash &hash, hashes) {
        TorrentHandle *const torrent = m_torrents.value(hash);
        if (torrent && !torrent->isSeed())
            torrentQueue.push(qMakePair(torrent->queuePosition(), torrent));
    }

    // Decrease torrents priority (starting with the ones with lowest priority)
    while (!torrentQueue.empty()) {
        TorrentHandle *const torrent = torrentQueue.top().second;
        torrentQueuePositionDown(torrent->nativeHandle());
        torrentQueue.pop();
    }

    foreach (const InfoHash &hash, m_loadedMetadata.keys())
        torrentQueuePositionBottom(m_nativeSession->find_torrent(hash));
}

void Session::topTorrentsPriority(const QStringList &hashes)
{
    std::priority_queue<QPair<int, TorrentHandle *>,
            std::vector<QPair<int, TorrentHandle *> >,
            std::greater<QPair<int, TorrentHandle *> > > torrentQueue;

    // Sort torrents by priority
    foreach (const InfoHash &hash, hashes) {
        TorrentHandle *const torrent = m_torrents.value(hash);
        if (torrent && !torrent->isSeed())
            torrentQueue.push(qMakePair(torrent->queuePosition(), torrent));
    }

    // Top torrents priority (starting with the ones with highest priority)
    while (!torrentQueue.empty()) {
        TorrentHandle *const torrent = torrentQueue.top().second;
        torrentQueuePositionTop(torrent->nativeHandle());
        torrentQueue.pop();
    }
}

void Session::bottomTorrentsPriority(const QStringList &hashes)
{
    std::priority_queue<QPair<int, TorrentHandle *>,
            std::vector<QPair<int, TorrentHandle *> >,
            std::less<QPair<int, TorrentHandle *> > > torrentQueue;

    // Sort torrents by priority
    foreach (const InfoHash &hash, hashes) {
        TorrentHandle *const torrent = m_torrents.value(hash);
        if (torrent && !torrent->isSeed())
            torrentQueue.push(qMakePair(torrent->queuePosition(), torrent));
    }

    // Bottom torrents priority (starting with the ones with lowest priority)
    while (!torrentQueue.empty()) {
        TorrentHandle *const torrent = torrentQueue.top().second;
        torrentQueuePositionBottom(torrent->nativeHandle());
        torrentQueue.pop();
    }

    foreach (const InfoHash &hash, m_loadedMetadata.keys())
        torrentQueuePositionBottom(m_nativeSession->find_torrent(hash));
}

QHash<InfoHash, TorrentHandle *> Session::torrents() const
{
    return m_torrents;
}

TorrentStatusReport Session::torrentStatusReport() const
{
    return m_torrentStatusReport;
}

// source - .torrent file path/url or magnet uri
bool Session::addTorrent(QString source, const AddTorrentParams &params)
{
    MagnetUri magnetUri(source);
    if (magnetUri.isValid()) {
        return addTorrent_impl(params, magnetUri);
    }
    else if (Utils::Misc::isUrl(source)) {
        Logger::instance()->addMessage(tr("Downloading '%1', please wait...", "e.g: Downloading 'xxx.torrent', please wait...").arg(source));
        // Launch downloader
        Net::DownloadHandler *handler = Net::DownloadManager::instance()->downloadUrl(source, true, 10485760 /* 10MB */, true);
        connect(handler, SIGNAL(downloadFinished(QString, QString)), this, SLOT(handleDownloadFinished(QString, QString)));
        connect(handler, SIGNAL(downloadFailed(QString, QString)), this, SLOT(handleDownloadFailed(QString, QString)));
        connect(handler, SIGNAL(redirectedToMagnet(QString, QString)), this, SLOT(handleRedirectedToMagnet(QString, QString)));
        m_downloadedTorrents[handler->url()] = params;
    }
    else {
        TorrentFileGuard guard(source);
        guard.markAsAddedToSession();
        return addTorrent_impl(params, MagnetUri(), TorrentInfo::loadFromFile(source));
    }

    return false;
}

bool Session::addTorrent(const TorrentInfo &torrentInfo, const AddTorrentParams &params)
{
    if (!torrentInfo.isValid()) return false;

    return addTorrent_impl(params, MagnetUri(), torrentInfo);
}

// Add a torrent to the BitTorrent session
bool Session::addTorrent_impl(AddTorrentData addData, const MagnetUri &magnetUri,
                              TorrentInfo torrentInfo, const QByteArray &fastresumeData)
{
    addData.savePath = normalizeSavePath(
                addData.savePath,
                ((!addData.resumed && isAutoTMMDisabledByDefault()) ? defaultSavePath() : ""));

    if (!addData.category.isEmpty()) {
        if (!m_categories.contains(addData.category) && !addCategory(addData.category)) {
            qWarning() << "Couldn't create category" << addData.category;
            addData.category = "";
        }
    }

    libt::add_torrent_params p;
    InfoHash hash;
    std::vector<char> buf(fastresumeData.constData(), fastresumeData.constData() + fastresumeData.size());
    std::vector<boost::uint8_t> filePriorities;

    QString savePath;
    if (addData.savePath.isEmpty()) // using Automatic mode
        savePath = categorySavePath(addData.category);
    else  // using Manual mode
        savePath = addData.savePath;

    bool fromMagnetUri = magnetUri.isValid();
    if (fromMagnetUri) {
        hash = magnetUri.hash();

        if (m_loadedMetadata.contains(hash)) {
            // Adding preloaded torrent
            m_loadedMetadata.remove(hash);
            libt::torrent_handle handle = m_nativeSession->find_torrent(hash);
            --m_extraLimit;

            try {
                handle.auto_managed(false);
                handle.pause();
            }
            catch (std::exception &) {}

            adjustLimits();

            // use common 2nd step of torrent addition
            m_addingTorrents.insert(hash, addData);
            createTorrentHandle(handle);
            return true;
        }

        p = magnetUri.addTorrentParams();
    }
    else if (torrentInfo.isValid()) {
        // Metadata
        if (!addData.resumed && !addData.hasSeedStatus)
            findIncompleteFiles(torrentInfo, savePath);
        p.ti = torrentInfo.nativeInfo();
        hash = torrentInfo.hash();
    }
    else {
        // We can have an invalid torrentInfo when there isn't a matching
        // .torrent file to the .fastresume we loaded. Possibly from a
        // failed upgrade.
        return false;
    }

    if (addData.resumed && !fromMagnetUri) {
        // Set torrent fast resume data
        p.resume_data = buf;
        p.flags |= libt::add_torrent_params::flag_use_resume_save_path;
    }
    else {
        foreach (int prio, addData.filePriorities)
            filePriorities.push_back(prio);
        p.file_priorities = filePriorities;
    }

    // We should not add torrent if it already
    // processed or adding to session
    if (m_addingTorrents.contains(hash) || m_loadedMetadata.contains(hash)) return false;

    if (m_torrents.contains(hash)) {
        TorrentHandle *const torrent = m_torrents.value(hash);
        if (torrent->isPrivate() || (!fromMagnetUri && torrentInfo.isPrivate()))
            return false;
        torrent->addTrackers(fromMagnetUri ? magnetUri.trackers() : torrentInfo.trackers());
        torrent->addUrlSeeds(fromMagnetUri ? magnetUri.urlSeeds() : torrentInfo.urlSeeds());
        return true;
    }

    qDebug("Adding torrent...");
    qDebug(" -> Hash: %s", qPrintable(hash));

    // Preallocation mode
    if (isPreallocationEnabled())
        p.storage_mode = libt::storage_mode_allocate;
    else
        p.storage_mode = libt::storage_mode_sparse;

    p.flags |= libt::add_torrent_params::flag_paused; // Start in pause
    p.flags &= ~libt::add_torrent_params::flag_auto_managed; // Because it is added in paused state
    p.flags &= ~libt::add_torrent_params::flag_duplicate_is_error; // Already checked

    // Seeding mode
    // Skip checking and directly start seeding (new in libtorrent v0.15)
    if (addData.skipChecking)
        p.flags |= libt::add_torrent_params::flag_seed_mode;
    else
        p.flags &= ~libt::add_torrent_params::flag_seed_mode;

    // Limits
    p.max_connections = maxConnectionsPerTorrent();
    p.max_uploads = maxUploadsPerTorrent();
    p.save_path = Utils::String::toStdString(Utils::Fs::toNativePath(savePath));

    m_addingTorrents.insert(hash, addData);
    // Adding torrent to BitTorrent session
    m_nativeSession->async_add_torrent(p);
    return true;
}

bool Session::findIncompleteFiles(TorrentInfo &torrentInfo, QString &savePath) const
{
    auto findInDir = [](const QString &dirPath, TorrentInfo &torrentInfo) -> bool
    {
        bool found = false;
        if (torrentInfo.filesCount() == 1) {
            const QString filePath = dirPath + torrentInfo.filePath(0);
            if (QFile(filePath).exists()) {
                found = true;
            }
            else if (QFile(filePath + QB_EXT).exists()) {
                found = true;
                torrentInfo.renameFile(0, torrentInfo.filePath(0) + QB_EXT);
            }
        }
        else {
            QSet<QString> allFiles;
            int dirPathSize = dirPath.size();
            foreach (const QString &file, findAllFiles(dirPath + torrentInfo.name()))
                allFiles << file.mid(dirPathSize);
            for (int i = 0; i < torrentInfo.filesCount(); ++i) {
                QString filePath = torrentInfo.filePath(i);
                if (allFiles.contains(filePath)) {
                    found = true;
                }
                else {
                    filePath += QB_EXT;
                    if (allFiles.contains(filePath)) {
                        found = true;
                        torrentInfo.renameFile(i, filePath);
                    }
                }
            }
        }

        return found;
    };

    bool found = findInDir(savePath, torrentInfo);
    if (!found && isTempPathEnabled()) {
        savePath = torrentTempPath(torrentInfo.hash());
        found = findInDir(savePath, torrentInfo);
    }

    return found;
}

// Add a torrent to the BitTorrent session in hidden mode
// and force it to load its metadata
bool Session::loadMetadata(const MagnetUri &magnetUri)
{
    if (!magnetUri.isValid()) return false;

    InfoHash hash = magnetUri.hash();
    QString name = magnetUri.name();

    // We should not add torrent if it already
    // processed or adding to session
    if (m_torrents.contains(hash)) return false;
    if (m_addingTorrents.contains(hash)) return false;
    if (m_loadedMetadata.contains(hash)) return false;

    qDebug("Adding torrent to preload metadata...");
    qDebug(" -> Hash: %s", qPrintable(hash));
    qDebug(" -> Name: %s", qPrintable(name));

    libt::add_torrent_params p = magnetUri.addTorrentParams();

    // Flags
    // Preallocation mode
    if (isPreallocationEnabled())
        p.storage_mode = libt::storage_mode_allocate;
    else
        p.storage_mode = libt::storage_mode_sparse;

    // Limits
    p.max_connections = maxConnectionsPerTorrent();
    p.max_uploads = maxUploadsPerTorrent();

    QString savePath = QString("%1/%2").arg(QDir::tempPath()).arg(hash);
    p.save_path = Utils::String::toStdString(Utils::Fs::toNativePath(savePath));

    // Forced start
    p.flags &= ~libt::add_torrent_params::flag_paused;
    p.flags &= ~libt::add_torrent_params::flag_auto_managed;
    // Solution to avoid accidental file writes
    p.flags |= libt::add_torrent_params::flag_upload_mode;

    // Adding torrent to BitTorrent session
    libt::error_code ec;
    libt::torrent_handle h = m_nativeSession->add_torrent(p, ec);
    if (ec) return false;

    // waiting for metadata...
    m_loadedMetadata.insert(h.info_hash(), TorrentInfo());
    ++m_extraLimit;
    adjustLimits();

    return true;
}

void Session::exportTorrentFile(TorrentHandle *const torrent, TorrentExportFolder folder)
{
    Q_ASSERT(((folder == TorrentExportFolder::Regular) && !torrentExportDirectory().isEmpty()) ||
             ((folder == TorrentExportFolder::Finished) && !finishedTorrentExportDirectory().isEmpty()));

    QString validName = Utils::Fs::toValidFileSystemName(torrent->name());
    QString torrentFilename = QString("%1.torrent").arg(torrent->hash());
    QString torrentExportFilename = QString("%1.torrent").arg(validName);
    QString torrentPath = QDir(m_resumeFolderPath).absoluteFilePath(torrentFilename);
    QDir exportPath(folder == TorrentExportFolder::Regular ? torrentExportDirectory() : finishedTorrentExportDirectory());
    if (exportPath.exists() || exportPath.mkpath(exportPath.absolutePath())) {
        QString newTorrentPath = exportPath.absoluteFilePath(torrentExportFilename);
        int counter = 0;
        while (QFile::exists(newTorrentPath) && !Utils::Fs::sameFiles(torrentPath, newTorrentPath)) {
            // Append number to torrent name to make it unique
            torrentExportFilename = QString("%1 %2.torrent").arg(validName).arg(++counter);
            newTorrentPath = exportPath.absoluteFilePath(torrentExportFilename);
        }

        if (!QFile::exists(newTorrentPath))
            QFile::copy(torrentPath, newTorrentPath);
    }
}

void Session::changeSpeedLimitMode_impl(bool alternative)
{
    qDebug() << Q_FUNC_INFO << alternative;
    if (alternative == isAltGlobalSpeedLimitEnabled()) return;

    // Save new state to remember it on startup
    m_isAltGlobalSpeedLimitEnabled = alternative;
    configureDeferred();
    // Notify
    emit speedLimitModeChanged(alternative);
}

void Session::generateResumeData(bool final)
{
    foreach (TorrentHandle *const torrent, m_torrents) {
        if (!torrent->isValid()) continue;
        if (torrent->hasMissingFiles()) continue;
        if (torrent->isChecking() || torrent->hasError()) continue;
        if (!final && !torrent->needSaveResumeData()) continue;

        saveTorrentResumeData(torrent);
        qDebug("Saving fastresume data for %s", qPrintable(torrent->name()));
    }
}

// Called on exit
void Session::saveResumeData()
{
    qDebug("Saving fast resume data...");

    // Pause session
    m_nativeSession->pause();

    generateResumeData(true);

    while (m_numResumeData > 0) {
        std::vector<libt::alert *> alerts;
        getPendingAlerts(alerts, 30 * 1000);
        if (alerts.empty()) {
            std::cerr << " aborting with " << m_numResumeData
                      << " outstanding torrents to save resume data for"
                      << std::endl;
            break;
        }

        for (const auto a: alerts) {
            switch (a->type()) {
            case libt::save_resume_data_failed_alert::alert_type:
            case libt::save_resume_data_alert::alert_type:
                TorrentHandle *torrent = m_torrents.take(static_cast<libt::torrent_alert *>(a)->handle.info_hash());
                if (torrent)
                    torrent->handleAlert(a);
                break;
            }
#if LIBTORRENT_VERSION_NUM < 10100
            delete a;
#endif
        }
    }
}

void Session::setDefaultSavePath(QString path)
{
    path = normalizeSavePath(path);
    if (path == m_defaultSavePath) return;

    m_defaultSavePath = path;

    if (isDisableAutoTMMWhenDefaultSavePathChanged())
        foreach (TorrentHandle *const torrent, torrents())
            torrent->setAutoTMMEnabled(false);
    else
        foreach (TorrentHandle *const torrent, torrents())
            torrent->handleCategorySavePathChanged();
}

void Session::setTempPath(QString path)
{
    path = normalizeSavePath(path, defaultSavePath() + "temp/");
    if (path == m_tempPath) return;

    m_tempPath = path;

    foreach (TorrentHandle *const torrent, m_torrents)
        torrent->handleTempPathChanged();
}

void Session::networkOnlineStateChanged(const bool online)
{
    Logger::instance()->addMessage(tr("System network status changed to %1", "e.g: System network status changed to ONLINE").arg(online ? tr("ONLINE") : tr("OFFLINE")), Log::INFO);
}

void Session::networkConfigurationChange(const QNetworkConfiguration& cfg)
{
    const QString configuredInterfaceName = networkInterface();
    // Empty means "Any Interface". In this case libtorrent has binded to 0.0.0.0 so any change to any interface will
    // be automatically picked up. Otherwise we would rebinding here to 0.0.0.0 again.
    if (configuredInterfaceName.isEmpty()) return;

    const QString changedInterface = cfg.name();

    // workaround for QTBUG-52633: check interface IPs, react only if the IPs have changed
    // seems to be present only with NetworkManager, hence Q_OS_LINUX
#if defined Q_OS_LINUX && QT_VERSION >= QT_VERSION_CHECK(5, 0, 0) // && QT_VERSION <= QT_VERSION_CHECK(5, ?, ?)
    static QStringList boundIPs = getListeningIPs();
    const QStringList newBoundIPs = getListeningIPs();
    if ((configuredInterfaceName == changedInterface) && (boundIPs != newBoundIPs)) {
        boundIPs = newBoundIPs;
#else
    if (configuredInterfaceName == changedInterface) {
#endif
        Logger::instance()->addMessage(tr("Network configuration of %1 has changed, refreshing session binding", "e.g: Network configuration of tun0 has changed, refreshing session binding").arg(changedInterface), Log::INFO);
        configureListeningInterface();
    }
}

const QStringList Session::getListeningIPs()
{
    Logger* const logger = Logger::instance();
    QStringList IPs;

    const QString ifaceName = networkInterface();
    const QString ifaceAddr = networkInterfaceAddress();
    const bool listenIPv6 = isIPv6Enabled();

    if (!ifaceAddr.isEmpty()) {
        QHostAddress addr(ifaceAddr);
        if (addr.isNull()) {
            logger->addMessage(tr("Configured network interface address %1 isn't valid.", "Configured network interface address 124.5.1568.1 isn't valid.").arg(ifaceAddr), Log::CRITICAL);
            IPs.append("127.0.0.1"); // Force listening to localhost and avoid accidental connection that will expose user data.
            return IPs;
        }
    }

    if (ifaceName.isEmpty()) {
        if (!ifaceAddr.isEmpty())
            IPs.append(ifaceAddr);
        else
            IPs.append(QString());

        return IPs;
    }

    // Attempt to listen on provided interface
    const QNetworkInterface networkIFace = QNetworkInterface::interfaceFromName(ifaceName);
    if (!networkIFace.isValid()) {
        qDebug("Invalid network interface: %s", qPrintable(ifaceName));
        logger->addMessage(tr("The network interface defined is invalid: %1").arg(ifaceName), Log::CRITICAL);
        IPs.append("127.0.0.1"); // Force listening to localhost and avoid accidental connection that will expose user data.
        return IPs;
    }

    const QList<QNetworkAddressEntry> addresses = networkIFace.addressEntries();
    qDebug("This network interface has %d IP addresses", addresses.size());
    QHostAddress ip;
    QString ipString;
    QAbstractSocket::NetworkLayerProtocol protocol;
    foreach (const QNetworkAddressEntry &entry, addresses) {
        ip = entry.ip();
        ipString = ip.toString();
        protocol = ip.protocol();
        Q_ASSERT(protocol == QAbstractSocket::IPv4Protocol || protocol == QAbstractSocket::IPv6Protocol);
        if ((!listenIPv6 && (protocol == QAbstractSocket::IPv6Protocol))
            || (listenIPv6 && (protocol == QAbstractSocket::IPv4Protocol)))
            continue;

        //If an iface address has been defined only allow ip's that match it to go through
        if (!ifaceAddr.isEmpty()) {
            if (ifaceAddr == ipString) {
                IPs.append(ipString);
                break;
            }
        }
        else {
            IPs.append(ipString);
        }
    }

    // Make sure there is at least one IP
    // At this point there was a valid network interface, with no suitable IP.
    if (IPs.size() == 0) {
        logger->addMessage(tr("qBittorrent didn't find an %1 local address to listen on", "qBittorrent didn't find an IPv4 local address to listen on").arg(listenIPv6 ? "IPv6" : "IPv4"), Log::CRITICAL);
        IPs.append("127.0.0.1"); // Force listening to localhost and avoid accidental connection that will expose user data.
        return IPs;
    }

    return IPs;
}

// Set the ports range in which is chosen the port
// the BitTorrent session will listen to
void Session::configureListeningInterface()
{
#if LIBTORRENT_VERSION_NUM < 10100
    const ushort port = this->port();
    qDebug() << Q_FUNC_INFO << port;

    Logger* const logger = Logger::instance();

    std::pair<int, int> ports(port, port);
    libt::error_code ec;
    const QStringList IPs = getListeningIPs();

    foreach (const QString ip, IPs) {
        if (ip.isEmpty()) {
            logger->addMessage(tr("qBittorrent is trying to listen on any interface port: %1", "e.g: qBittorrent is trying to listen on any interface port: TCP/6881").arg(QString::number(port)), Log::INFO);
            m_nativeSession->listen_on(ports, ec, 0, libt::session::listen_no_system_port);

            if (ec)
                logger->addMessage(tr("qBittorrent failed to listen on any interface port: %1. Reason: %2.", "e.g: qBittorrent failed to listen on any interface port: TCP/6881. Reason: no such interface" ).arg(QString::number(port)).arg(QString::fromLocal8Bit(ec.message().c_str())), Log::CRITICAL);

            return;
        }

        m_nativeSession->listen_on(ports, ec, ip.toLatin1().constData(), libt::session::listen_no_system_port);
        if (!ec) {
            logger->addMessage(tr("qBittorrent is trying to listen on interface %1 port: %2", "e.g: qBittorrent is trying to listen on interface 192.168.0.1 port: TCP/6881").arg(ip).arg(port), Log::INFO);
            return;
        }
    }
#else
    m_listenInterfaceChanged = true;
    configureDeferred();
#endif
}

int Session::globalDownloadSpeedLimit() const
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    return m_globalDownloadSpeedLimit * 1024;
}

void Session::setGlobalDownloadSpeedLimit(int limit)
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    limit /= 1024;
    if (limit < 0) limit = 0;
    if (limit == globalDownloadSpeedLimit()) return;

    m_globalDownloadSpeedLimit = limit;
    if (!isAltGlobalSpeedLimitEnabled())
        configureDeferred();
}

int Session::globalUploadSpeedLimit() const
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    return m_globalUploadSpeedLimit * 1024;
}

void Session::setGlobalUploadSpeedLimit(int limit)
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    limit /= 1024;
    if (limit < 0) limit = 0;
    if (limit == globalUploadSpeedLimit()) return;

    m_globalUploadSpeedLimit = limit;
    if (!isAltGlobalSpeedLimitEnabled())
        configureDeferred();
}

int Session::altGlobalDownloadSpeedLimit() const
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    return m_altGlobalDownloadSpeedLimit * 1024;
}

void Session::setAltGlobalDownloadSpeedLimit(int limit)
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    limit /= 1024;
    if (limit < 0) limit = 0;
    if (limit == altGlobalDownloadSpeedLimit()) return;

    m_altGlobalDownloadSpeedLimit = limit;
    if (isAltGlobalSpeedLimitEnabled())
        configureDeferred();
}

int Session::altGlobalUploadSpeedLimit() const
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    return m_altGlobalUploadSpeedLimit * 1024;
}

void Session::setAltGlobalUploadSpeedLimit(int limit)
{
    // Unfortunately the value was saved as KiB instead of B.
    // But it is better to pass it around internally(+ webui) as Bytes.
    limit /= 1024;
    if (limit < 0) limit = 0;
    if (limit == altGlobalUploadSpeedLimit()) return;

    m_altGlobalUploadSpeedLimit = limit;
    if (isAltGlobalSpeedLimitEnabled())
        configureDeferred();
}

int Session::downloadSpeedLimit() const
{
    return isAltGlobalSpeedLimitEnabled()
            ? altGlobalDownloadSpeedLimit()
            : globalDownloadSpeedLimit();
}

void Session::setDownloadSpeedLimit(int limit)
{
    if (isAltGlobalSpeedLimitEnabled())
        setAltGlobalDownloadSpeedLimit(limit);
    else
        setGlobalDownloadSpeedLimit(limit);
}

int Session::uploadSpeedLimit() const
{
    return isAltGlobalSpeedLimitEnabled()
            ? altGlobalUploadSpeedLimit()
            : globalUploadSpeedLimit();
}

void Session::setUploadSpeedLimit(int limit)
{
    if (isAltGlobalSpeedLimitEnabled())
        setAltGlobalUploadSpeedLimit(limit);
    else
        setGlobalUploadSpeedLimit(limit);
}

bool Session::isAltGlobalSpeedLimitEnabled() const
{
    return m_isAltGlobalSpeedLimitEnabled;
}

void Session::setAltGlobalSpeedLimitEnabled(bool enabled)
{
    // Stop the scheduler when the user has manually changed the bandwidth mode
    if (isBandwidthSchedulerEnabled())
        setBandwidthSchedulerEnabled(false);

    changeSpeedLimitMode_impl(enabled);
}

bool Session::isBandwidthSchedulerEnabled() const
{
    return m_isBandwidthSchedulerEnabled;
}

void Session::setBandwidthSchedulerEnabled(bool enabled)
{
    if (enabled != isBandwidthSchedulerEnabled()) {
        m_isBandwidthSchedulerEnabled = enabled;
        if (enabled)
            enableBandwidthScheduler();
        else
            delete m_bwScheduler;
    }
}

uint Session::saveResumeDataInterval() const
{
    return m_saveResumeDataInterval;
}

void Session::setSaveResumeDataInterval(uint value)
{
    if (value != saveResumeDataInterval()) {
        m_saveResumeDataInterval = value;
        m_resumeDataTimer->setInterval(value * 60 * 1000);
    }
}

int Session::port() const
{
    static int randomPort = rand() % 64512 + 1024;
    if (useRandomPort())
        return randomPort;
    return m_port;
}

void Session::setPort(int port)
{
    if (port != this->port()) {
        m_port = port;
        configureListeningInterface();
    }
}

bool Session::useRandomPort() const
{
    return m_useRandomPort;
}

void Session::setUseRandomPort(bool value)
{
    m_useRandomPort = value;
}

QString Session::networkInterface() const
{
    return m_networkInterface;
}

void Session::setNetworkInterface(const QString &interface)
{
    if (interface != networkInterface()) {
        m_networkInterface = interface;
        configureListeningInterface();
    }
}

QString Session::networkInterfaceName() const
{
    return m_networkInterfaceName;
}

void Session::setNetworkInterfaceName(const QString &name)
{
    m_networkInterfaceName = name;
}

QString Session::networkInterfaceAddress() const
{
    return m_networkInterfaceAddress;
}

void Session::setNetworkInterfaceAddress(const QString &address)
{
    if (address != networkInterfaceAddress()) {
        m_networkInterfaceAddress = address;
        configureListeningInterface();
    }
}

bool Session::isIPv6Enabled() const
{
    return m_isIPv6Enabled;
}

void Session::setIPv6Enabled(bool enabled)
{
    if (enabled != isIPv6Enabled()) {
        m_isIPv6Enabled = enabled;
        configureListeningInterface();
    }
}

int Session::encryption() const
{
    return m_encryption;
}

void Session::setEncryption(int state)
{
    if (state != encryption()) {
        m_encryption = state;
        configureDeferred();
        Logger::instance()->addMessage(
                    tr("Encryption support [%1]")
                    .arg(state == 0 ? tr("ON") : state == 1 ? tr("FORCED") : tr("OFF"))
                    , Log::INFO);
    }
}

bool Session::isForceProxyEnabled() const
{
    return m_isForceProxyEnabled;
}

void Session::setForceProxyEnabled(bool enabled)
{
    if (enabled != isForceProxyEnabled()) {
        m_isForceProxyEnabled = enabled;
        configureDeferred();
    }
}

bool Session::isProxyPeerConnectionsEnabled() const
{
    return m_isProxyPeerConnectionsEnabled;
}

void Session::setProxyPeerConnectionsEnabled(bool enabled)
{
    if (enabled != isProxyPeerConnectionsEnabled()) {
        m_isProxyPeerConnectionsEnabled = enabled;
        configureDeferred();
    }
}

bool Session::isAddTrackersEnabled() const
{
    return m_isAddTrackersEnabled;
}

void Session::setAddTrackersEnabled(bool enabled)
{
    m_isAddTrackersEnabled = enabled;
}

QString Session::additionalTrackers() const
{
    return m_additionalTrackers;
}

void Session::setAdditionalTrackers(const QString &trackers)
{
    if (trackers != additionalTrackers()) {
        m_additionalTrackers = trackers;
        populateAdditionalTrackers();
    }
}

bool Session::isIPFilteringEnabled() const
{
    return m_isIPFilteringEnabled;
}

void Session::setIPFilteringEnabled(bool enabled)
{
    if (enabled != m_isIPFilteringEnabled) {
        m_isIPFilteringEnabled = enabled;
        m_IPFilteringChanged = true;
        configureDeferred();
    }
}

QString Session::IPFilterFile() const
{
    return Utils::Fs::fromNativePath(m_IPFilterFile);
}

void Session::setIPFilterFile(QString path)
{
    path = Utils::Fs::fromNativePath(path);
    if (path != IPFilterFile()) {
        m_IPFilterFile = path;
        m_IPFilteringChanged = true;
        configureDeferred();
    }
}

int Session::maxConnectionsPerTorrent() const
{
    return m_maxConnectionsPerTorrent;
}

void Session::setMaxConnectionsPerTorrent(int max)
{
    max = (max > 0) ? max : -1;
    if (max != maxConnectionsPerTorrent()) {
        m_maxConnectionsPerTorrent = max;

        // Apply this to all session torrents
        for (const auto &handle: m_nativeSession->get_torrents()) {
            if (!handle.is_valid()) continue;
            try {
                handle.set_max_connections(max);
            }
            catch(std::exception) {}
        }
    }
}

int Session::maxUploadsPerTorrent() const
{
    return m_maxUploadsPerTorrent;
}

void Session::setMaxUploadsPerTorrent(int max)
{
    max = (max > 0) ? max : -1;
    if (max != maxUploadsPerTorrent()) {
        m_maxUploadsPerTorrent = max;

        // Apply this to all session torrents
        for (const auto &handle: m_nativeSession->get_torrents()) {
            if (!handle.is_valid()) continue;
            try {
                handle.set_max_uploads(max);
            }
            catch(std::exception) {}
        }
    }
}

bool Session::announceToAllTrackers() const
{
    return m_announceToAllTrackers;
}

void Session::setAnnounceToAllTrackers(bool val)
{
    if (val != m_announceToAllTrackers) {
        m_announceToAllTrackers = val;
        configureDeferred();
    }
}

uint Session::diskCacheSize() const
{
    uint size = m_diskCacheSize;
    // These macros may not be available on compilers other than MSVC and GCC
#if defined(__x86_64__) || defined(_M_X64)
    size = qMin(size, 4096u);  // 4GiB
#else
    // When build as 32bit binary, set the maximum at less than 2GB to prevent crashes
    // allocate 1536MiB and leave 512MiB to the rest of program data in RAM
    size = qMin(size, 1536u);
#endif
    return size;
}

void Session::setDiskCacheSize(uint size)
{
#if defined(__x86_64__) || defined(_M_X64)
    size = qMin(size, 4096u);  // 4GiB
#else
    // allocate 1536MiB and leave 512MiB to the rest of program data in RAM
    size = qMin(size, 1536u);
#endif
    if (size != m_diskCacheSize) {
        m_diskCacheSize = size;
        configureDeferred();
    }
}

uint Session::diskCacheTTL() const
{
    return m_diskCacheTTL;
}

void Session::setDiskCacheTTL(uint ttl)
{
    if (ttl != m_diskCacheTTL) {
        m_diskCacheTTL = ttl;
        configureDeferred();
    }
}

bool Session::useOSCache() const
{
    return m_useOSCache;
}

void Session::setUseOSCache(bool use)
{
    if (use != m_useOSCache) {
        m_useOSCache = use;
        configureDeferred();
    }
}

bool Session::isAnonymousModeEnabled() const
{
    return m_isAnonymousModeEnabled;
}

void Session::setAnonymousModeEnabled(bool enabled)
{
    if (enabled != m_isAnonymousModeEnabled) {
        m_isAnonymousModeEnabled = enabled;
        configureDeferred();
        Logger::instance()->addMessage(
                    tr("Anonymous mode [%1]").arg(isAnonymousModeEnabled() ? tr("ON") : tr("OFF"))
                    , Log::INFO);
    }
}

bool Session::isQueueingSystemEnabled() const
{
    return m_isQueueingEnabled;
}

void Session::setQueueingSystemEnabled(bool enabled)
{
    if (enabled != m_isQueueingEnabled) {
        m_isQueueingEnabled = enabled;
        configureDeferred();
    }
}

int Session::maxActiveDownloads() const
{
    return m_maxActiveDownloads;
}

void Session::setMaxActiveDownloads(int max)
{
    max = std::max(max, -1);
    if (max != m_maxActiveDownloads) {
        m_maxActiveDownloads = max;
        configureDeferred();
    }
}

int Session::maxActiveUploads() const
{
    return m_maxActiveUploads;
}

void Session::setMaxActiveUploads(int max)
{
    max = std::max(max, -1);
    if (max != m_maxActiveUploads) {
        m_maxActiveUploads = max;
        configureDeferred();
    }
}

int Session::maxActiveTorrents() const
{
    return m_maxActiveTorrents;
}

void Session::setMaxActiveTorrents(int max)
{
    max = std::max(max, -1);
    if (max != m_maxActiveTorrents) {
        m_maxActiveTorrents = max;
        configureDeferred();
    }
}

bool Session::ignoreSlowTorrentsForQueueing() const
{
    return m_ignoreSlowTorrentsForQueueing;
}

void Session::setIgnoreSlowTorrentsForQueueing(bool ignore)
{
    if (ignore != m_ignoreSlowTorrentsForQueueing) {
        m_ignoreSlowTorrentsForQueueing = ignore;
        configureDeferred();
    }
}

uint Session::outgoingPortsMin() const
{
    return m_outgoingPortsMin;
}

void Session::setOutgoingPortsMin(uint min)
{
    if (min != m_outgoingPortsMin) {
        m_outgoingPortsMin = min;
        configureDeferred();
    }
}

uint Session::outgoingPortsMax() const
{
    return m_outgoingPortsMax;
}

void Session::setOutgoingPortsMax(uint max)
{
    if (max != m_outgoingPortsMax) {
        m_outgoingPortsMax = max;
        configureDeferred();
    }
}

bool Session::ignoreLimitsOnLAN() const
{
    return m_ignoreLimitsOnLAN;
}

void Session::setIgnoreLimitsOnLAN(bool ignore)
{
    if (ignore != m_ignoreLimitsOnLAN) {
        m_ignoreLimitsOnLAN = ignore;
        configureDeferred();
    }
}

bool Session::includeOverheadInLimits() const
{
    return m_includeOverheadInLimits;
}

void Session::setIncludeOverheadInLimits(bool include)
{
    if (include != m_includeOverheadInLimits) {
        m_includeOverheadInLimits = include;
        configureDeferred();
    }
}

QString Session::announceIP() const
{
    return m_announceIP;
}

void Session::setAnnounceIP(const QString &ip)
{
    if (ip != m_announceIP) {
        m_announceIP = ip;
        configureDeferred();
    }
}

bool Session::isSuperSeedingEnabled() const
{
    return m_isSuperSeedingEnabled;
}

void Session::setSuperSeedingEnabled(bool enabled)
{
    if (enabled != m_isSuperSeedingEnabled) {
        m_isSuperSeedingEnabled = enabled;
        configureDeferred();
    }
}

int Session::maxConnections() const
{
    return m_maxConnections;
}

void Session::setMaxConnections(int max)
{
    max = (max > 0) ? max : -1;
    if (max != m_maxConnections) {
        m_maxConnections = max;
        configureDeferred();
    }
}

int Session::maxHalfOpenConnections() const
{
    return m_maxHalfOpenConnections;
}

void Session::setMaxHalfOpenConnections(int max)
{
    max = (max > 0) ? max : -1;
    if (max != m_maxHalfOpenConnections) {
        m_maxHalfOpenConnections = max;
        configureDeferred();
    }
}

int Session::maxUploads() const
{
    return m_maxUploads;
}

void Session::setMaxUploads(int max)
{
    max = (max > 0) ? max : -1;
    if (max != m_maxUploads) {
        m_maxUploads = max;
        configureDeferred();
    }
}

bool Session::isUTPEnabled() const
{
    return m_isUTPEnabled;
}

void Session::setUTPEnabled(bool enabled)
{
    if (enabled != m_isUTPEnabled) {
        m_isUTPEnabled = enabled;
        configureDeferred();
    }
}

bool Session::isUTPRateLimited() const
{
    return m_isUTPRateLimited;
}

void Session::setUTPRateLimited(bool limited)
{
    if (limited != m_isUTPRateLimited) {
        m_isUTPRateLimited = limited;
        configureDeferred();
    }
}

bool Session::isTrackerFilteringEnabled() const
{
    return m_isTrackerFilteringEnabled;
}

void Session::setTrackerFilteringEnabled(bool enabled)
{
    if (enabled != m_isTrackerFilteringEnabled) {
        m_isTrackerFilteringEnabled = enabled;
        configureDeferred();
    }
}

bool Session::isListening() const
{
    return m_nativeSession->is_listening();
}

MaxRatioAction Session::maxRatioAction() const
{
    return static_cast<MaxRatioAction>(m_maxRatioAction.value());
}

void Session::setMaxRatioAction(MaxRatioAction act)
{
    m_maxRatioAction = static_cast<int>(act);
}

// If this functions returns true, we cannot add torrent to session,
// but it is still possible to merge trackers in some case
bool Session::isKnownTorrent(const InfoHash &hash) const
{
    return (m_torrents.contains(hash)
            || m_addingTorrents.contains(hash)
            || m_loadedMetadata.contains(hash));
}

void Session::updateRatioTimer()
{
    if ((globalMaxRatio() == -1) && !hasPerTorrentRatioLimit()) {
        if (m_bigRatioTimer->isActive())
            m_bigRatioTimer->stop();
    }
    else if (!m_bigRatioTimer->isActive()) {
        m_bigRatioTimer->start();
    }
}

void Session::handleTorrentRatioLimitChanged(TorrentHandle *const torrent)
{
    Q_UNUSED(torrent);
    updateRatioTimer();
}

void Session::saveTorrentResumeData(TorrentHandle *const torrent)
{
    torrent->saveResumeData();
    ++m_numResumeData;
}

void Session::handleTorrentSavePathChanged(TorrentHandle *const torrent)
{
    emit torrentSavePathChanged(torrent);
}

void Session::handleTorrentCategoryChanged(TorrentHandle *const torrent, const QString &oldCategory)
{
    emit torrentCategoryChanged(torrent, oldCategory);
}

void Session::handleTorrentSavingModeChanged(TorrentHandle * const torrent)
{
    emit torrentSavingModeChanged(torrent);
}

void Session::handleTorrentTrackersAdded(TorrentHandle *const torrent, const QList<TrackerEntry> &newTrackers)
{
    foreach (const TrackerEntry &newTracker, newTrackers)
        Logger::instance()->addMessage(tr("Tracker '%1' was added to torrent '%2'").arg(newTracker.url()).arg(torrent->name()));
    emit trackersAdded(torrent, newTrackers);
    if (torrent->trackers().size() == newTrackers.size())
        emit trackerlessStateChanged(torrent, false);
    emit trackersChanged(torrent);
}

void Session::handleTorrentTrackersRemoved(TorrentHandle *const torrent, const QList<TrackerEntry> &deletedTrackers)
{
    foreach (const TrackerEntry &deletedTracker, deletedTrackers)
        Logger::instance()->addMessage(tr("Tracker '%1' was deleted from torrent '%2'").arg(deletedTracker.url()).arg(torrent->name()));
    emit trackersRemoved(torrent, deletedTrackers);
    if (torrent->trackers().size() == 0)
        emit trackerlessStateChanged(torrent, true);
    emit trackersChanged(torrent);
}

void Session::handleTorrentTrackersChanged(TorrentHandle *const torrent)
{
    emit trackersChanged(torrent);
}

void Session::handleTorrentUrlSeedsAdded(TorrentHandle *const torrent, const QList<QUrl> &newUrlSeeds)
{
    foreach (const QUrl &newUrlSeed, newUrlSeeds)
        Logger::instance()->addMessage(tr("URL seed '%1' was added to torrent '%2'").arg(newUrlSeed.toString()).arg(torrent->name()));
}

void Session::handleTorrentUrlSeedsRemoved(TorrentHandle *const torrent, const QList<QUrl> &urlSeeds)
{
    foreach (const QUrl &urlSeed, urlSeeds)
        Logger::instance()->addMessage(tr("URL seed '%1' was removed from torrent '%2'").arg(urlSeed.toString()).arg(torrent->name()));
}

void Session::handleTorrentMetadataReceived(TorrentHandle *const torrent)
{
    saveTorrentResumeData(torrent);

    // Save metadata
    const QDir resumeDataDir(m_resumeFolderPath);
    QString torrentFile = resumeDataDir.absoluteFilePath(QString("%1.torrent").arg(torrent->hash()));
    if (torrent->saveTorrentFile(torrentFile)) {
        // Copy the torrent file to the export folder
        if (!torrentExportDirectory().isEmpty())
            exportTorrentFile(torrent);
    }

    emit torrentMetadataLoaded(torrent);
}

void Session::handleTorrentPaused(TorrentHandle *const torrent)
{
    if (!torrent->hasError() && !torrent->hasMissingFiles())
        saveTorrentResumeData(torrent);
    emit torrentPaused(torrent);
}

void Session::handleTorrentResumed(TorrentHandle *const torrent)
{
    emit torrentResumed(torrent);
}

void Session::handleTorrentChecked(TorrentHandle *const torrent)
{
    emit torrentFinishedChecking(torrent);
}

void Session::handleTorrentFinished(TorrentHandle *const torrent)
{
    if (!torrent->hasError() && !torrent->hasMissingFiles())
        saveTorrentResumeData(torrent);
    emit torrentFinished(torrent);

    qDebug("Checking if the torrent contains torrent files to download");
    // Check if there are torrent files inside
    for (int i = 0; i < torrent->filesCount(); ++i) {
        const QString torrentRelpath = torrent->filePath(i);
        if (torrentRelpath.endsWith(".torrent", Qt::CaseInsensitive)) {
            qDebug("Found possible recursive torrent download.");
            const QString torrentFullpath = torrent->savePath(true) + "/" + torrentRelpath;
            qDebug("Full subtorrent path is %s", qPrintable(torrentFullpath));
            TorrentInfo torrentInfo = TorrentInfo::loadFromFile(torrentFullpath);
            if (torrentInfo.isValid()) {
                qDebug("emitting recursiveTorrentDownloadPossible()");
                emit recursiveTorrentDownloadPossible(torrent);
                break;
            }
            else {
                qDebug("Caught error loading torrent");
                Logger::instance()->addMessage(tr("Unable to decode '%1' torrent file.").arg(Utils::Fs::toNativePath(torrentFullpath)), Log::CRITICAL);
            }
        }
    }

    // Move .torrent file to another folder
    if (!finishedTorrentExportDirectory().isEmpty())
        exportTorrentFile(torrent, TorrentExportFolder::Finished);

    if (!hasUnfinishedTorrents())
        emit allTorrentsFinished();
}

void Session::handleTorrentResumeDataReady(TorrentHandle *const torrent, const libtorrent::entry &data)
{
    --m_numResumeData;

    // Separated thread is used for the blocking IO which results in slow processing of many torrents.
    // Encoding data in parallel while doing IO saves time. Copying libtorrent::entry objects around
    // isn't cheap too.

    QByteArray out;
    libt::bencode(std::back_inserter(out), data);

    QMetaObject::invokeMethod(m_resumeDataSavingManager, "saveResumeData",
                              Q_ARG(QString, torrent->hash()), Q_ARG(QByteArray, out));
}

void Session::handleTorrentResumeDataFailed(TorrentHandle *const torrent)
{
    Q_UNUSED(torrent)
    --m_numResumeData;
}

void Session::handleTorrentTrackerReply(TorrentHandle *const torrent, const QString &trackerUrl)
{
    emit trackerSuccess(torrent, trackerUrl);
}

void Session::handleTorrentTrackerError(TorrentHandle *const torrent, const QString &trackerUrl)
{
    emit trackerError(torrent, trackerUrl);
}

void Session::handleTorrentTrackerAuthenticationRequired(TorrentHandle *const torrent, const QString &trackerUrl)
{
    Q_UNUSED(trackerUrl);
    emit trackerAuthenticationRequired(torrent);
}

void Session::handleTorrentTrackerWarning(TorrentHandle *const torrent, const QString &trackerUrl)
{
    emit trackerWarning(torrent, trackerUrl);
}

bool Session::hasPerTorrentRatioLimit() const
{
    foreach (TorrentHandle *const torrent, m_torrents)
        if (torrent->ratioLimit() >= 0) return true;

    return false;
}

void Session::initResumeFolder()
{
    m_resumeFolderPath = Utils::Fs::expandPathAbs(Utils::Fs::QDesktopServicesDataLocation() + RESUME_FOLDER);
    QDir resumeFolderDir(m_resumeFolderPath);
    if (resumeFolderDir.exists() || resumeFolderDir.mkpath(resumeFolderDir.absolutePath())) {
        m_resumeFolderLock.setFileName(resumeFolderDir.absoluteFilePath("session.lock"));
        if (!m_resumeFolderLock.open(QFile::WriteOnly)) {
            throw std::runtime_error("Cannot write to torrent resume folder.");
        }
    }
    else {
        throw std::runtime_error("Cannot create torrent resume folder.");
    }
}

void Session::configureDeferred()
{
    if (m_deferredConfigureScheduled) return; // Obtaining the lock is expensive, let's check early
    QWriteLocker locker(&m_lock);
    if (m_deferredConfigureScheduled) return; // something might have changed while we were getting the lock

    QMetaObject::invokeMethod(this, "configure", Qt::QueuedConnection);
    m_deferredConfigureScheduled = true;
}

// Enable IP Filtering
void Session::enableIPFilter()
{
    qDebug("Enabling IPFilter");
    if (!m_filterParser) {
        m_filterParser = new FilterParserThread(m_nativeSession, this);
        connect(m_filterParser.data(), SIGNAL(IPFilterParsed(int)), SLOT(handleIPFilterParsed(int)));
        connect(m_filterParser.data(), SIGNAL(IPFilterError()), SLOT(handleIPFilterError()));
    }

    m_filterParser->processFilterFile(IPFilterFile());
}

// Disable IP Filtering
void Session::disableIPFilter()
{
    qDebug("Disabling IPFilter");
    m_nativeSession->set_ip_filter(libt::ip_filter());
    if (m_filterParser) {
        disconnect(m_filterParser.data(), 0, this, 0);
        delete m_filterParser;
    }

    // Add the banned IPs after the IPFilter disabling
    // which creates an empty filter and overrides all previously
    // applied bans.
    processBannedIPs();
}

void Session::recursiveTorrentDownload(const InfoHash &hash)
{
    TorrentHandle *const torrent = m_torrents.value(hash);
    if (!torrent) return;

    for (int i = 0; i < torrent->filesCount(); ++i) {
        const QString torrentRelpath = torrent->filePath(i);
        if (torrentRelpath.endsWith(".torrent")) {
            Logger::instance()->addMessage(
                        tr("Recursive download of file '%1' embedded in torrent '%2'"
                           , "Recursive download of 'test.torrent' embedded in torrent 'test2'")
                        .arg(Utils::Fs::toNativePath(torrentRelpath)).arg(torrent->name()));
            const QString torrentFullpath = torrent->savePath() + "/" + torrentRelpath;

            AddTorrentParams params;
            // Passing the save path along to the sub torrent file
            params.savePath = torrent->savePath();
            addTorrent(TorrentInfo::loadFromFile(torrentFullpath), params);
        }
    }
}

SessionStatus Session::status() const
{
    return m_nativeSession->status();
}

CacheStatus Session::cacheStatus() const
{
    return m_nativeSession->get_cache_status();
}

// Will resume torrents in backup directory
void Session::startUpTorrents()
{
    qDebug("Resuming torrents...");

    const QDir resumeDataDir(m_resumeFolderPath);
    QStringList fastresumes = resumeDataDir.entryList(
                QStringList(QLatin1String("*.fastresume")), QDir::Files, QDir::Unsorted);

    Logger *const logger = Logger::instance();

    typedef struct
    {
        QString hash;
        MagnetUri magnetUri;
        AddTorrentData addTorrentData;
        QByteArray data;
    } TorrentResumeData;

    auto startupTorrent = [this, logger, resumeDataDir](const TorrentResumeData &params)
    {
        QString filePath = resumeDataDir.filePath(QString("%1.torrent").arg(params.hash));
        qDebug() << "Starting up torrent" << params.hash << "...";
        if (!addTorrent_impl(params.addTorrentData, params.magnetUri, TorrentInfo::loadFromFile(filePath), params.data))
            logger->addMessage(tr("Unable to resume torrent '%1'.", "e.g: Unable to resume torrent 'hash'.")
                               .arg(params.hash), Log::CRITICAL);
    };

    qDebug("Starting up torrents");
    qDebug("Queue size: %d", fastresumes.size());
    // Resume downloads
    QMap<int, TorrentResumeData> queuedResumeData;
    int nextQueuePosition = 1;
    QRegExp rx(QLatin1String("^([A-Fa-f0-9]{40})\\.fastresume$"));
    foreach (const QString &fastresumeName, fastresumes) {
        if (rx.indexIn(fastresumeName) == -1) continue;

        QString hash = rx.cap(1);
        QString fastresumePath = resumeDataDir.absoluteFilePath(fastresumeName);
        QByteArray data;
        AddTorrentData resumeData;
        MagnetUri magnetUri;
        int queuePosition;
        if (readFile(fastresumePath, data) && loadTorrentResumeData(data, resumeData, queuePosition, magnetUri)) {
            if (queuePosition <= nextQueuePosition) {
                startupTorrent({ hash, magnetUri, resumeData, data });

                if (queuePosition == nextQueuePosition) {
                    ++nextQueuePosition;
                    while (queuedResumeData.contains(nextQueuePosition)) {
                        startupTorrent(queuedResumeData.take(nextQueuePosition));
                        ++nextQueuePosition;
                    }
                }
            }
            else {
                queuedResumeData[queuePosition] = { hash, magnetUri, resumeData, data };
            }
        }
    }

    // starting up downloading torrents (queue position > 0)
    foreach (const TorrentResumeData &torrentResumeData, queuedResumeData)
        startupTorrent(torrentResumeData);
}

quint64 Session::getAlltimeDL() const
{
    return m_statistics->getAlltimeDL();
}

quint64 Session::getAlltimeUL() const
{
    return m_statistics->getAlltimeUL();
}

void Session::refresh()
{
    m_nativeSession->post_torrent_updates();
}

void Session::handleIPFilterParsed(int ruleCount)
{
    Logger::instance()->addMessage(tr("Successfully parsed the provided IP filter: %1 rules were applied.", "%1 is a number").arg(ruleCount));
    emit IPFilterParsed(false, ruleCount);
}

void Session::handleIPFilterError()
{
    Logger::instance()->addMessage(tr("Error: Failed to parse the provided IP filter."), Log::CRITICAL);
    emit IPFilterParsed(true, 0);
}

#if LIBTORRENT_VERSION_NUM < 10100
void Session::dispatchAlerts(libt::alert *alertPtr)
{
    QMutexLocker lock(&m_alertsMutex);

    bool wasEmpty = m_alerts.empty();

    m_alerts.push_back(alertPtr);

    if (wasEmpty) {
        m_alertsWaitCondition.wakeAll();
        QMetaObject::invokeMethod(this, "readAlerts", Qt::QueuedConnection);
    }
}
#endif

void Session::getPendingAlerts(std::vector<libt::alert *> &out, ulong time)
{
    Q_ASSERT(out.empty());

#if LIBTORRENT_VERSION_NUM < 10100
    QMutexLocker lock(&m_alertsMutex);

    if (m_alerts.empty())
        m_alertsWaitCondition.wait(&m_alertsMutex, time);

    m_alerts.swap(out);
#else
    if (time > 0)
        m_nativeSession->wait_for_alert(libt::milliseconds(time));
    m_nativeSession->pop_alerts(&out);
#endif
}

// Read alerts sent by the BitTorrent session
void Session::readAlerts()
{
    std::vector<libt::alert *> alerts;
    getPendingAlerts(alerts);

    for (const auto a: alerts) {
        handleAlert(a);
#if LIBTORRENT_VERSION_NUM < 10100
        delete a;
#endif
    }
}

void Session::handleAlert(libt::alert *a)
{
    try {
        switch (a->type()) {
        case libt::stats_alert::alert_type:
        case libt::file_renamed_alert::alert_type:
        case libt::file_completed_alert::alert_type:
        case libt::torrent_finished_alert::alert_type:
        case libt::save_resume_data_alert::alert_type:
        case libt::save_resume_data_failed_alert::alert_type:
        case libt::storage_moved_alert::alert_type:
        case libt::storage_moved_failed_alert::alert_type:
        case libt::torrent_paused_alert::alert_type:
        case libt::tracker_error_alert::alert_type:
        case libt::tracker_reply_alert::alert_type:
        case libt::tracker_warning_alert::alert_type:
        case libt::fastresume_rejected_alert::alert_type:
        case libt::torrent_checked_alert::alert_type:
            dispatchTorrentAlert(a);
            break;
        case libt::metadata_received_alert::alert_type:
            handleMetadataReceivedAlert(static_cast<libt::metadata_received_alert*>(a));
            dispatchTorrentAlert(a);
            break;
        case libt::state_update_alert::alert_type:
            handleStateUpdateAlert(static_cast<libt::state_update_alert*>(a));
            break;
        case libt::file_error_alert::alert_type:
            handleFileErrorAlert(static_cast<libt::file_error_alert*>(a));
            break;
        case libt::add_torrent_alert::alert_type:
            handleAddTorrentAlert(static_cast<libt::add_torrent_alert*>(a));
            break;
        case libt::torrent_removed_alert::alert_type:
            handleTorrentRemovedAlert(static_cast<libt::torrent_removed_alert*>(a));
            break;
        case libt::torrent_deleted_alert::alert_type:
            handleTorrentDeletedAlert(static_cast<libt::torrent_deleted_alert*>(a));
            break;
        case libt::torrent_delete_failed_alert::alert_type:
            handleTorrentDeleteFailedAlert(static_cast<libt::torrent_delete_failed_alert*>(a));
            break;
        case libt::portmap_error_alert::alert_type:
            handlePortmapWarningAlert(static_cast<libt::portmap_error_alert*>(a));
            break;
        case libt::portmap_alert::alert_type:
            handlePortmapAlert(static_cast<libt::portmap_alert*>(a));
            break;
        case libt::peer_blocked_alert::alert_type:
            handlePeerBlockedAlert(static_cast<libt::peer_blocked_alert*>(a));
            break;
        case libt::peer_ban_alert::alert_type:
            handlePeerBanAlert(static_cast<libt::peer_ban_alert*>(a));
            break;
        case libt::url_seed_alert::alert_type:
            handleUrlSeedAlert(static_cast<libt::url_seed_alert*>(a));
            break;
        case libt::listen_succeeded_alert::alert_type:
            handleListenSucceededAlert(static_cast<libt::listen_succeeded_alert*>(a));
            break;
        case libt::listen_failed_alert::alert_type:
            handleListenFailedAlert(static_cast<libt::listen_failed_alert*>(a));
            break;
        case libt::external_ip_alert::alert_type:
            handleExternalIPAlert(static_cast<libt::external_ip_alert*>(a));
            break;
        }
    }
    catch (std::exception &exc) {
        qWarning() << "Caught exception in " << Q_FUNC_INFO << ": " << Utils::String::fromStdString(exc.what());
    }
}

void Session::dispatchTorrentAlert(libt::alert *a)
{
    TorrentHandle *const torrent = m_torrents.value(static_cast<libt::torrent_alert*>(a)->handle.info_hash());
    if (torrent)
        torrent->handleAlert(a);
}

void Session::createTorrentHandle(const libt::torrent_handle &nativeHandle)
{
    // Magnet added for preload its metadata
    if (!m_addingTorrents.contains(nativeHandle.info_hash())) return;

    AddTorrentData data = m_addingTorrents.take(nativeHandle.info_hash());

    TorrentHandle *const torrent = new TorrentHandle(this, nativeHandle, data);
    m_torrents.insert(torrent->hash(), torrent);

    Logger *const logger = Logger::instance();

    bool fromMagnetUri = !torrent->hasMetadata();

    if (data.resumed) {
        if (fromMagnetUri && !data.addPaused)
            torrent->resume(data.addForced);

        logger->addMessage(tr("'%1' resumed. (fast resume)", "'torrent name' was resumed. (fast resume)")
                           .arg(torrent->name()));
    }
    else {
        qDebug("This is a NEW torrent (first time)...");

        // The following is useless for newly added magnet
        if (!fromMagnetUri) {
            // Backup torrent file
            const QDir resumeDataDir(m_resumeFolderPath);
            const QString newFile = resumeDataDir.absoluteFilePath(QString("%1.torrent").arg(torrent->hash()));
            if (torrent->saveTorrentFile(newFile)) {
                // Copy the torrent file to the export folder
                if (!torrentExportDirectory().isEmpty())
                    exportTorrentFile(torrent);
            }
            else {
                logger->addMessage(tr("Couldn't save '%1.torrent'").arg(torrent->hash()), Log::CRITICAL);
            }
        }

        if (isAddTrackersEnabled() && !torrent->isPrivate())
            torrent->addTrackers(m_additionalTrackerList);

        bool addPaused = data.addPaused;
        if (data.addPaused == TriStateBool::Undefined)
            addPaused = isAddTorrentPaused();

        // Start torrent because it was added in paused state
        if (!addPaused)
            torrent->resume();
        logger->addMessage(tr("'%1' added to download list.", "'torrent name' was added to download list.")
                           .arg(torrent->name()));

        // In case of crash before the scheduled generation
        // of the fastresumes.
        saveTorrentResumeData(torrent);
    }

    if ((torrent->ratioLimit() >= 0) && !m_bigRatioTimer->isActive())
        m_bigRatioTimer->start();

    // Send torrent addition signal
    emit torrentAdded(torrent);
    // Send new torrent signal
    if (!data.resumed)
        emit torrentNew(torrent);
}

void Session::handleAddTorrentAlert(libt::add_torrent_alert *p)
{
    if (p->error) {
        qDebug("/!\\ Error: Failed to add torrent!");
        QString msg = Utils::String::fromStdString(p->message());
        Logger::instance()->addMessage(tr("Couldn't add torrent. Reason: %1").arg(msg), Log::WARNING);
        emit addTorrentFailed(msg);
    }
    else {
        createTorrentHandle(p->handle);
    }
}

void Session::handleTorrentRemovedAlert(libt::torrent_removed_alert *p)
{
    if (m_loadedMetadata.contains(p->info_hash))
        emit metadataLoaded(m_loadedMetadata.take(p->info_hash));
}

void Session::handleTorrentDeletedAlert(libt::torrent_deleted_alert *p)
{
    m_savePathsToRemove.remove(p->info_hash);
}

void Session::handleTorrentDeleteFailedAlert(libt::torrent_delete_failed_alert *p)
{
    // libtorrent won't delete the directory if it contains files not listed in the torrent,
    // so we remove the directory ourselves
    if (m_savePathsToRemove.contains(p->info_hash)) {
        QString path = m_savePathsToRemove.take(p->info_hash);
        Utils::Fs::smartRemoveEmptyFolderTree(path);
    }
}

void Session::handleMetadataReceivedAlert(libt::metadata_received_alert *p)
{
    InfoHash hash = p->handle.info_hash();

    if (m_loadedMetadata.contains(hash)) {
        --m_extraLimit;
        adjustLimits();
        m_loadedMetadata[hash] = TorrentInfo(p->handle.torrent_file());
        m_nativeSession->remove_torrent(p->handle, libt::session::delete_files);
    }
}

void Session::handleFileErrorAlert(libt::file_error_alert *p)
{
    qDebug() << Q_FUNC_INFO;
    // NOTE: Check this function!
    TorrentHandle *const torrent = m_torrents.value(p->handle.info_hash());
    if (torrent) {
        QString msg = Utils::String::fromStdString(p->message());
        Logger::instance()->addMessage(tr("An I/O error occurred, '%1' paused. %2")
                           .arg(torrent->name()).arg(msg));
        emit fullDiskError(torrent, msg);
    }
}

void Session::handlePortmapWarningAlert(libt::portmap_error_alert *p)
{
    Logger::instance()->addMessage(tr("UPnP/NAT-PMP: Port mapping failure, message: %1").arg(Utils::String::fromStdString(p->message())), Log::CRITICAL);
}

void Session::handlePortmapAlert(libt::portmap_alert *p)
{
    qDebug("UPnP Success, msg: %s", p->message().c_str());
    Logger::instance()->addMessage(tr("UPnP/NAT-PMP: Port mapping successful, message: %1").arg(Utils::String::fromStdString(p->message())), Log::INFO);
}

void Session::handlePeerBlockedAlert(libt::peer_blocked_alert *p)
{
    boost::system::error_code ec;
    std::string ip = p->ip.to_string(ec);
    QString reason;
    switch (p->reason) {
    case libt::peer_blocked_alert::ip_filter:
        reason = tr("due to IP filter.", "this peer was blocked due to ip filter.");
        break;
    case libt::peer_blocked_alert::port_filter:
        reason = tr("due to port filter.", "this peer was blocked due to port filter.");
        break;
    case libt::peer_blocked_alert::i2p_mixed:
        reason = tr("due to i2p mixed mode restrictions.", "this peer was blocked due to i2p mixed mode restrictions.");
        break;
    case libt::peer_blocked_alert::privileged_ports:
        reason = tr("because it has a low port.", "this peer was blocked because it has a low port.");
        break;
    case libt::peer_blocked_alert::utp_disabled:
        reason = trUtf8("because %1 is disabled.", "this peer was blocked because uTP is disabled.").arg(QString::fromUtf8(C_UTP)); // don't translate μTP
        break;
    case libt::peer_blocked_alert::tcp_disabled:
        reason = tr("because %1 is disabled.", "this peer was blocked because TCP is disabled.").arg("TCP"); // don't translate TCP
        break;
    }

    if (!ec)
        Logger::instance()->addPeer(QString::fromLatin1(ip.c_str()), true, reason);
}

void Session::handlePeerBanAlert(libt::peer_ban_alert *p)
{
    boost::system::error_code ec;
    std::string ip = p->ip.address().to_string(ec);
    if (!ec)
        Logger::instance()->addPeer(QString::fromLatin1(ip.c_str()), false);
}

void Session::handleUrlSeedAlert(libt::url_seed_alert *p)
{
    Logger::instance()->addMessage(tr("URL seed lookup failed for URL: '%1', message: %2").arg(Utils::String::fromStdString(p->url)).arg(Utils::String::fromStdString(p->message())), Log::CRITICAL);
}

void Session::handleListenSucceededAlert(libt::listen_succeeded_alert *p)
{
    boost::system::error_code ec;
    QString proto = "TCP";
    if (p->sock_type == libt::listen_succeeded_alert::udp)
        proto = "UDP";
    else if (p->sock_type == libt::listen_succeeded_alert::tcp)
        proto = "TCP";
    else if (p->sock_type == libt::listen_succeeded_alert::tcp_ssl)
        proto = "TCP_SSL";
    qDebug() << "Successfully listening on " << proto << p->endpoint.address().to_string(ec).c_str() << "/" << p->endpoint.port();
    Logger::instance()->addMessage(tr("qBittorrent is successfully listening on interface %1 port: %2/%3", "e.g: qBittorrent is successfully listening on interface 192.168.0.1 port: TCP/6881").arg(p->endpoint.address().to_string(ec).c_str()).arg(proto).arg(QString::number(p->endpoint.port())), Log::INFO);

    // Force reannounce on all torrents because some trackers blacklist some ports
    std::vector<libt::torrent_handle> torrents = m_nativeSession->get_torrents();
    std::vector<libt::torrent_handle>::iterator it = torrents.begin();
    std::vector<libt::torrent_handle>::iterator itend = torrents.end();
    for ( ; it != itend; ++it)
        it->force_reannounce();
}

void Session::handleListenFailedAlert(libt::listen_failed_alert *p)
{
    boost::system::error_code ec;
    QString proto = "TCP";
    if (p->sock_type == libt::listen_failed_alert::udp)
        proto = "UDP";
    else if (p->sock_type == libt::listen_failed_alert::tcp)
        proto = "TCP";
    else if (p->sock_type == libt::listen_failed_alert::tcp_ssl)
        proto = "TCP_SSL";
    else if (p->sock_type == libt::listen_failed_alert::i2p)
        proto = "I2P";
    else if (p->sock_type == libt::listen_failed_alert::socks5)
        proto = "SOCKS5";
    qDebug() << "Failed listening on " << proto << p->endpoint.address().to_string(ec).c_str() << "/" << p->endpoint.port();
    Logger::instance()->addMessage(
                tr("qBittorrent failed listening on interface %1 port: %2/%3. Reason: %4.",
                   "e.g: qBittorrent failed listening on interface 192.168.0.1 port: TCP/6881. Reason: already in use.")
                .arg(p->endpoint.address().to_string(ec).c_str()).arg(proto).arg(QString::number(p->endpoint.port()))
                .arg(QString::fromLocal8Bit(p->error.message().c_str())), Log::CRITICAL);
}

void Session::handleExternalIPAlert(libt::external_ip_alert *p)
{
    boost::system::error_code ec;
    Logger::instance()->addMessage(tr("External IP: %1", "e.g. External IP: 192.168.0.1").arg(p->external_address.to_string(ec).c_str()), Log::INFO);
}

void Session::handleStateUpdateAlert(libt::state_update_alert *p)
{
    foreach (const libt::torrent_status &status, p->status) {
        TorrentHandle *const torrent = m_torrents.value(status.info_hash);
        if (torrent)
            torrent->handleStateUpdate(status);
    }

    m_torrentStatusReport = TorrentStatusReport();
    foreach (TorrentHandle *const torrent, m_torrents) {
        if (torrent->isDownloading())
            ++m_torrentStatusReport.nbDownloading;
        if (torrent->isUploading())
            ++m_torrentStatusReport.nbSeeding;
        if (torrent->isCompleted())
            ++m_torrentStatusReport.nbCompleted;
        if (torrent->isPaused())
            ++m_torrentStatusReport.nbPaused;
        if (torrent->isResumed())
            ++m_torrentStatusReport.nbResumed;
        if (torrent->isActive())
            ++m_torrentStatusReport.nbActive;
        if (torrent->isInactive())
            ++m_torrentStatusReport.nbInactive;
        if (torrent->isErrored())
            ++m_torrentStatusReport.nbErrored;
    }

    emit torrentsUpdated();
}

namespace
{
    bool readFile(const QString &path, QByteArray &buf)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug("Cannot read file %s: %s", qPrintable(path), qPrintable(file.errorString()));
            return false;
        }

        buf = file.readAll();
        return true;
    }

    bool loadTorrentResumeData(const QByteArray &data, AddTorrentData &torrentData, int &prio,  MagnetUri &magnetUri)
    {
        torrentData = AddTorrentData();
        torrentData.resumed = true;
        torrentData.skipChecking = false;

        libt::error_code ec;
#if LIBTORRENT_VERSION_NUM < 10100
        libt::lazy_entry fast;
        libt::lazy_bdecode(data.constData(), data.constData() + data.size(), fast, ec);
        if (ec || (fast.type() != libt::lazy_entry::dict_t)) return false;
#else
        libt::bdecode_node fast;
        libt::bdecode(data.constData(), data.constData() + data.size(), fast, ec);
        if (ec || (fast.type() != libt::bdecode_node::dict_t)) return false;
#endif

        torrentData.savePath = Utils::Fs::fromNativePath(Utils::String::fromStdString(fast.dict_find_string_value("qBt-savePath")));
        torrentData.ratioLimit = Utils::String::fromStdString(fast.dict_find_string_value("qBt-ratioLimit")).toDouble();
        // **************************************************************************************
        // Workaround to convert legacy label to category
        // TODO: Should be removed in future
        torrentData.category = Utils::String::fromStdString(fast.dict_find_string_value("qBt-label"));
        if (torrentData.category.isEmpty())
        // **************************************************************************************
            torrentData.category = Utils::String::fromStdString(fast.dict_find_string_value("qBt-category"));
        torrentData.name = Utils::String::fromStdString(fast.dict_find_string_value("qBt-name"));
        torrentData.hasSeedStatus = fast.dict_find_int_value("qBt-seedStatus");
        torrentData.disableTempPath = fast.dict_find_int_value("qBt-tempPathDisabled");

        magnetUri = MagnetUri(Utils::String::fromStdString(fast.dict_find_string_value("qBt-magnetUri")));
        torrentData.addPaused = fast.dict_find_int_value("qBt-paused");
        torrentData.addForced = fast.dict_find_int_value("qBt-forced");

        prio = fast.dict_find_int_value("qBt-queuePosition");

        return true;
    }

    void torrentQueuePositionUp(const libt::torrent_handle &handle)
    {
        try {
            handle.queue_position_up();
        }
        catch (std::exception &exc) {
            qDebug() << Q_FUNC_INFO << " fails: " << exc.what();
        }
    }

    void torrentQueuePositionDown(const libt::torrent_handle &handle)
    {
        try {
            handle.queue_position_down();
        }
        catch (std::exception &exc) {
            qDebug() << Q_FUNC_INFO << " fails: " << exc.what();
        }
    }

    void torrentQueuePositionTop(const libt::torrent_handle &handle)
    {
        try {
            handle.queue_position_top();
        }
        catch (std::exception &exc) {
            qDebug() << Q_FUNC_INFO << " fails: " << exc.what();
        }
    }

    void torrentQueuePositionBottom(const libt::torrent_handle &handle)
    {
        try {
            handle.queue_position_bottom();
        }
        catch (std::exception &exc) {
            qDebug() << Q_FUNC_INFO << " fails: " << exc.what();
        }
    }
}

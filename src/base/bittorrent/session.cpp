
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
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <queue>
#include <vector>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/lt_trackers.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/identify_client.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>

#include "base/logger.h"
#include "base/net/downloadhandler.h"
#include "base/net/downloadmanager.h"
#include "base/net/portforwarder.h"
#include "base/preferences.h"
#include "base/settingsstorage.h"
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

namespace libt = libtorrent;
using namespace BitTorrent;

#define SETTINGS_KEY(name) "BitTorrent/Session/" name
const QString KEY_CATEGORIES = SETTINGS_KEY("Categories");
const QString KEY_MAXRATIOACTION = SETTINGS_KEY("MaxRatioAction");
const QString KEY_DEFAULTSAVEPATH = SETTINGS_KEY("DefaultSavePath");
const QString KEY_TEMPPATH = SETTINGS_KEY("TempPath");
const QString KEY_SUBCATEGORIESENABLED = SETTINGS_KEY("SubcategoriesEnabled");
const QString KEY_TEMPPATHENABLED = SETTINGS_KEY("TempPathEnabled");
const QString KEY_DISABLE_AUTOTMM_BYDEFAULT = SETTINGS_KEY("DisableAutoTMMByDefault");
const QString KEY_DISABLE_AUTOTMM_ONCATEGORYCHANGED = SETTINGS_KEY("DisableAutoTMMTriggers/CategoryChanged");
const QString KEY_DISABLE_AUTOTMM_ONDEFAULTSAVEPATHCHANGED = SETTINGS_KEY("DisableAutoTMMTriggers/DefaultSavePathChanged");
const QString KEY_DISABLE_AUTOTMM_ONCATEGORYSAVEPATHCHANGED = SETTINGS_KEY("DisableAutoTMMTriggers/CategorySavePathChanged");
const QString KEY_ADDTORRENTPAUSED = SETTINGS_KEY("AddTorrentPaused");

namespace
{
    bool readFile(const QString &path, QByteArray &buf);
    bool loadTorrentResumeData(const QByteArray &data, AddTorrentData &torrentData, int &prio, MagnetUri &magnetUri);

    void torrentQueuePositionUp(const libt::torrent_handle &handle);
    void torrentQueuePositionDown(const libt::torrent_handle &handle);
    void torrentQueuePositionTop(const libt::torrent_handle &handle);
    void torrentQueuePositionBottom(const libt::torrent_handle &handle);

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

    QString normalizeSavePath(QString path, const QString &defaultPath = Utils::Fs::QDesktopServicesDownloadLocation())
    {
        path = Utils::Fs::fromNativePath(path.trimmed());
        if (path.isEmpty())
            path = Utils::Fs::fromNativePath(defaultPath.trimmed());
        if (!path.isEmpty() && !path.endsWith('/'))
            path += '/';

        return path;
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
}

// Session

Session *Session::m_instance = nullptr;

Session::Session(QObject *parent)
    : QObject(parent)
    , m_settings(SettingsStorage::instance())
    , m_LSDEnabled(false)
    , m_DHTEnabled(false)
    , m_PeXEnabled(false)
    , m_queueingEnabled(false)
    , m_torrentExportEnabled(false)
    , m_finishedTorrentExportEnabled(false)
    , m_preAllocateAll(false)
    , m_globalMaxRatio(-1)
    , m_numResumeData(0)
    , m_extraLimit(0)
    , m_appendExtension(false)
    , m_refreshInterval(0)
{
    Preferences* const pref = Preferences::instance();
    Logger* const logger = Logger::instance();

    initResumeFolder();

    m_bigRatioTimer = new QTimer(this);
    m_bigRatioTimer->setInterval(10000);
    connect(m_bigRatioTimer, SIGNAL(timeout()), SLOT(processBigRatios()));

    // Creating BitTorrent session

    // Construct session
    libt::fingerprint fingerprint(PEER_ID, VERSION_MAJOR, VERSION_MINOR, VERSION_BUGFIX, VERSION_BUILD);
    const unsigned short port = pref->getSessionPort();
    std::pair<int,int> ports(port, port);
    const QString ip = getListeningIPs().first();
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

    if (ip.isEmpty()) {
        logger->addMessage(tr("qBittorrent is trying to listen on any interface port: %1", "e.g: qBittorrent is trying to listen on any interface port: TCP/6881").arg(QString::number(port)), Log::INFO);
        m_nativeSession = new libt::session(fingerprint, ports, 0, 0, alertMask);
    }
    else {
        logger->addMessage(tr("qBittorrent is trying to listen on interface %1 port: %2", "e.g: qBittorrent is trying to listen on interface 192.168.0.1 port: TCP/6881").arg(ip).arg(port), Log::INFO);
        m_nativeSession = new libt::session(fingerprint, ports, ip.toLatin1().constData(), 0, alertMask);
    }

    logger->addMessage(tr("Peer ID: ") + Utils::String::fromStdString(fingerprint.to_string()));

#if LIBTORRENT_VERSION_NUM < 10100
    m_nativeSession->set_alert_dispatch([this](std::auto_ptr<libt::alert> alertPtr)
    {
        dispatchAlerts(alertPtr);
    });
#else
    m_nativeSession->set_alert_notify([this]()
    {
        QMetaObject::invokeMethod(this, "readAlerts", Qt::QueuedConnection);
    });
#endif

    // Enabling plugins
    //m_nativeSession->add_extension(&libt::create_metadata_plugin);
    m_nativeSession->add_extension(&libt::create_ut_metadata_plugin);
    if (pref->trackerExchangeEnabled())
        m_nativeSession->add_extension(&libt::create_lt_trackers_plugin);
    m_PeXEnabled = pref->isPeXEnabled();
    if (m_PeXEnabled)
        m_nativeSession->add_extension(&libt::create_ut_pex_plugin);
    m_nativeSession->add_extension(&libt::create_smart_ban_plugin);

    m_categories = map_cast(m_settings->loadValue(KEY_CATEGORIES).toMap());
    if (isSubcategoriesEnabled()) {
        // if subcategories support changed manually
        m_categories = expandCategories(m_categories);
        m_settings->storeValue(KEY_CATEGORIES, map_cast(m_categories));
    }

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, SIGNAL(timeout()), SLOT(refresh()));
    m_refreshTimer->start();

    // Regular saving of fastresume data
    m_resumeDataTimer = new QTimer(this);
    connect(m_resumeDataTimer, SIGNAL(timeout()), SLOT(generateResumeData()));

    m_statistics = new Statistics(this);

    m_maxRatioAction = static_cast<MaxRatioAction>(m_settings->loadValue(KEY_MAXRATIOACTION, Pause).toInt());
    m_defaultSavePath = normalizeSavePath(m_settings->loadValue(KEY_DEFAULTSAVEPATH).toString());
    m_tempPath = normalizeSavePath(m_settings->loadValue(KEY_TEMPPATH).toString(), m_defaultSavePath + "temp");

    // Apply user settings to BitTorrent session
    configure();
    connect(pref, SIGNAL(changed()), SLOT(configure()));

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
    return m_DHTEnabled;
}

bool Session::isLSDEnabled() const
{
    return m_LSDEnabled;
}

bool Session::isPexEnabled() const
{
    return m_PeXEnabled;
}

bool Session::isQueueingEnabled() const
{
    return m_queueingEnabled;
}

bool Session::isTempPathEnabled() const
{
    return m_settings->loadValue(KEY_TEMPPATHENABLED, false).toBool();
}

void Session::setTempPathEnabled(bool enabled)
{
    m_settings->storeValue(KEY_TEMPPATHENABLED, enabled);
    foreach (TorrentHandle *const torrent, m_torrents)
        torrent->handleTempPathChanged();
}

bool Session::isAppendExtensionEnabled() const
{
    return m_appendExtension;
}

QString Session::defaultSavePath() const
{
    return m_defaultSavePath;
}

QString Session::tempPath() const
{
    return m_tempPath;
}

QString Session::torrentTempPath(const InfoHash &hash) const
{
    return m_tempPath
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
    m_settings->storeValue(KEY_CATEGORIES, map_cast(m_categories));
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
        m_settings->storeValue(KEY_CATEGORIES, map_cast(m_categories));
        emit categoryRemoved(name);
    }

    return result;
}

bool Session::isSubcategoriesEnabled() const
{
    return m_settings->loadValue(KEY_SUBCATEGORIESENABLED, false).toBool();
}

void Session::setSubcategoriesEnabled(bool value)
{
    if (isSubcategoriesEnabled() == value) return;

    if (value) {
        // expand categories to include all parent categories
        m_categories = expandCategories(m_categories);
        // update stored categories
        m_settings->storeValue(KEY_CATEGORIES, map_cast(m_categories));
    }
    else {
        // reload categories
        m_categories = map_cast(m_settings->loadValue(KEY_CATEGORIES).toMap());
    }

    m_settings->storeValue(KEY_SUBCATEGORIESENABLED, value);
    emit subcategoriesSupportChanged();
}

bool Session::isAutoTMMDisabledByDefault() const
{
    return m_settings->loadValue(KEY_DISABLE_AUTOTMM_BYDEFAULT, true).toBool();
}

void Session::setAutoTMMDisabledByDefault(bool value)
{
    m_settings->storeValue(KEY_DISABLE_AUTOTMM_BYDEFAULT, value);
}

bool Session::isDisableAutoTMMWhenCategoryChanged() const
{
    return m_settings->loadValue(KEY_DISABLE_AUTOTMM_ONCATEGORYCHANGED, false).toBool();
}

void Session::setDisableAutoTMMWhenCategoryChanged(bool value)
{
    m_settings->storeValue(KEY_DISABLE_AUTOTMM_ONCATEGORYCHANGED, value);
}

bool Session::isDisableAutoTMMWhenDefaultSavePathChanged() const
{
    return m_settings->loadValue(KEY_DISABLE_AUTOTMM_ONDEFAULTSAVEPATHCHANGED, true).toBool();
}

void Session::setDisableAutoTMMWhenDefaultSavePathChanged(bool value)
{
    m_settings->storeValue(KEY_DISABLE_AUTOTMM_ONDEFAULTSAVEPATHCHANGED, value);
}

bool Session::isDisableAutoTMMWhenCategorySavePathChanged() const
{
    return m_settings->loadValue(KEY_DISABLE_AUTOTMM_ONCATEGORYSAVEPATHCHANGED, true).toBool();
}

void Session::setDisableAutoTMMWhenCategorySavePathChanged(bool value)
{
    m_settings->storeValue(KEY_DISABLE_AUTOTMM_ONCATEGORYSAVEPATHCHANGED, value);
}

bool Session::isAddTorrentPaused() const
{
    return m_settings->loadValue(KEY_ADDTORRENTPAUSED, false).toBool();
}

void Session::setAddTorrentPaused(bool value)
{
    m_settings->storeValue(KEY_ADDTORRENTPAUSED, value);
}

qreal Session::globalMaxRatio() const
{
    return m_globalMaxRatio;
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
    if (!m_instance)
        m_instance = new Session;
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

void Session::setSessionSettings()
{
    Preferences* const pref = Preferences::instance();
    Logger* const logger = Logger::instance();

    libt::session_settings sessionSettings = m_nativeSession->settings();
    sessionSettings.user_agent = "qBittorrent " VERSION;
    //std::cout << "HTTP User-Agent is " << sessionSettings.user_agent << std::endl;
    logger->addMessage(tr("HTTP User-Agent is '%1'").arg(Utils::String::fromStdString(sessionSettings.user_agent)));

    sessionSettings.upnp_ignore_nonrouters = true;
    sessionSettings.use_dht_as_fallback = false;
    // Disable support for SSL torrents for now
    sessionSettings.ssl_listen = 0;
    // To prevent ISPs from blocking seeding
    sessionSettings.lazy_bitfields = true;
    // Speed up exit
    sessionSettings.stop_tracker_timeout = 1;
    sessionSettings.auto_scrape_interval = 1200; // 20 minutes
    bool announce_to_all = pref->announceToAllTrackers();
    sessionSettings.announce_to_all_trackers = announce_to_all;
    sessionSettings.announce_to_all_tiers = announce_to_all;
    sessionSettings.auto_scrape_min_interval = 900; // 15 minutes
    int cache_size = pref->diskCacheSize();
    sessionSettings.cache_size = cache_size ? cache_size * 64 : -1;
    sessionSettings.cache_expiry = pref->diskCacheTTL();
    qDebug() << "Using a disk cache size of" << cache_size << "MiB";
    libt::session_settings::io_buffer_mode_t mode = pref->osCache() ? libt::session_settings::enable_os_cache : libt::session_settings::disable_os_cache;
    sessionSettings.disk_io_read_mode = mode;
    sessionSettings.disk_io_write_mode = mode;

    m_resumeDataTimer->setInterval(pref->saveResumeDataInterval() * 60 * 1000);

    sessionSettings.anonymous_mode = pref->isAnonymousModeEnabled();
    if (sessionSettings.anonymous_mode)
        logger->addMessage(tr("Anonymous mode [ON]"), Log::INFO);
    else
        logger->addMessage(tr("Anonymous mode [OFF]"), Log::INFO);

    // Queueing System
    m_queueingEnabled = pref->isQueueingSystemEnabled();
    if (m_queueingEnabled) {
        adjustLimits(sessionSettings);

        sessionSettings.active_seeds = pref->getMaxActiveUploads();
        sessionSettings.dont_count_slow_torrents = pref->ignoreSlowTorrentsForQueueing();
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
#if LIBTORRENT_VERSION_NUM < 10100
    sessionSettings.outgoing_ports = std::make_pair(pref->outgoingPortsMin(), pref->outgoingPortsMax());
#else
    sessionSettings.outgoing_port = pref->outgoingPortsMin();
    sessionSettings.num_outgoing_ports = pref->outgoingPortsMax() - pref->outgoingPortsMin();
#endif
    // Ignore limits on LAN
    qDebug() << "Ignore limits on LAN" << pref->getIgnoreLimitsOnLAN();
    sessionSettings.ignore_limits_on_local_network = pref->getIgnoreLimitsOnLAN();
    // Include overhead in transfer limits
    sessionSettings.rate_limit_ip_overhead = pref->includeOverheadInLimits();
    // IP address to announce to trackers
    sessionSettings.announce_ip = Utils::String::toStdString(pref->getNetworkAddress());
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
        sessionSettings.mixed_mode_algorithm = libt::session_settings::prefer_tcp;
    else
        sessionSettings.mixed_mode_algorithm = libt::session_settings::peer_proportional;
    sessionSettings.connection_speed = 20; //default is 10
    if (pref->isProxyEnabled())
        sessionSettings.force_proxy = pref->getForceProxy();
    else
        sessionSettings.force_proxy = false;
    sessionSettings.no_connect_privileged_ports = false;
    sessionSettings.seed_choking_algorithm = libt::session_settings::fastest_upload;

    sessionSettings.apply_ip_filter_to_trackers = pref->isFilteringTrackerEnabled();
    qDebug() << "Set session settings";
    m_nativeSession->set_settings(sessionSettings);
}

void Session::adjustLimits()
{
    if (m_queueingEnabled) {
        libt::session_settings sessionSettings(m_nativeSession->settings());
        adjustLimits(sessionSettings);
        m_nativeSession->set_settings(sessionSettings);
    }
}

void Session::adjustLimits(libt::session_settings &sessionSettings)
{
    Preferences *const pref = Preferences::instance();

    //Internally increase the queue limits to ensure that the magnet is started
    int max_downloading = pref->getMaxActiveDownloads();
    int max_active = pref->getMaxActiveTorrents();

    if (max_downloading > -1)
        sessionSettings.active_downloads = max_downloading + m_extraLimit;
    else
        sessionSettings.active_downloads = max_downloading;

    if (max_active > -1)
        sessionSettings.active_limit = max_active + m_extraLimit;
    else
        sessionSettings.active_limit = max_active;
}

// Set BitTorrent session configuration
void Session::configure()
{
    qDebug("Configuring session");
    Preferences* const pref = Preferences::instance();

    const unsigned short oldListenPort = m_nativeSession->listen_port();
    const unsigned short newListenPort = pref->getSessionPort();
    if (oldListenPort != newListenPort) {
        qDebug("Session port changes in program preferences: %d -> %d", oldListenPort, newListenPort);
        setListeningPort();
    }

    uint newRefreshInterval = pref->getRefreshInterval();
    if (newRefreshInterval != m_refreshInterval) {
        m_refreshInterval = newRefreshInterval;
        m_refreshTimer->setInterval(m_refreshInterval);
    }

    setAppendExtension(pref->useIncompleteFilesExtension());
    preAllocateAllFiles(pref->preAllocateAllFiles());

    // * Torrent export directory
    const bool torrentExportEnabled = pref->isTorrentExportEnabled();
    if (m_torrentExportEnabled != torrentExportEnabled) {
        m_torrentExportEnabled = torrentExportEnabled;
        if (m_torrentExportEnabled) {
            qDebug("Torrent export is enabled, exporting the current torrents");
            for (auto torrent: m_torrents)
                exportTorrentFile(torrent);
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
    }
    else {
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
    }
    else {
        // Enabled
        setUploadRateLimit(up_limit*1024);
    }

    if (pref->isSchedulerEnabled()) {
        if (!m_bwScheduler) {
            m_bwScheduler = new BandwidthScheduler(this);
            connect(m_bwScheduler.data(), SIGNAL(switchToAlternativeMode(bool)), this, SLOT(switchToAlternativeMode(bool)));
        }
        m_bwScheduler->start();
    }
    else {
        delete m_bwScheduler;
    }

    Logger* const logger = Logger::instance();

    // * Session settings
    setSessionSettings();

    // Bittorrent
    // * Max connections per torrent limit
    setMaxConnectionsPerTorrent(pref->getMaxConnecsPerTorrent());
    // * Max uploads per torrent limit
    setMaxUploadsPerTorrent(pref->getMaxUploadsPerTorrent());
    // * DHT
    enableDHT(pref->isDHTEnabled());

    // * PeX
    if (m_PeXEnabled)
        logger->addMessage(tr("PeX support [ON]"), Log::INFO);
    else
        logger->addMessage(tr("PeX support [OFF]"), Log::CRITICAL);
    if (m_PeXEnabled != pref->isPeXEnabled())
        logger->addMessage(tr("Restart is required to toggle PeX support"), Log::CRITICAL);

    // * LSD
    if (pref->isLSDEnabled()) {
        enableLSD(true);
        logger->addMessage(tr("Local Peer Discovery support [ON]"), Log::INFO);
    }
    else {
        enableLSD(false);
        logger->addMessage(tr("Local Peer Discovery support [OFF]"), Log::INFO);
    }

    // * Encryption
    const int encryptionState = pref->getEncryptionSetting();
    // The most secure, rc4 only so that all streams and encrypted
    libt::pe_settings encryptionSettings;
    encryptionSettings.allowed_enc_level = libt::pe_settings::rc4;
    encryptionSettings.prefer_rc4 = true;
    switch(encryptionState) {
    case 0: //Enabled
        encryptionSettings.out_enc_policy = libt::pe_settings::enabled;
        encryptionSettings.in_enc_policy = libt::pe_settings::enabled;
        logger->addMessage(tr("Encryption support [ON]"), Log::INFO);
        break;
    case 1: // Forced
        encryptionSettings.out_enc_policy = libt::pe_settings::forced;
        encryptionSettings.in_enc_policy = libt::pe_settings::forced;
        logger->addMessage(tr("Encryption support [FORCED]"), Log::INFO);
        break;
    default: // Disabled
        encryptionSettings.out_enc_policy = libt::pe_settings::disabled;
        encryptionSettings.in_enc_policy = libt::pe_settings::disabled;
        logger->addMessage(tr("Encryption support [OFF]"), Log::INFO);
    }

    qDebug("Applying encryption settings");
    m_nativeSession->set_pe_settings(encryptionSettings);

    // * Add trackers
    m_additionalTrackers.clear();
    if (pref->isAddTrackersEnabled()) {
        foreach (QString tracker, pref->getTrackersList().split("\n")) {
            tracker = tracker.trimmed();
            if (!tracker.isEmpty())
                m_additionalTrackers << tracker;
        }
    }

    // * Maximum ratio
    setGlobalMaxRatio(pref->getGlobalMaxRatio());

    // Ip Filter
    if (pref->isFilteringEnabled())
        enableIPFilter(pref->getFilter());
    else
        disableIPFilter();
    // Add the banned IPs after the possibly disabled IPFilter
    // which creates an empty filter and overrides all previously
    // applied bans.
    FilterParserThread::processFilterList(m_nativeSession, pref->bannedIPs());

    // * Proxy settings
    libt::proxy_settings proxySettings;
    if (pref->isProxyEnabled()) {
        qDebug("Enabling P2P proxy");
        proxySettings.hostname = Utils::String::toStdString(pref->getProxyIp());
        qDebug("hostname is %s", proxySettings.hostname.c_str());
        proxySettings.port = pref->getProxyPort();
        qDebug("port is %d", proxySettings.port);
        if (pref->isProxyAuthEnabled()) {
            proxySettings.username = Utils::String::toStdString(pref->getProxyUsername());
            proxySettings.password = Utils::String::toStdString(pref->getProxyPassword());
            qDebug("username is %s", proxySettings.username.c_str());
            qDebug("password is %s", proxySettings.password.c_str());
        }
    }

    switch(pref->getProxyType()) {
    case Proxy::HTTP:
        qDebug("type: http");
        proxySettings.type = libt::proxy_settings::http;
        break;
    case Proxy::HTTP_PW:
        qDebug("type: http_pw");
        proxySettings.type = libt::proxy_settings::http_pw;
        break;
    case Proxy::SOCKS4:
        proxySettings.type = libt::proxy_settings::socks4;
        break;
    case Proxy::SOCKS5:
        qDebug("type: socks5");
        proxySettings.type = libt::proxy_settings::socks5;
        break;
    case Proxy::SOCKS5_PW:
        qDebug("type: socks5_pw");
        proxySettings.type = libt::proxy_settings::socks5_pw;
        break;
    default:
        proxySettings.type = libt::proxy_settings::none;
    }

    setProxySettings(proxySettings);

    // Tracker
    if (pref->isTrackerEnabled()) {
        if (!m_tracker)
            m_tracker = new Tracker(this);

        if (m_tracker->start())
            logger->addMessage(tr("Embedded Tracker [ON]"), Log::INFO);
        else
            logger->addMessage(tr("Failed to start the embedded tracker!"), Log::CRITICAL);
    }
    else {
        logger->addMessage(tr("Embedded Tracker [OFF]"));
        if (m_tracker)
            delete m_tracker;
    }

    qDebug("Session configured");
}

void Session::preAllocateAllFiles(bool b)
{
    const bool change = (m_preAllocateAll != b);
    if (change) {
        qDebug("PreAllocateAll changed, reloading all torrents!");
        m_preAllocateAll = b;
    }
}

void Session::processBigRatios()
{
    qDebug("Process big ratios...");

    foreach (TorrentHandle *const torrent, m_torrents) {
        if (torrent->isSeed() && (torrent->ratioLimit() != TorrentHandle::NO_RATIO_LIMIT)) {
            const qreal ratio = torrent->realRatio();
            qreal ratioLimit = torrent->ratioLimit();
            if (ratioLimit == TorrentHandle::USE_GLOBAL_RATIO) {
                // If Global Max Ratio is really set...
                if (m_globalMaxRatio >= 0)
                    ratioLimit = m_globalMaxRatio;
                else
                    continue;
            }
            qDebug("Ratio: %f (limit: %f)", ratio, ratioLimit);
            Q_ASSERT(ratioLimit >= 0.f);

            if ((ratio <= TorrentHandle::MAX_RATIO) && (ratio >= ratioLimit)) {
                Logger* const logger = Logger::instance();
                if (m_maxRatioAction == Remove) {
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

void Session::changeSpeedLimitMode(bool alternative)
{
    Preferences* const pref = Preferences::instance();
    // Stop the scheduler when the user has manually changed the bandwidth mode
    if (pref->isSchedulerEnabled()) {
        pref->setSchedulerEnabled(false);
        delete m_bwScheduler;
    }

    changeSpeedLimitMode_impl(alternative);
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
    FilterParserThread::processFilterList(m_nativeSession, QStringList(ip));
    Preferences::instance()->banIP(ip);
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
                ((!addData.resumed && isAutoTMMDisabledByDefault()) ? m_defaultSavePath : ""));

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
    if (m_preAllocateAll)
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
    Preferences *const pref = Preferences::instance();
    p.max_connections = pref->getMaxConnecsPerTorrent();
    p.max_uploads = pref->getMaxUploadsPerTorrent();
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
    if (m_preAllocateAll)
        p.storage_mode = libt::storage_mode_allocate;
    else
        p.storage_mode = libt::storage_mode_sparse;

    Preferences *const pref = Preferences::instance();
    // Limits
    p.max_connections = pref->getMaxConnecsPerTorrent();
    p.max_uploads = pref->getMaxUploadsPerTorrent();

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
    Q_ASSERT(((folder == TorrentExportFolder::Regular) && m_torrentExportEnabled) ||
             ((folder == TorrentExportFolder::Finished) && m_finishedTorrentExportEnabled));

    QString validName = Utils::Fs::toValidFileSystemName(torrent->name());
    QString torrentFilename = QString("%1.torrent").arg(torrent->hash());
    QString torrentExportFilename = QString("%1.torrent").arg(validName);
    QString torrentPath = QDir(m_resumeFolderPath).absoluteFilePath(torrentFilename);
    QDir exportPath(folder == TorrentExportFolder::Regular ? Preferences::instance()->getTorrentExportDir() : Preferences::instance()->getFinishedTorrentExportDir());
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

void Session::setMaxConnectionsPerTorrent(int max)
{
    qDebug() << Q_FUNC_INFO << max;

    // Apply this to all session torrents
    std::vector<libt::torrent_handle> handles = m_nativeSession->get_torrents();
    std::vector<libt::torrent_handle>::const_iterator it = handles.begin();
    std::vector<libt::torrent_handle>::const_iterator itend = handles.end();
    for ( ; it != itend; ++it) {
        if (!it->is_valid()) continue;
        try {
            it->set_max_connections(max);
        }
        catch(std::exception) {}
    }
}

void Session::setMaxUploadsPerTorrent(int max)
{
    qDebug() << Q_FUNC_INFO << max;

    // Apply this to all session torrents
    std::vector<libt::torrent_handle> handles = m_nativeSession->get_torrents();
    std::vector<libt::torrent_handle>::const_iterator it = handles.begin();
    std::vector<libt::torrent_handle>::const_iterator itend = handles.end();
    for ( ; it != itend; ++it) {
        if (!it->is_valid()) continue;
        try {
            it->set_max_uploads(max);
        }
        catch(std::exception) {}
    }
}

void Session::enableLSD(bool enable)
{
    if (enable) {
        if (!m_LSDEnabled) {
            qDebug("Enabling Local Peer Discovery");
            m_nativeSession->start_lsd();
            m_LSDEnabled = true;
        }
    }
    else {
        if (m_LSDEnabled) {
            qDebug("Disabling Local Peer Discovery");
            m_nativeSession->stop_lsd();
            m_LSDEnabled = false;
        }
    }
}

// Enable DHT
void Session::enableDHT(bool enable)
{
    Logger* const logger = Logger::instance();

    if (enable) {
        if (!m_DHTEnabled) {
            try {
                qDebug() << "Starting DHT...";
                Q_ASSERT(!m_nativeSession->is_dht_running());
                m_nativeSession->start_dht();
                m_nativeSession->add_dht_router(std::make_pair(std::string("router.bittorrent.com"), 6881));
                m_nativeSession->add_dht_router(std::make_pair(std::string("router.utorrent.com"), 6881));
                m_nativeSession->add_dht_router(std::make_pair(std::string("dht.transmissionbt.com"), 6881));
                m_nativeSession->add_dht_router(std::make_pair(std::string("dht.aelitis.com"), 6881)); // Vuze
                m_DHTEnabled = true;
                logger->addMessage(tr("DHT support [ON]"), Log::INFO);
                qDebug("DHT enabled");
            }
            catch(std::exception &e) {
                qDebug("Could not enable DHT, reason: %s", e.what());
                logger->addMessage(tr("DHT support [OFF]. Reason: %1").arg(Utils::String::fromStdString(e.what())), Log::CRITICAL);
            }
        }
    }
    else {
        if (m_DHTEnabled) {
            m_DHTEnabled = false;
            m_nativeSession->stop_dht();
            logger->addMessage(tr("DHT support [OFF]"), Log::INFO);
            qDebug("DHT disabled");
        }
    }
}

void Session::changeSpeedLimitMode_impl(bool alternative)
{
    qDebug() << Q_FUNC_INFO << alternative;
    Preferences* const pref = Preferences::instance();

    // Save new state to remember it on startup
    pref->setAltBandwidthEnabled(alternative);

    // Apply settings to the bittorrent session
    int downLimit = alternative ? pref->getAltGlobalDownloadLimit() : pref->getGlobalDownloadLimit();
    if (downLimit <= 0)
        downLimit = -1;
    else
        downLimit *= 1024;
    setDownloadRateLimit(downLimit);

    // Upload rate
    int upLimit = alternative ? pref->getAltGlobalUploadLimit() : pref->getGlobalUploadLimit();
    if (upLimit <= 0)
        upLimit = -1;
    else
        upLimit *= 1024;
    setUploadRateLimit(upLimit);

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
    if (m_defaultSavePath == path) return;

    m_defaultSavePath = path;
    m_settings->storeValue(KEY_DEFAULTSAVEPATH, m_defaultSavePath);

    if (isDisableAutoTMMWhenDefaultSavePathChanged())
        foreach (TorrentHandle *const torrent, torrents())
            torrent->setAutoTMMEnabled(false);
    else
        foreach (TorrentHandle *const torrent, torrents())
            torrent->handleCategorySavePathChanged();
}

void Session::setTempPath(QString path)
{
    path = normalizeSavePath(path, m_defaultSavePath + "temp");
    if (m_tempPath == path) return;

    m_tempPath = path;
    m_settings->storeValue(KEY_TEMPPATH, m_tempPath);

    foreach (TorrentHandle *const torrent, m_torrents)
        torrent->handleTempPathChanged();
}

void Session::setAppendExtension(bool append)
{
    if (m_appendExtension != append) {
        m_appendExtension = append;
        // append or remove .!qB extension for incomplete files
        foreach (TorrentHandle *const torrent, m_torrents)
            torrent->handleAppendExtensionToggled();
    }
}

void Session::networkOnlineStateChanged(const bool online)
{
    Logger::instance()->addMessage(tr("System network status changed to %1", "e.g: System network status changed to ONLINE").arg(online ? tr("ONLINE") : tr("OFFLINE")), Log::INFO);
}

void Session::networkConfigurationChange(const QNetworkConfiguration& cfg)
{
    const QString configuredInterfaceName = Preferences::instance()->getNetworkInterface();
    // Empty means "Any Interface". In this case libtorrent has binded to 0.0.0.0 so any change to any interface will
    // be automatically picked up. Otherwise we would rebinding here to 0.0.0.0 again.
    if (configuredInterfaceName.isEmpty())
        return;
    const QString changedInterface = cfg.name();
    if (configuredInterfaceName == changedInterface) {
        Logger::instance()->addMessage(tr("Network configuration of %1 has changed, refreshing session binding", "e.g: Network configuration of tun0 has changed, refreshing session binding").arg(changedInterface), Log::INFO);
        setListeningPort();
    }
}

const QStringList Session::getListeningIPs()
{
    Preferences* const pref = Preferences::instance();
    Logger* const logger = Logger::instance();
    QStringList IPs;

    const QString ifaceName = pref->getNetworkInterface();
    const QString ifaceAddr = pref->getNetworkInterfaceAddress();
    const bool listenIPv6 = pref->getListenIPv6();

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

        // If an iface address has been defined only allow ip's that match it to go through
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
void Session::setListeningPort()
{
    Preferences* const pref = Preferences::instance();
    const unsigned short port = pref->getSessionPort();
    qDebug() << Q_FUNC_INFO << port;
    Logger* const logger = Logger::instance();

    std::pair<int,int> ports(port, port);
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
}

// Set download rate limit
// -1 to disable
void Session::setDownloadRateLimit(int rate)
{
    qDebug() << Q_FUNC_INFO << rate;
    Q_ASSERT((rate == -1) || (rate >= 0));
    libt::session_settings settings = m_nativeSession->settings();
    settings.download_rate_limit = rate;
    m_nativeSession->set_settings(settings);
}

// Set upload rate limit
// -1 to disable
void Session::setUploadRateLimit(int rate)
{
    qDebug() << Q_FUNC_INFO << rate;
    Q_ASSERT((rate == -1) || (rate >= 0));
    libt::session_settings settings = m_nativeSession->settings();
    settings.upload_rate_limit = rate;
    m_nativeSession->set_settings(settings);
}

int Session::downloadRateLimit() const
{
    return m_nativeSession->settings().download_rate_limit;
}

int Session::uploadRateLimit() const
{
    return m_nativeSession->settings().upload_rate_limit;
}

bool Session::isListening() const
{
    return m_nativeSession->is_listening();
}

MaxRatioAction Session::maxRatioAction() const
{
    return m_maxRatioAction;
}

void Session::setMaxRatioAction(MaxRatioAction act)
{
    if (m_maxRatioAction != act) {
        m_maxRatioAction = act;
        m_settings->storeValue(KEY_MAXRATIOACTION, act);
    }
}

// Torrents will a ratio superior to the given value will
// be automatically deleted
void Session::setGlobalMaxRatio(qreal ratio)
{
    if (ratio < 0)
        ratio = -1.;
    if (m_globalMaxRatio != ratio) {
        m_globalMaxRatio = ratio;
        qDebug("* Set globalMaxRatio to %.1f", m_globalMaxRatio);
        updateRatioTimer();
    }
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
    if ((m_globalMaxRatio == -1) && !hasPerTorrentRatioLimit()) {
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
        if (m_torrentExportEnabled)
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

    Preferences *const pref = Preferences::instance();
    // Move .torrent file to another folder
    if (pref->isFinishedTorrentExportEnabled())
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

// Enable IP Filtering
void Session::enableIPFilter(const QString &filterPath, bool force)
{
    qDebug("Enabling IPFilter");
    if (!m_filterParser) {
        m_filterParser = new FilterParserThread(m_nativeSession, this);
        connect(m_filterParser.data(), SIGNAL(IPFilterParsed(int)), SLOT(handleIPFilterParsed(int)));
        connect(m_filterParser.data(), SIGNAL(IPFilterError()), SLOT(handleIPFilterError()));
    }
    if (m_filterPath.isEmpty() || m_filterPath != Utils::Fs::fromNativePath(filterPath) || force) {
        m_filterPath = Utils::Fs::fromNativePath(filterPath);
        m_filterParser->processFilterFile(Utils::Fs::fromNativePath(filterPath));
    }
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
    m_filterPath = "";
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

// Set Proxy
void Session::setProxySettings(libt::proxy_settings proxySettings)
{
    qDebug() << Q_FUNC_INFO;

    proxySettings.proxy_peer_connections = Preferences::instance()->proxyPeerConnections();
    m_nativeSession->set_proxy(proxySettings);

    // Define environment variables for urllib in search engine plugins
    if (Preferences::instance()->isProxyOnlyForTorrents()) {
        qputenv("http_proxy", QByteArray());
        qputenv("https_proxy", QByteArray());
        qputenv("sock_proxy", QByteArray());
    }
    else {
        QString proxy_str;
        switch(proxySettings.type) {
        case libt::proxy_settings::http_pw:
            proxy_str = QString("http://%1:%2@%3:%4").arg(Utils::String::fromStdString(proxySettings.username)).arg(Utils::String::fromStdString(proxySettings.password))
                    .arg(Utils::String::fromStdString(proxySettings.hostname)).arg(proxySettings.port);
            break;
        case libt::proxy_settings::http:
            proxy_str = QString("http://%1:%2").arg(Utils::String::fromStdString(proxySettings.hostname)).arg(proxySettings.port);
            break;
        case libt::proxy_settings::socks5:
            proxy_str = QString("%1:%2").arg(Utils::String::fromStdString(proxySettings.hostname)).arg(proxySettings.port);
            break;
        case libt::proxy_settings::socks5_pw:
            proxy_str = QString("%1:%2@%3:%4").arg(Utils::String::fromStdString(proxySettings.username)).arg(Utils::String::fromStdString(proxySettings.password))
                    .arg(Utils::String::fromStdString(proxySettings.hostname)).arg(proxySettings.port);
            break;
        default:
            qDebug("Disabling HTTP communications proxy");
            qputenv("http_proxy", QByteArray());
            qputenv("https_proxy", QByteArray());
            qputenv("sock_proxy", QByteArray());
            return;
        }
        qDebug("HTTP communications proxy string: %s", qPrintable(proxy_str));
        if ((proxySettings.type == libt::proxy_settings::socks5) || (proxySettings.type == libt::proxy_settings::socks5_pw))
            qputenv("sock_proxy", proxy_str.toLocal8Bit());
        else {
            qputenv("http_proxy", proxy_str.toLocal8Bit());
            qputenv("https_proxy", proxy_str.toLocal8Bit());
        }
    }
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
    emit ipFilterParsed(false, ruleCount);
}

void Session::handleIPFilterError()
{
    Logger::instance()->addMessage(tr("Error: Failed to parse the provided IP filter."), Log::CRITICAL);
    emit ipFilterParsed(true, 0);
}

#if LIBTORRENT_VERSION_NUM < 10100
void Session::dispatchAlerts(std::auto_ptr<libt::alert> alertPtr)
{
    QMutexLocker lock(&m_alertsMutex);

    bool wasEmpty = m_alerts.empty();

    m_alerts.push_back(alertPtr.release());

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

    Preferences *const pref = Preferences::instance();
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
                if (m_torrentExportEnabled)
                    exportTorrentFile(torrent);
            }
            else {
                logger->addMessage(tr("Couldn't save '%1.torrent'").arg(torrent->hash()), Log::CRITICAL);
            }
        }

        if (pref->isAddTrackersEnabled() && !torrent->isPrivate())
            torrent->addTrackers(m_additionalTrackers);

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

        libt::lazy_entry fast;
        libt::error_code ec;
        libt::lazy_bdecode(data.constData(), data.constData() + data.size(), fast, ec);
        if (ec || (fast.type() != libt::lazy_entry::dict_t)) return false;

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

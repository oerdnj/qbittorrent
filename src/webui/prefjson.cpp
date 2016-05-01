/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006-2012  Ishan Arora and Christophe Dumez
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

#include "prefjson.h"
#include "base/preferences.h"
#include "base/scanfoldersmodel.h"
#include "base/utils/fs.h"

#ifndef QT_NO_OPENSSL
#include <QSslCertificate>
#include <QSslKey>
#endif
#include <QStringList>
#include <QTranslator>
#include <QCoreApplication>
#include "jsonutils.h"

prefjson::prefjson()
{
}

QByteArray prefjson::getPreferences()
{
    const Preferences* const pref = Preferences::instance();
    QVariantMap data;

    // Downloads
    // Hard Disk
    data["save_path"] = Utils::Fs::toNativePath(pref->getSavePath());
    data["temp_path_enabled"] = pref->isTempPathEnabled();
    data["temp_path"] = Utils::Fs::toNativePath(pref->getTempPath());
    data["preallocate_all"] = pref->preAllocateAllFiles();
    data["incomplete_files_ext"] = pref->useIncompleteFilesExtension();
    QVariantHash dirs = pref->getScanDirs();
    QVariantMap nativeDirs;
    for (QVariantHash::const_iterator i = dirs.begin(), e = dirs.end(); i != e; ++i) {
        if (i.value().type() == QVariant::Int)
            nativeDirs.insert(Utils::Fs::toNativePath(i.key()), i.value().toInt());
        else
            nativeDirs.insert(Utils::Fs::toNativePath(i.key()), Utils::Fs::toNativePath(i.value().toString()));
    }
    data["scan_dirs"] = nativeDirs;
    data["export_dir"] = Utils::Fs::toNativePath(pref->getTorrentExportDir());
    data["export_dir_fin"] = Utils::Fs::toNativePath(pref->getFinishedTorrentExportDir());
    // Email notification upon download completion
    data["mail_notification_enabled"] = pref->isMailNotificationEnabled();
    data["mail_notification_email"] = pref->getMailNotificationEmail();
    data["mail_notification_smtp"] = pref->getMailNotificationSMTP();
    data["mail_notification_ssl_enabled"] = pref->getMailNotificationSMTPSSL();
    data["mail_notification_auth_enabled"] = pref->getMailNotificationSMTPAuth();
    data["mail_notification_username"] = pref->getMailNotificationSMTPUsername();
    data["mail_notification_password"] = pref->getMailNotificationSMTPPassword();
    // Run an external program on torrent completion
    data["autorun_enabled"] = pref->isAutoRunEnabled();
    data["autorun_program"] = Utils::Fs::toNativePath(pref->getAutoRunProgram());

    // Connection
    // Listening Port
    data["listen_port"] = pref->getSessionPort();
    data["upnp"] = pref->isUPnPEnabled();
    data["random_port"] = pref->useRandomPort();
    // Connections Limits
    data["max_connec"] = pref->getMaxConnecs();
    data["max_connec_per_torrent"] = pref->getMaxConnecsPerTorrent();
    data["max_uploads"] = pref->getMaxUploads();
    data["max_uploads_per_torrent"] = pref->getMaxUploadsPerTorrent();
    // Proxy Server
    data["proxy_type"] = pref->getProxyType();
    data["proxy_ip"] = pref->getProxyIp();
    data["proxy_port"] = pref->getProxyPort();
    data["proxy_peer_connections"] = pref->proxyPeerConnections();
    data["force_proxy"] = pref->getForceProxy();
    data["proxy_auth_enabled"] = pref->isProxyAuthEnabled();
    data["proxy_username"] = pref->getProxyUsername();
    data["proxy_password"] = pref->getProxyPassword();
    // IP Filtering
    data["ip_filter_enabled"] = pref->isFilteringEnabled();
    data["ip_filter_path"] = Utils::Fs::toNativePath(pref->getFilter());
    data["ip_filter_trackers"] = pref->isFilteringTrackerEnabled();

    // Speed
    // Global Rate Limits
    data["dl_limit"] = pref->getGlobalDownloadLimit();
    data["up_limit"] = pref->getGlobalUploadLimit();
    data["enable_utp"] = pref->isuTPEnabled();
    data["limit_utp_rate"] = pref->isuTPRateLimited();
    data["limit_tcp_overhead"] = pref->includeOverheadInLimits();
    data["alt_dl_limit"] = pref->getAltGlobalDownloadLimit();
    data["alt_up_limit"] = pref->getAltGlobalUploadLimit();
    // Scheduling
    data["scheduler_enabled"] = pref->isSchedulerEnabled();
    const QTime start_time = pref->getSchedulerStartTime();
    data["schedule_from_hour"] = start_time.hour();
    data["schedule_from_min"] = start_time.minute();
    const QTime end_time = pref->getSchedulerEndTime();
    data["schedule_to_hour"] = end_time.hour();
    data["schedule_to_min"] = end_time.minute();
    data["scheduler_days"] = pref->getSchedulerDays();

    // Bittorrent
    // Privacy
    data["dht"] = pref->isDHTEnabled();
    data["pex"] = pref->isPeXEnabled();
    data["lsd"] = pref->isLSDEnabled();
    data["encryption"] = pref->getEncryptionSetting();
    data["anonymous_mode"] = pref->isAnonymousModeEnabled();
    // Torrent Queueing
    data["queueing_enabled"] = pref->isQueueingSystemEnabled();
    data["max_active_downloads"] = pref->getMaxActiveDownloads();
    data["max_active_torrents"] = pref->getMaxActiveTorrents();
    data["max_active_uploads"] = pref->getMaxActiveUploads();
    data["dont_count_slow_torrents"] = pref->ignoreSlowTorrentsForQueueing();
    // Share Ratio Limiting
    data["max_ratio_enabled"] = (pref->getGlobalMaxRatio() >= 0.);
    data["max_ratio"] = pref->getGlobalMaxRatio();
    data["max_ratio_act"] = static_cast<int>(pref->getMaxRatioAction());

    // Web UI
    // Language
    data["locale"] = pref->getLocale();
    // HTTP Server
    data["web_ui_port"] = pref->getWebUiPort();
    data["web_ui_upnp"] = pref->useUPnPForWebUIPort();
    data["use_https"] = pref->isWebUiHttpsEnabled();
    data["ssl_key"] = QString::fromLatin1(pref->getWebUiHttpsKey());
    data["ssl_cert"] = QString::fromLatin1(pref->getWebUiHttpsCertificate());
    // Authentication
    data["web_ui_username"] = pref->getWebUiUsername();
    data["web_ui_password"] = pref->getWebUiPassword();
    data["bypass_local_auth"] = !pref->isWebUiLocalAuthEnabled();
    // Update my dynamic domain name
    data["dyndns_enabled"] = pref->isDynDNSEnabled();
    data["dyndns_service"] = pref->getDynDNSService();
    data["dyndns_username"] = pref->getDynDNSUsername();
    data["dyndns_password"] = pref->getDynDNSPassword();
    data["dyndns_domain"] = pref->getDynDomainName();

    return json::toJson(data);
}

void prefjson::setPreferences(const QString& json)
{
    Preferences* const pref = Preferences::instance();
    const QVariantMap m = json::fromJson(json).toMap();

    // Downloads
    // Hard Disk
    if (m.contains("save_path"))
        pref->setSavePath(m["save_path"].toString());
    if (m.contains("temp_path_enabled"))
        pref->setTempPathEnabled(m["temp_path_enabled"].toBool());
    if (m.contains("temp_path"))
        pref->setTempPath(m["temp_path"].toString());
    if (m.contains("preallocate_all"))
        pref->preAllocateAllFiles(m["preallocate_all"].toBool());
    if (m.contains("incomplete_files_ext"))
        pref->useIncompleteFilesExtension(m["incomplete_files_ext"].toBool());
    if (m.contains("scan_dirs")) {
        QVariantMap nativeDirs = m["scan_dirs"].toMap();
        QVariantHash oldScanDirs = pref->getScanDirs();
        QVariantHash scanDirs;
        ScanFoldersModel *model = ScanFoldersModel::instance();
        for (QVariantMap::const_iterator i = nativeDirs.begin(), e = nativeDirs.end(); i != e; ++i) {
            QString folder = Utils::Fs::fromNativePath(i.key());
            int downloadType;
            QString downloadPath;
            ScanFoldersModel::PathStatus ec;
            if (i.value().type() == QVariant::String) {
                downloadType = ScanFoldersModel::CUSTOM_LOCATION;
                downloadPath = Utils::Fs::fromNativePath(i.value().toString());
            }
            else {
                downloadType = i.value().toInt();
                downloadPath = (downloadType == ScanFoldersModel::DEFAULT_LOCATION) ? "Default folder" : "Watch folder";
            }

            if (!oldScanDirs.contains(folder))
                ec = model->addPath(folder, static_cast<ScanFoldersModel::PathType>(downloadType), downloadPath);
            else
                ec = model->updatePath(folder, static_cast<ScanFoldersModel::PathType>(downloadType), downloadPath);

            if (ec == ScanFoldersModel::Ok) {
                scanDirs.insert(folder, (downloadType == ScanFoldersModel::CUSTOM_LOCATION) ? QVariant(downloadPath) : QVariant(downloadType));
                qDebug("New watched folder: %s to %s", qPrintable(folder), qPrintable(downloadPath));
            }
            else {
                qDebug("Watched folder %s failed with error %d", qPrintable(folder), ec);
            }
        }

        // Update deleted folders
        foreach (QVariant folderVariant, oldScanDirs.keys()) {
            QString folder = folderVariant.toString();
            if (!scanDirs.contains(folder)) {
                model->removePath(folder);
                qDebug("Removed watched folder %s", qPrintable(folder));
            }
        }
        pref->setScanDirs(scanDirs);
    }
    if (m.contains("export_dir"))
        pref->setTorrentExportDir(m["export_dir"].toString());
    if (m.contains("export_dir_fin"))
        pref->setFinishedTorrentExportDir(m["export_dir_fin"].toString());
    // Email notification upon download completion
    if (m.contains("mail_notification_enabled"))
        pref->setMailNotificationEnabled(m["mail_notification_enabled"].toBool());
    if (m.contains("mail_notification_email"))
        pref->setMailNotificationEmail(m["mail_notification_email"].toString());
    if (m.contains("mail_notification_smtp"))
        pref->setMailNotificationSMTP(m["mail_notification_smtp"].toString());
    if (m.contains("mail_notification_ssl_enabled"))
        pref->setMailNotificationSMTPSSL(m["mail_notification_ssl_enabled"].toBool());
    if (m.contains("mail_notification_auth_enabled"))
        pref->setMailNotificationSMTPAuth(m["mail_notification_auth_enabled"].toBool());
    if (m.contains("mail_notification_username"))
        pref->setMailNotificationSMTPUsername(m["mail_notification_username"].toString());
    if (m.contains("mail_notification_password"))
        pref->setMailNotificationSMTPPassword(m["mail_notification_password"].toString());
    // Run an external program on torrent completion
    if (m.contains("autorun_enabled"))
        pref->setAutoRunEnabled(m["autorun_enabled"].toBool());
    if (m.contains("autorun_program"))
        pref->setAutoRunProgram(m["autorun_program"].toString());

    // Connection
    // Listening Port
    if (m.contains("listen_port"))
        pref->setSessionPort(m["listen_port"].toInt());
    if (m.contains("upnp"))
        pref->setUPnPEnabled(m["upnp"].toBool());
    if (m.contains("random_port"))
        pref->setRandomPort(m["random_port"].toBool());
    // Connections Limits
    if (m.contains("max_connec"))
        pref->setMaxConnecs(m["max_connec"].toInt());
    if (m.contains("max_connec_per_torrent"))
        pref->setMaxConnecsPerTorrent(m["max_connec_per_torrent"].toInt());
    if (m.contains("max_uploads"))
        pref->setMaxUploads(m["max_uploads"].toInt());
    if (m.contains("max_uploads_per_torrent"))
        pref->setMaxUploadsPerTorrent(m["max_uploads_per_torrent"].toInt());
    // Proxy Server
    if (m.contains("proxy_type"))
        pref->setProxyType(m["proxy_type"].toInt());
    if (m.contains("proxy_ip"))
        pref->setProxyIp(m["proxy_ip"].toString());
    if (m.contains("proxy_port"))
        pref->setProxyPort(m["proxy_port"].toUInt());
    if (m.contains("proxy_peer_connections"))
        pref->setProxyPeerConnections(m["proxy_peer_connections"].toBool());
    if (m.contains("force_proxy"))
        pref->setForceProxy(m["force_proxy"].toBool());
    if (m.contains("proxy_auth_enabled"))
        pref->setProxyAuthEnabled(m["proxy_auth_enabled"].toBool());
    if (m.contains("proxy_username"))
        pref->setProxyUsername(m["proxy_username"].toString());
    if (m.contains("proxy_password"))
        pref->setProxyPassword(m["proxy_password"].toString());
    // IP Filtering
    if (m.contains("ip_filter_enabled"))
        pref->setFilteringEnabled(m["ip_filter_enabled"].toBool());
    if (m.contains("ip_filter_path"))
        pref->setFilter(m["ip_filter_path"].toString());
    if (m.contains("ip_filter_trackers"))
        pref->setFilteringTrackerEnabled(m["ip_filter_trackers"].toBool());

    // Speed
    // Global Rate Limits
    if (m.contains("dl_limit"))
        pref->setGlobalDownloadLimit(m["dl_limit"].toInt());
    if (m.contains("up_limit"))
        pref->setGlobalUploadLimit(m["up_limit"].toInt());
    if (m.contains("enable_utp"))
        pref->setuTPEnabled(m["enable_utp"].toBool());
    if (m.contains("limit_utp_rate"))
        pref->setuTPRateLimited(m["limit_utp_rate"].toBool());
    if (m.contains("limit_tcp_overhead"))
        pref->includeOverheadInLimits(m["limit_tcp_overhead"].toBool());
    if (m.contains("alt_dl_limit"))
        pref->setAltGlobalDownloadLimit(m["alt_dl_limit"].toInt());
    if (m.contains("alt_up_limit"))
        pref->setAltGlobalUploadLimit(m["alt_up_limit"].toInt());
    // Scheduling
    if (m.contains("scheduler_enabled"))
        pref->setSchedulerEnabled(m["scheduler_enabled"].toBool());
    if (m.contains("schedule_from_hour") && m.contains("schedule_from_min")) {
        pref->setSchedulerStartTime(QTime(m["schedule_from_hour"].toInt(),
                                                                         m["schedule_from_min"].toInt()));
    }
    if (m.contains("schedule_to_hour") && m.contains("schedule_to_min")) {
        pref->setSchedulerEndTime(QTime(m["schedule_to_hour"].toInt(),
                                                                     m["schedule_to_min"].toInt()));
    }
    if (m.contains("scheduler_days"))
        pref->setSchedulerDays(scheduler_days(m["scheduler_days"].toInt()));

    // Bittorrent
    // Privacy
    if (m.contains("dht"))
        pref->setDHTEnabled(m["dht"].toBool());
    if (m.contains("pex"))
        pref->setPeXEnabled(m["pex"].toBool());
    if (m.contains("lsd"))
        pref->setLSDEnabled(m["lsd"].toBool());
    if (m.contains("encryption"))
        pref->setEncryptionSetting(m["encryption"].toInt());
    if (m.contains("anonymous_mode"))
        pref->enableAnonymousMode(m["anonymous_mode"].toBool());
    // Torrent Queueing
    if (m.contains("queueing_enabled"))
        pref->setQueueingSystemEnabled(m["queueing_enabled"].toBool());
    if (m.contains("max_active_downloads"))
        pref->setMaxActiveDownloads(m["max_active_downloads"].toInt());
    if (m.contains("max_active_torrents"))
        pref->setMaxActiveTorrents(m["max_active_torrents"].toInt());
    if (m.contains("max_active_uploads"))
        pref->setMaxActiveUploads(m["max_active_uploads"].toInt());
    if (m.contains("dont_count_slow_torrents"))
        pref->setIgnoreSlowTorrentsForQueueing(m["dont_count_slow_torrents"].toBool());
    // Share Ratio Limiting
    if (m.contains("max_ratio_enabled"))
        pref->setGlobalMaxRatio(m["max_ratio"].toReal());
    else
        pref->setGlobalMaxRatio(-1);
    if (m.contains("max_ratio_act"))
        pref->setMaxRatioAction(static_cast<MaxRatioAction>(m["max_ratio_act"].toInt()));

    // Web UI
    // Language
    if (m.contains("locale")) {
        QString locale = m["locale"].toString();
        if (pref->getLocale() != locale) {
            QTranslator *translator = new QTranslator;
            if (translator->load(QString::fromUtf8(":/lang/qbittorrent_") + locale)) {
                qDebug("%s locale recognized, using translation.", qPrintable(locale));
            }else{
                qDebug("%s locale unrecognized, using default (en).", qPrintable(locale));
            }
            qApp->installTranslator(translator);

            pref->setLocale(locale);
        }
    }
    // HTTP Server
    if (m.contains("web_ui_port"))
        pref->setWebUiPort(m["web_ui_port"].toUInt());
    if (m.contains("web_ui_upnp"))
        pref->setUPnPForWebUIPort(m["web_ui_upnp"].toBool());
    if (m.contains("use_https"))
        pref->setWebUiHttpsEnabled(m["use_https"].toBool());
#ifndef QT_NO_OPENSSL
    if (m.contains("ssl_key")) {
        QByteArray raw_key = m["ssl_key"].toString().toLatin1();
        if (!QSslKey(raw_key, QSsl::Rsa).isNull())
            pref->setWebUiHttpsKey(raw_key);
    }
    if (m.contains("ssl_cert")) {
        QByteArray raw_cert = m["ssl_cert"].toString().toLatin1();
        if (!QSslCertificate(raw_cert).isNull())
            pref->setWebUiHttpsCertificate(raw_cert);
    }
#endif
    // Authentication
    if (m.contains("web_ui_username"))
        pref->setWebUiUsername(m["web_ui_username"].toString());
    if (m.contains("web_ui_password"))
        pref->setWebUiPassword(m["web_ui_password"].toString());
    if (m.contains("bypass_local_auth"))
        pref->setWebUiLocalAuthEnabled(!m["bypass_local_auth"].toBool());
    // Update my dynamic domain name
    if (m.contains("dyndns_enabled"))
        pref->setDynDNSEnabled(m["dyndns_enabled"].toBool());
    if (m.contains("dyndns_service"))
        pref->setDynDNSService(m["dyndns_service"].toInt());
    if (m.contains("dyndns_username"))
        pref->setDynDNSUsername(m["dyndns_username"].toString());
    if (m.contains("dyndns_password"))
        pref->setDynDNSPassword(m["dyndns_password"].toString());
    if (m.contains("dyndns_domain"))
        pref->setDynDomainName(m["dyndns_domain"].toString());

    // Save preferences
    pref->apply();
}

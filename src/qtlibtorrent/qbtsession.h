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
#ifndef __BITTORRENT_H__
#define __BITTORRENT_H__

#include <QHash>
#include <QMap>
#include <QUrl>
#include <QStringList>
#ifdef DISABLE_GUI
#include <QCoreApplication>
#else
#include <QApplication>
#include <QPalette>
#endif
#include <QPointer>
#include <QTimer>

#include <libtorrent/version.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/ip_filter.hpp>

#include "qtracker.h"
#include "qtorrenthandle.h"
#include "trackerinfos.h"

#define MAX_SAMPLES 20

class downloadThread;
class QTimer;
class FilterParserThread;
class HttpServer;
class BandwidthScheduler;
class ScanFoldersModel;
class TorrentSpeedMonitor;

class QBtSession : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(QBtSession)

private:
  explicit QBtSession();
  static QBtSession* m_instance;

public:
  static QBtSession* instance();
  static void drop();
  ~QBtSession();
  QTorrentHandle getTorrentHandle(QString hash) const;
  std::vector<libtorrent::torrent_handle> getTorrents() const;
  bool isFilePreviewPossible(QString fileHash) const;
  qreal getPayloadDownloadRate() const;
  qreal getPayloadUploadRate() const;
  libtorrent::session_status getSessionStatus() const;
  int getListenPort() const;
  qreal getRealRatio(QString hash) const;
  QHash<QString, TrackerInfos> getTrackersInfo(QString hash) const;
  bool hasActiveTorrents() const;
  bool hasDownloadingTorrents() const;
  //int getMaximumActiveDownloads() const;
  //int getMaximumActiveTorrents() const;
  int loadTorrentPriority(QString hash);
  inline QStringList getConsoleMessages() const { return consoleMessages; }
  inline QStringList getPeerBanMessages() const { return peerBanMessages; }
  inline libtorrent::session* getSession() const { return s; }
  inline bool useTemporaryFolder() const { return !defaultTempPath.isEmpty(); }
  inline QString getDefaultSavePath() const { return defaultSavePath; }
  inline ScanFoldersModel* getScanFoldersModel() const {  return m_scanFolders; }
  inline bool isDHTEnabled() const { return DHTEnabled; }
  inline bool isLSDEnabled() const { return LSDEnabled; }
  inline bool isPexEnabled() const { return PeXEnabled; }
  inline bool isQueueingEnabled() const { return queueingEnabled; }

public slots:
  QTorrentHandle addTorrent(QString path, bool fromScanDir = false, QString from_url = QString(), bool resumed = false);
  QTorrentHandle addMagnetUri(QString magnet_uri, bool resumed=false);
  void loadSessionState();
  void saveSessionState();
  void downloadFromUrl(QString url);
  void deleteTorrent(QString hash, bool delete_local_files = false);
  void startUpTorrents();
  void recheckTorrent(QString hash);
  void useAlternativeSpeedsLimit(bool alternative);
  qlonglong getETA(const QString& hash) const;
  /* Needed by Web UI */
  void pauseAllTorrents();
  void pauseTorrent(QString hash);
  void resumeTorrent(QString hash);
  void resumeAllTorrents();
  /* End Web UI */
  void preAllocateAllFiles(bool b);
  void saveFastResumeData();
  void enableIPFilter(const QString &filter_path, bool force=false);
  void disableIPFilter();
  void setQueueingEnabled(bool enable);
  void handleDownloadFailure(QString url, QString reason);
  void downloadUrlAndSkipDialog(QString url, QString save_path=QString(), QString label=QString());
  // Session configuration - Setters
  void setListeningPort(int port);
  void setMaxConnections(int maxConnec);
  void setMaxConnectionsPerTorrent(int max);
  void setMaxUploadsPerTorrent(int max);
  void setDownloadRateLimit(long rate);
  void setUploadRateLimit(long rate);
  void setMaxRatio(qreal ratio);
  void setDHTPort(int dht_port);
  void setProxySettings(const libtorrent::proxy_settings &proxySettings);
  void setSessionSettings(const libtorrent::session_settings &sessionSettings);
  void startTorrentsInPause(bool b);
  void setDefaultTempPath(QString temppath);
  void setAppendLabelToSavePath(bool append);
  void appendLabelToTorrentSavePath(const QTorrentHandle &h);
  void changeLabelInTorrentSavePath(const QTorrentHandle &h, QString old_label, QString new_label);
#if LIBTORRENT_VERSION_MINOR > 14
  void appendqBextensionToTorrent(const QTorrentHandle &h, bool append);
  void setAppendqBExtension(bool append);
#endif
  void applyEncryptionSettings(libtorrent::pe_settings se);
  void setDownloadLimit(QString hash, long val);
  void setUploadLimit(QString hash, long val);
  void enableUPnP(bool b);
  void enableLSD(bool b);
  bool enableDHT(bool b);
#ifdef DISABLE_GUI
  void addConsoleMessage(QString msg, QString color=QString::null);
#else
  void addConsoleMessage(QString msg, QColor color=QApplication::palette().color(QPalette::WindowText));
#endif
  void addPeerBanMessage(QString msg, bool from_ipfilter);
  void processDownloadedFile(QString, QString);
  void addMagnetSkipAddDlg(QString uri);
  void downloadFromURLList(const QStringList& urls);
  void configureSession();
  void banIP(QString ip);
  void recursiveTorrentDownload(const QTorrentHandle &h);

private:
  QString getSavePath(QString hash, bool fromScanDir = false, QString filePath = QString::null, QString root_folder=QString::null);
  bool loadFastResumeData(QString hash, std::vector<char> &buf);
  void loadTorrentSettings(QTorrentHandle h);
  void loadTorrentTempData(QTorrentHandle h, QString savePath, bool magnet);
  libtorrent::add_torrent_params initializeAddTorrentParams(QString hash);
  libtorrent::entry generateFilePriorityResumeData(boost::intrusive_ptr<libtorrent::torrent_info> &t, const std::vector<int> &fp);

private slots:
  void addTorrentsFromScanFolder(QStringList&);
  void readAlerts();
  void processBigRatios();
  void exportTorrentFiles(QString path);
  void saveTempFastResumeData();
  void sendNotificationEmail(QTorrentHandle h);
  void autoRunExternalProgram(QTorrentHandle h, bool async=true);
  void cleanUpAutoRunProcess(int);
  void mergeTorrents(QTorrentHandle h_ex, boost::intrusive_ptr<libtorrent::torrent_info> t);
  void exportTorrentFile(QTorrentHandle h);
  void initWebUi();
  void handleIPFilterParsed(int ruleCount);
  void handleIPFilterError();

signals:
  void addedTorrent(const QTorrentHandle& h);
  void deletedTorrent(QString hash);
  void torrentAboutToBeRemoved(const QTorrentHandle &h);
  void pausedTorrent(const QTorrentHandle& h);
  void resumedTorrent(const QTorrentHandle& h);
  void finishedTorrent(const QTorrentHandle& h);
  void fullDiskError(const QTorrentHandle& h, QString msg);
  void trackerError(QString hash, QString time, QString msg);
  void trackerAuthenticationRequired(const QTorrentHandle& h);
  void newDownloadedTorrent(QString path, QString url);
  void updateFileSize(QString hash);
  void downloadFromUrlFailure(QString url, QString reason);
  void torrentFinishedChecking(const QTorrentHandle& h);
  void metadataReceived(const QTorrentHandle &h);
  void savePathChanged(const QTorrentHandle &h);
  void newConsoleMessage(const QString &msg);
  void newBanMessage(const QString &msg);
  void alternativeSpeedsModeChanged(bool alternative);
  void recursiveTorrentDownloadPossible(const QTorrentHandle &h);
  void ipFilterParsed(bool error, int ruleCount);

private:
#if LIBTORRENT_VERSION_MINOR < 15
  void saveDHTEntry();
#endif

private:
  // Bittorrent
  libtorrent::session *s;
  QPointer<QTimer> timerAlerts;
  QPointer<BandwidthScheduler> bd_scheduler;
  QMap<QUrl, QPair<QString, QString> > savepathLabel_fromurl;
  QHash<QString, QHash<QString, TrackerInfos> > trackersInfos;
  QHash<QString, QString> savePathsToRemove;
  QStringList torrentsToPausedAfterChecking;
  QTimer resumeDataTimer;
  // Ratio
  QPointer<QTimer> BigRatioTimer;
  // HTTP
  downloadThread* downloader;
  // File System
  ScanFoldersModel *m_scanFolders;
  // Console / Log
  QStringList consoleMessages;
  QStringList peerBanMessages;
  // Settings
  bool preAllocateAll;
  bool addInPause;
  qreal ratio_limit;
  int high_ratio_action;
  bool UPnPEnabled;
  bool LSDEnabled;
  bool DHTEnabled;
  int current_dht_port;
  bool PeXEnabled;
  bool queueingEnabled;
  bool appendLabelToSavePath;
  bool torrentExport;
#if LIBTORRENT_VERSION_MINOR > 14
  bool appendqBExtension;
#endif
  QString defaultSavePath;
  QString defaultTempPath;
  // IP filtering
  QPointer<FilterParserThread> filterParser;
  QString filterPath;
  // Web UI
  QPointer<HttpServer> httpServer;
  QList<QUrl> url_skippingDlg;
  // GeoIP
#ifndef DISABLE_GUI
  bool geoipDBLoaded;
  bool resolve_countries;
#endif
  // Tracker
  QPointer<QTracker> m_tracker;
  TorrentSpeedMonitor *m_speedMonitor;

};

#endif

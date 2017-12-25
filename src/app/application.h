/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015  Vladimir Golovnev <glassez@yandex.ru>
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
 */

#ifndef APPLICATION_H
#define APPLICATION_H

#include <QPointer>
#include <QStringList>
#include <QTranslator>

#ifndef DISABLE_GUI
#include "qtsingleapplication.h"
typedef QtSingleApplication BaseApplication;
class MainWindow;

#ifdef Q_OS_WIN
QT_BEGIN_NAMESPACE
class QSessionManager;
QT_END_NAMESPACE
#endif // Q_OS_WIN

#else
#include "qtsinglecoreapplication.h"
typedef QtSingleCoreApplication BaseApplication;
#endif

#include "base/utils/misc.h"
#include "cmdoptions.h"

#ifndef DISABLE_WEBUI
class WebUI;
#endif

class FileLogger;

namespace BitTorrent
{
    class TorrentHandle;
}

namespace RSS
{
    class Session;
    class AutoDownloader;
}

class Application : public BaseApplication
{
    Q_OBJECT
    Q_DISABLE_COPY(Application)

public:
    Application(const QString &id, int &argc, char **argv);
    ~Application() override;

 #if (defined(Q_OS_WIN) && !defined(DISABLE_GUI))
    bool isRunning();
#endif
    int exec(const QStringList &params);
    bool sendParams(const QStringList &params);

#ifndef DISABLE_GUI
    QPointer<MainWindow> mainWindow();
#endif

    const QBtCommandLineParameters &commandLineArgs() const;

    // FileLogger properties
    bool isFileLoggerEnabled() const;
    void setFileLoggerEnabled(bool value);
    QString fileLoggerPath() const;
    void setFileLoggerPath(const QString &path);
    bool isFileLoggerBackup() const;
    void setFileLoggerBackup(bool value);
    bool isFileLoggerDeleteOld() const;
    void setFileLoggerDeleteOld(bool value);
    int fileLoggerMaxSize() const;
    void setFileLoggerMaxSize(const int value);
    int fileLoggerAge() const;
    void setFileLoggerAge(const int value);
    int fileLoggerAgeType() const;
    void setFileLoggerAgeType(const int value);

protected:
#ifndef DISABLE_GUI
#ifdef Q_OS_MAC
    bool event(QEvent *);
#endif
    bool notify(QObject* receiver, QEvent* event);
#endif

private slots:
    void processMessage(const QString &message);
    void torrentFinished(BitTorrent::TorrentHandle *const torrent);
    void allTorrentsFinished();
    void cleanup();
#if (!defined(DISABLE_GUI) && defined(Q_OS_WIN))
    void shutdownCleanup(QSessionManager &manager);
#endif

private:
    bool m_running;
    ShutdownDialogAction m_shutdownAct;
    QBtCommandLineParameters m_commandLineArgs;

#ifndef DISABLE_GUI
    QPointer<MainWindow> m_window;
#endif

#ifndef DISABLE_WEBUI
    WebUI *m_webui;
#endif

    // FileLog
    QPointer<FileLogger> m_fileLogger;

    QTranslator m_qtTranslator;
    QTranslator m_translator;
    QStringList m_paramsQueue;

    void initializeTranslation();
    void processParams(const QStringList &params);
    void runExternalProgram(BitTorrent::TorrentHandle *const torrent) const;
    void sendNotificationEmail(const BitTorrent::TorrentHandle *torrent);
    void validateCommandLineParameters();
};

#endif // APPLICATION_H

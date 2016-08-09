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

#include <QDebug>
#include <QFileInfo>
#include <QLocale>
#include <QLibraryInfo>
#include <QSysInfo>
#include <QProcess>
#include <QAtomicInt>

#ifndef DISABLE_GUI
#include "gui/guiiconprovider.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <QSharedMemory>
#include <QSessionManager>
#endif // Q_OS_WIN
#ifdef Q_OS_MAC
#include <QFileOpenEvent>
#include <QFont>
#include <QUrl>
#endif // Q_OS_MAC
#include "mainwindow.h"
#include "addnewtorrentdialog.h"
#include "shutdownconfirmdlg.h"
#else // DISABLE_GUI
#include <iostream>
#endif // DISABLE_GUI

#ifndef DISABLE_WEBUI
#include "webui/webui.h"
#endif

#include "application.h"
#include "filelogger.h"
#include "base/logger.h"
#include "base/preferences.h"
#include "base/settingsstorage.h"
#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/iconprovider.h"
#include "base/scanfoldersmodel.h"
#include "base/net/smtp.h"
#include "base/net/downloadmanager.h"
#include "base/net/geoipmanager.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrenthandle.h"

namespace
{
#define SETTINGS_KEY(name) "Application/" name

    // FileLogger properties keys
#define FILELOGGER_SETTINGS_KEY(name) SETTINGS_KEY("FileLogger/") name
    const QString KEY_FILELOGGER_ENABLED = FILELOGGER_SETTINGS_KEY("Enabled");
    const QString KEY_FILELOGGER_PATH = FILELOGGER_SETTINGS_KEY("Path");
    const QString KEY_FILELOGGER_BACKUP = FILELOGGER_SETTINGS_KEY("Backup");
    const QString KEY_FILELOGGER_DELETEOLD = FILELOGGER_SETTINGS_KEY("DeleteOld");
    const QString KEY_FILELOGGER_MAXSIZE = FILELOGGER_SETTINGS_KEY("MaxSize");
    const QString KEY_FILELOGGER_AGE = FILELOGGER_SETTINGS_KEY("Age");
    const QString KEY_FILELOGGER_AGETYPE = FILELOGGER_SETTINGS_KEY("AgeType");

    //just a shortcut
    inline SettingsStorage *settings() { return  SettingsStorage::instance(); }

    const QString LOG_FOLDER("logs");
    const char PARAMS_SEPARATOR[] = "|";
}

Application::Application(const QString &id, int &argc, char **argv)
    : BaseApplication(id, argc, argv)
    , m_running(false)
    , m_shutdownAct(ShutdownDialogAction::Exit)
{
    Logger::initInstance();
    SettingsStorage::initInstance();
    Preferences::initInstance();

#if defined(Q_OS_MACX) && !defined(DISABLE_GUI)
    if (QSysInfo::MacintoshVersion > QSysInfo::MV_10_8) {
        // fix Mac OS X 10.9 (mavericks) font issue
        // https://bugreports.qt-project.org/browse/QTBUG-32789
        QFont::insertSubstitution(".Lucida Grande UI", "Lucida Grande");
    }
#endif
    setApplicationName("qBittorrent");
    initializeTranslation();
#ifndef DISABLE_GUI
#ifdef QBT_USES_QT5
    setAttribute(Qt::AA_UseHighDpiPixmaps, true);  // opt-in to the high DPI pixmap support
#endif // QBT_USES_QT5
    setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
    connect(this, SIGNAL(commitDataRequest(QSessionManager &)), this, SLOT(shutdownCleanup(QSessionManager &)), Qt::DirectConnection);
#endif // Q_OS_WIN
#endif // DISABLE_GUI

    connect(this, SIGNAL(messageReceived(const QString &)), SLOT(processMessage(const QString &)));
    connect(this, SIGNAL(aboutToQuit()), SLOT(cleanup()));

    if (isFileLoggerEnabled())
        m_fileLogger = new FileLogger(fileLoggerPath(), isFileLoggerBackup(), fileLoggerMaxSize(), isFileLoggerDeleteOld(), fileLoggerAge(), static_cast<FileLogger::FileLogAgeType>(fileLoggerAgeType()));

    Logger::instance()->addMessage(tr("qBittorrent %1 started", "qBittorrent v3.2.0alpha started").arg(VERSION));
}

#ifndef DISABLE_GUI
QPointer<MainWindow> Application::mainWindow()
{
    return m_window;
}
#endif

bool Application::isFileLoggerEnabled() const
{
    return settings()->loadValue(KEY_FILELOGGER_ENABLED, true).toBool();
}

void Application::setFileLoggerEnabled(bool value)
{
    if (value && !m_fileLogger)
        m_fileLogger = new FileLogger(fileLoggerPath(), isFileLoggerBackup(), fileLoggerMaxSize(), isFileLoggerDeleteOld(), fileLoggerAge(), static_cast<FileLogger::FileLogAgeType>(fileLoggerAgeType()));
    else if (!value)
        delete m_fileLogger;
    settings()->storeValue(KEY_FILELOGGER_ENABLED, value);
}

QString Application::fileLoggerPath() const
{
    return settings()->loadValue(KEY_FILELOGGER_PATH, QVariant(Utils::Fs::QDesktopServicesDataLocation() + LOG_FOLDER)).toString();
}

void Application::setFileLoggerPath(const QString &value)
{
    if (m_fileLogger)
        m_fileLogger->changePath(value);
    settings()->storeValue(KEY_FILELOGGER_PATH, value);
}

bool Application::isFileLoggerBackup() const
{
    return settings()->loadValue(KEY_FILELOGGER_BACKUP, true).toBool();
}

void Application::setFileLoggerBackup(bool value)
{
    if (m_fileLogger)
        m_fileLogger->setBackup(value);
    settings()->storeValue(KEY_FILELOGGER_BACKUP, value);
}

bool Application::isFileLoggerDeleteOld() const
{
    return settings()->loadValue(KEY_FILELOGGER_DELETEOLD, true).toBool();
}

void Application::setFileLoggerDeleteOld(bool value)
{
    if (value && m_fileLogger)
        m_fileLogger->deleteOld(fileLoggerAge(), static_cast<FileLogger::FileLogAgeType>(fileLoggerAgeType()));
    settings()->storeValue(KEY_FILELOGGER_DELETEOLD, value);
}

int Application::fileLoggerMaxSize() const
{
    int val = settings()->loadValue(KEY_FILELOGGER_MAXSIZE, 10).toInt();
    if (val < 1)
        return 1;
    if (val > 1000)
        return 1000;
    return val;
}

void Application::setFileLoggerMaxSize(const int value)
{
    if (m_fileLogger)
        m_fileLogger->setMaxSize(value);
    settings()->storeValue(KEY_FILELOGGER_MAXSIZE, std::min(std::max(value, 1), 1000));
}

int Application::fileLoggerAge() const
{
    int val = settings()->loadValue(KEY_FILELOGGER_AGE, 6).toInt();
    if (val < 1)
        return 1;
    if (val > 365)
        return 365;
    return val;
}

void Application::setFileLoggerAge(const int value)
{
    settings()->storeValue(KEY_FILELOGGER_AGE, std::min(std::max(value, 1), 365));
}

int Application::fileLoggerAgeType() const
{
    int val = settings()->loadValue(KEY_FILELOGGER_AGETYPE, 1).toInt();
    return (val < 0 || val > 2) ? 1 : val;
}

void Application::setFileLoggerAgeType(const int value)
{
    settings()->storeValue(KEY_FILELOGGER_AGETYPE, (value < 0 || value > 2) ? 1 : value);
}

void Application::processMessage(const QString &message)
{
    QStringList params = message.split(QLatin1String(PARAMS_SEPARATOR), QString::SkipEmptyParts);
    // If Application is not running (i.e., other
    // components are not ready) store params
    if (m_running)
        processParams(params);
    else
        m_paramsQueue.append(params);
}

void Application::runExternalProgram(BitTorrent::TorrentHandle *const torrent) const
{
    QString program = Preferences::instance()->getAutoRunProgram();
    program.replace("%N", torrent->name());
    program.replace("%L", torrent->category());
    program.replace("%F", Utils::Fs::toNativePath(torrent->contentPath()));
    program.replace("%R", Utils::Fs::toNativePath(torrent->rootPath()));
    program.replace("%D", Utils::Fs::toNativePath(torrent->savePath()));
    program.replace("%C", QString::number(torrent->filesCount()));
    program.replace("%Z", QString::number(torrent->totalSize()));
    program.replace("%T", torrent->currentTracker());
    program.replace("%I", torrent->hash());

    Logger *logger = Logger::instance();
    logger->addMessage(tr("Torrent: %1, running external program, command: %2").arg(torrent->name()).arg(program));

#if defined(Q_OS_UNIX)
    QProcess::startDetached(QLatin1String("/bin/sh"), {QLatin1String("-c"), program});
#elif defined(Q_OS_WIN)  // test cmd: `echo "%F" > "c:\ab ba.txt"`
    static const QString cmdPath = []() -> QString {
        WCHAR systemPath[64] = {0};
        GetSystemDirectoryW(systemPath, sizeof(systemPath) / sizeof(WCHAR));
        return QString::fromWCharArray(systemPath) + QLatin1String("\\cmd.exe /C ");
    }();
    program.prepend(QLatin1String("\"")).append(QLatin1String("\""));
    program.prepend(cmdPath);
    const uint cmdMaxLength = 32768;  // max length (incl. terminate char) for `lpCommandLine` in `CreateProcessW()`
    if ((program.size() + 1) > cmdMaxLength) {
        logger->addMessage(tr("Torrent: %1, run external program command too long (length > %2), execution failed.").arg(torrent->name()).arg(cmdMaxLength), Log::CRITICAL);
        return;
    }

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    WCHAR *arg = new WCHAR[program.size() + 1];
    program.toWCharArray(arg);
    arg[program.size()] = L'\0';
    if (CreateProcessW(NULL, arg, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    delete[] arg;
#else
    QProcess::startDetached(program);
#endif
}

void Application::sendNotificationEmail(BitTorrent::TorrentHandle *const torrent)
{
    // Prepare mail content
    QString content = QObject::tr("Torrent name: %1").arg(torrent->name()) + "\n";
    content += QObject::tr("Torrent size: %1").arg(Utils::Misc::friendlyUnit(torrent->wantedSize())) + "\n";
    content += QObject::tr("Save path: %1").arg(torrent->savePath()) + "\n\n";
    content += QObject::tr("The torrent was downloaded in %1.",
                  "The torrent was downloaded in 1 hour and 20 seconds")
            .arg(Utils::Misc::userFriendlyDuration(torrent->activeTime())) + "\n\n\n";
    content += QObject::tr("Thank you for using qBittorrent.") + "\n";

    // Send the notification email
    Net::Smtp *sender = new Net::Smtp;
    sender->sendMail("notification@qbittorrent.org",
                     Preferences::instance()->getMailNotificationEmail(),
                     QObject::tr("[qBittorrent] '%1' has finished downloading").arg(torrent->name()),
                     content);
}

void Application::torrentFinished(BitTorrent::TorrentHandle *const torrent)
{
    Preferences *const pref = Preferences::instance();

    // AutoRun program
    if (pref->isAutoRunEnabled())
        runExternalProgram(torrent);

    // Mail notification
    if (pref->isMailNotificationEnabled()) {
        Logger::instance()->addMessage(tr("Torrent: %1, sending mail notification").arg(torrent->name()));
        sendNotificationEmail(torrent);
    }
}

void Application::allTorrentsFinished()
{
    Preferences *const pref = Preferences::instance();
    bool isExit = pref->shutdownqBTWhenDownloadsComplete();
    bool isShutdown = pref->shutdownWhenDownloadsComplete();
    bool isSuspend = pref->suspendWhenDownloadsComplete();
    bool isHibernate = pref->hibernateWhenDownloadsComplete();

    bool haveAction = isExit || isShutdown || isSuspend || isHibernate;
    if (!haveAction) return;

    ShutdownDialogAction action = ShutdownDialogAction::Exit;
    if (isSuspend)
        action = ShutdownDialogAction::Suspend;
    else if (isHibernate)
        action = ShutdownDialogAction::Hibernate;
    else if (isShutdown)
        action = ShutdownDialogAction::Shutdown;

#ifndef DISABLE_GUI
    // ask confirm
    if ((action == ShutdownDialogAction::Exit) && (pref->dontConfirmAutoExit())) {
        // do nothing & skip confirm
    }
    else {
        if (!ShutdownConfirmDlg::askForConfirmation(action)) return;
    }
#endif // DISABLE_GUI

    // Actually shut down
    if (action != ShutdownDialogAction::Exit) {
        qDebug("Preparing for auto-shutdown because all downloads are complete!");
        // Disabling it for next time
        pref->setShutdownWhenDownloadsComplete(false);
        pref->setSuspendWhenDownloadsComplete(false);
        pref->setHibernateWhenDownloadsComplete(false);
        // Make sure preferences are synced before exiting
        m_shutdownAct = action;
    }

    qDebug("Exiting the application");
    exit();
}

bool Application::sendParams(const QStringList &params)
{
    return sendMessage(params.join(QLatin1String(PARAMS_SEPARATOR)));
}

// As program parameters, we can get paths or urls.
// This function parse the parameters and call
// the right addTorrent function, considering
// the parameter type.
void Application::processParams(const QStringList &params)
{
#ifndef DISABLE_GUI
    if (params.isEmpty()) {
        m_window->activate(); // show UI
        return;
    }
#endif

    foreach (QString param, params) {
        param = param.trimmed();
#ifndef DISABLE_GUI
        if (AddNewTorrentDialog::isEnabled())
            AddNewTorrentDialog::show(param, m_window);
        else
#endif
            BitTorrent::Session::instance()->addTorrent(param);
    }
}

int Application::exec(const QStringList &params)
{
    Net::DownloadManager::initInstance();
#ifdef DISABLE_GUI
    IconProvider::initInstance();
#else
    GuiIconProvider::initInstance();
#endif

    BitTorrent::Session::initInstance();
    connect(BitTorrent::Session::instance(), SIGNAL(torrentFinished(BitTorrent::TorrentHandle *const)), SLOT(torrentFinished(BitTorrent::TorrentHandle *const)));
    connect(BitTorrent::Session::instance(), SIGNAL(allTorrentsFinished()), SLOT(allTorrentsFinished()), Qt::QueuedConnection);

#ifndef DISABLE_COUNTRIES_RESOLUTION
    Net::GeoIPManager::initInstance();
#endif
    ScanFoldersModel::initInstance(this);

#ifndef DISABLE_WEBUI
    m_webui = new WebUI;
#endif

#ifdef DISABLE_GUI
#ifndef DISABLE_WEBUI
    Preferences* const pref = Preferences::instance();
    // Display some information to the user
    std::cout << std::endl << "******** " << qPrintable(tr("Information")) << " ********" << std::endl;
    std::cout << qPrintable(tr("To control qBittorrent, access the Web UI at http://localhost:%1").arg(QString::number(pref->getWebUiPort()))) << std::endl;
    std::cout << qPrintable(tr("The Web UI administrator user name is: %1").arg(pref->getWebUiUsername())) << std::endl;
    qDebug() << "Password:" << pref->getWebUiPassword();
    if (pref->getWebUiPassword() == "f6fdffe48c908deb0f4c3bd36c032e72") {
        std::cout << qPrintable(tr("The Web UI administrator password is still the default one: %1").arg("adminadmin")) << std::endl;
        std::cout << qPrintable(tr("This is a security risk, please consider changing your password from program preferences.")) << std::endl;
    }
#endif // DISABLE_WEBUI
#else
    m_window = new MainWindow;
#endif // DISABLE_GUI

    m_running = true;
    m_paramsQueue = params + m_paramsQueue;
    if (!m_paramsQueue.isEmpty()) {
        processParams(m_paramsQueue);
        m_paramsQueue.clear();
    }

    return BaseApplication::exec();
}

#ifndef DISABLE_GUI
#ifdef Q_OS_WIN
bool Application::isRunning()
{
    bool running = BaseApplication::isRunning();
    QSharedMemory *sharedMem = new QSharedMemory(id() + QLatin1String("-shared-memory-key"), this);
    if (!running) {
        // First instance creates shared memory and store PID
        if (sharedMem->create(sizeof(DWORD)) && sharedMem->lock()) {
            *(static_cast<DWORD*>(sharedMem->data())) = ::GetCurrentProcessId();
            sharedMem->unlock();
        }
    }
    else {
        // Later instances attach to shared memory and retrieve PID
        if (sharedMem->attach() && sharedMem->lock()) {
            ::AllowSetForegroundWindow(*(static_cast<DWORD*>(sharedMem->data())));
            sharedMem->unlock();
        }
    }

    if (!sharedMem->isAttached())
        qWarning() << "Failed to initialize shared memory: " << sharedMem->errorString();

    return running;
}
#endif // Q_OS_WIN

#ifdef Q_OS_MAC
bool Application::event(QEvent *ev)
{
    if (ev->type() == QEvent::FileOpen) {
        QString path = static_cast<QFileOpenEvent *>(ev)->file();
        if (path.isEmpty())
            // Get the url instead
            path = static_cast<QFileOpenEvent *>(ev)->url().toString();
        qDebug("Received a mac file open event: %s", qPrintable(path));
        if (m_running)
            processParams(QStringList(path));
        else
            m_paramsQueue.append(path);
        return true;
    }
    else {
        return BaseApplication::event(ev);
    }
}
#endif // Q_OS_MAC

bool Application::notify(QObject *receiver, QEvent *event)
{
    try {
        return QApplication::notify(receiver, event);
    }
    catch (const std::exception &e) {
        qCritical() << "Exception thrown:" << e.what() << ", receiver: " << receiver->objectName();
        receiver->dumpObjectInfo();
    }

    return false;
}
#endif // DISABLE_GUI

void Application::initializeTranslation()
{
    Preferences* const pref = Preferences::instance();
    // Load translation
    QString locale = pref->getLocale();

    if (locale.isEmpty()) {
        locale = QLocale::system().name();
        pref->setLocale(locale);
    }

    if (m_qtTranslator.load(
#ifdef QBT_USES_QT5
            QString::fromUtf8("qtbase_") + locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath)) ||
        m_qtTranslator.load(
#endif
            QString::fromUtf8("qt_") + locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
            qDebug("Qt %s locale recognized, using translation.", qPrintable(locale));
    }
    else {
        qDebug("Qt %s locale unrecognized, using default (en).", qPrintable(locale));
    }
    installTranslator(&m_qtTranslator);

    if (m_translator.load(QString::fromUtf8(":/lang/qbittorrent_") + locale)) {
        qDebug("%s locale recognized, using translation.", qPrintable(locale));
    }
    else {
        qDebug("%s locale unrecognized, using default (en).", qPrintable(locale));
    }
    installTranslator(&m_translator);

#ifndef DISABLE_GUI
    if (locale.startsWith("ar") || locale.startsWith("he")) {
        qDebug("Right to Left mode");
        setLayoutDirection(Qt::RightToLeft);
    }
    else {
        setLayoutDirection(Qt::LeftToRight);
    }
#endif
}

#if (!defined(DISABLE_GUI) && defined(Q_OS_WIN))
void Application::shutdownCleanup(QSessionManager &manager)
{
    Q_UNUSED(manager);

    // This is only needed for a special case on Windows XP.
    // (but is called for every Windows version)
    // If a process takes too much time to exit during OS
    // shutdown, the OS presents a dialog to the user.
    // That dialog tells the user that qbt is blocking the
    // shutdown, it shows a progress bar and it offers
    // a "Terminate Now" button for the user. However,
    // after the progress bar has reached 100% another button
    // is offered to the user reading "Cancel". With this the
    // user can cancel the **OS** shutdown. If we don't do
    // the cleanup by handling the commitDataRequest() signal
    // and the user clicks "Cancel", it will result in qbt being
    // killed and the shutdown proceeding instead. Apparently
    // aboutToQuit() is emitted too late in the shutdown process.
    cleanup();

    // According to the qt docs we shouldn't call quit() inside a slot.
    // aboutToQuit() is never emitted if the user hits "Cancel" in
    // the above dialog.
    QTimer::singleShot(0, qApp, SLOT(quit()));
}
#endif

void Application::cleanup()
{
#ifndef DISABLE_GUI
#ifdef Q_OS_WIN
    // cleanup() can be called multiple times during shutdown. We only need it once.
    static QAtomicInt alreadyDone;
    if (!alreadyDone.testAndSetAcquire(0, 1))
        return;
#endif // Q_OS_WIN

    // Hide the window and not leave it on screen as
    // unresponsive. Also for Windows take the WinId
    // after it's hidden, because hide() may cause a
    // WinId change.
    m_window->hide();

#ifdef Q_OS_WIN
    typedef BOOL (WINAPI *PSHUTDOWNBRCREATE)(HWND, LPCWSTR);
    PSHUTDOWNBRCREATE shutdownBRCreate = (PSHUTDOWNBRCREATE)::GetProcAddress(::GetModuleHandleW(L"User32.dll"), "ShutdownBlockReasonCreate");
    // Only available on Vista+
    if (shutdownBRCreate)
        shutdownBRCreate((HWND)m_window->effectiveWinId(), tr("Saving torrent progress...").toStdWString().c_str());
#endif // Q_OS_WIN

    // Do manual cleanup in MainWindow to force widgets
    // to save their Preferences, stop all timers and
    // delete as many widgets as possible to leave only
    // a 'shell' MainWindow.
    // We need a valid window handle for Windows Vista+
    // otherwise the system shutdown will continue even
    // though we created a ShutdownBlockReason
    m_window->cleanup();

#endif // DISABLE_GUI

#ifndef DISABLE_WEBUI
    delete m_webui;
#endif

    ScanFoldersModel::freeInstance();
    BitTorrent::Session::freeInstance();
#ifndef DISABLE_COUNTRIES_RESOLUTION
    Net::GeoIPManager::freeInstance();
#endif
    Net::DownloadManager::freeInstance();
    Preferences::freeInstance();
    SettingsStorage::freeInstance();
    delete m_fileLogger;
    Logger::freeInstance();
    IconProvider::freeInstance();

#ifndef DISABLE_GUI
#ifdef Q_OS_WIN
    typedef BOOL (WINAPI *PSHUTDOWNBRDESTROY)(HWND);
    PSHUTDOWNBRDESTROY shutdownBRDestroy = (PSHUTDOWNBRDESTROY)::GetProcAddress(::GetModuleHandleW(L"User32.dll"), "ShutdownBlockReasonDestroy");
    // Only available on Vista+
    if (shutdownBRDestroy)
        shutdownBRDestroy((HWND)m_window->effectiveWinId());
#endif // Q_OS_WIN
    delete m_window;
#endif // DISABLE_GUI

    if (m_shutdownAct != ShutdownDialogAction::Exit) {
        qDebug() << "Sending computer shutdown/suspend/hibernate signal...";
        Utils::Misc::shutdownComputer(m_shutdownAct);
    }
}

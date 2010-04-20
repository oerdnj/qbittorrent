#ifndef FILESYSTEMWATCHER_H
#define FILESYSTEMWATCHER_H

#include <QFileSystemWatcher>

#ifndef Q_WS_WIN
#include <QTimer>
#include <QDir>
#include <QPointer>
#include <QStringList>
#include <QSet>
#include <iostream>
#include <errno.h>
#ifdef Q_WS_MAC
#include <sys/param.h>
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif
#endif

#ifndef CIFS_MAGIC_NUMBER
#define CIFS_MAGIC_NUMBER 0xFF534D42
#endif

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif

/*
 * Subclassing QFileSystemWatcher in order to support Network File
 * System watching (NFS, CIFS) on Linux and Mac OS.
 */
class FileSystemWatcher: public QFileSystemWatcher {
  Q_OBJECT

private:
#ifndef Q_WS_WIN
  QList<QDir> watched_folders;
  QPointer<QTimer> watch_timer;
#endif
  QStringList filters;

#ifndef Q_WS_WIN
protected:
  bool isNetworkFileSystem(QString path) {
    QString file = path;
    if(!file.endsWith(QDir::separator()))
      file += QDir::separator();
    file += ".";
    struct statfs buf;
    if(!statfs(file.toLocal8Bit().constData(), &buf)) {
      return (buf.f_type == (long)CIFS_MAGIC_NUMBER || buf.f_type == (long)NFS_SUPER_MAGIC);
    } else {
      std::cerr << "Error: statfs() call failed for " << qPrintable(file) << ". Supposing it is a local folder..." << std::endl;
      switch(errno) {
      case EACCES:
        std::cerr << "Search permission is denied for a component of the path prefix of the path" << std::endl;
        break;
      case EFAULT:
        std::cerr << "Buf or path points to an invalid address" << std::endl;
        break;
      case EINTR:
        std::cerr << "This call was interrupted by a signal" << std::endl;
        break;
      case EIO:
        std::cerr << "I/O Error" << std::endl;
        break;
      case ELOOP:
        std::cerr << "Too many symlinks" << std::endl;
        break;
      case ENAMETOOLONG:
        std::cerr << "path is too long" << std::endl;
        break;
      case ENOENT:
        std::cerr << "The file referred by path does not exist" << std::endl;
        break;
      case ENOMEM:
        std::cerr << "Insufficient kernel memory" << std::endl;
        break;
      case ENOSYS:
        std::cerr << "The file system does not detect this call" << std::endl;
        break;
      case ENOTDIR:
        std::cerr << "A component of the path is not a directory" << std::endl;
        break;
      case EOVERFLOW:
        std::cerr << "Some values were too large to be represented in the struct" << std::endl;
        break;
      default:
        std::cerr << "Unknown error" << std::endl;
      }
      std::cerr << "Errno: " << errno << std::endl;
      return false;
    }

  }
#endif

public:
  FileSystemWatcher(QObject *parent): QFileSystemWatcher(parent) {
    filters << "*.torrent";
    connect(this, SIGNAL(directoryChanged(QString)), this, SLOT(scanLocalFolder(QString)));
  }

  ~FileSystemWatcher() {
#ifndef Q_WS_WIN
    if(watch_timer)
      delete watch_timer;
#endif
  }

  QStringList directories() const {
    QStringList dirs;
#ifndef Q_WS_WIN
    if(watch_timer) {
      foreach (const QDir &dir, watched_folders)
        dirs << dir.canonicalPath();
    }
#endif
    dirs << QFileSystemWatcher::directories();
    return dirs;
  }

  void addPath(const QString & path) {
#ifndef Q_WS_WIN
    QDir dir(path);
    if (!dir.exists())
      return;
    // Check if the path points to a network file system or not
    if(isNetworkFileSystem(path)) {
      // Network mode
      qDebug("Network folder detected: %s", qPrintable(path));
      qDebug("Using file polling mode instead of inotify...");
      watched_folders << dir;
      // Set up the watch timer
      if (!watch_timer) {
        watch_timer = new QTimer(this);
        connect(watch_timer, SIGNAL(timeout()), this, SLOT(scanNetworkFolders()));
        watch_timer->start(5000); // 5 sec
      }
    } else {
#endif
      // Normal mode
      qDebug("FS Watching is watching %s in normal mode", qPrintable(path));
      QFileSystemWatcher::addPath(path);
      scanLocalFolder(path);
#ifndef Q_WS_WIN
    }
#endif
  }

  void removePath(const QString & path) {
#ifndef Q_WS_WIN
    QDir dir(path);
    for (int i = 0; i < watched_folders.count(); ++i) {
      if (QDir(watched_folders.at(i)) == dir) {
        watched_folders.removeAt(i);
        if (watched_folders.isEmpty())
          delete watch_timer;
        return;
      }
    }
#endif
    // Normal mode
    QFileSystemWatcher::removePath(path);
  }

protected slots:
  void scanLocalFolder(QString path) {
    qDebug("scanLocalFolder(%s) called", qPrintable(path));
    QStringList torrents;
    // Local folders scan
    addTorrentsFromDir(QDir(path), torrents);
    // Report detected torrent files
    if(!torrents.empty()) {
      qDebug("The following files are being reported: %s", qPrintable(torrents.join("\n")));
      emit torrentsAdded(torrents);
    }
  }

  void scanNetworkFolders() {
    qDebug("scanNetworkFolders() called");
    QStringList torrents;
    // Network folders scan
    foreach (const QDir &dir, watched_folders) {
      //qDebug("FSWatcher: Polling manually folder %s", qPrintable(dir.path()));
      addTorrentsFromDir(dir, torrents);
    }
    // Report detected torrent files
    if(!torrents.empty()) {
      qDebug("The following files are being reported: %s", qPrintable(torrents.join("\n")));
      emit torrentsAdded(torrents);
    }
  }

signals:
  void torrentsAdded(QStringList &pathList);

private:
  void addTorrentsFromDir(const QDir &dir, QStringList &torrents) {
    const QStringList &files = dir.entryList(filters, QDir::Files, QDir::Unsorted);
    foreach(const QString &file, files)
      torrents << dir.canonicalPath() + '/' + file;
  }

};

#endif // FILESYSTEMWATCHER_H
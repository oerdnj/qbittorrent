/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2010  Christian Kandeler, Christophe Dumez
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

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

#include "utils/misc.h"
#include "utils/fs.h"
#include "preferences.h"
#include "filesystemwatcher.h"
#include "bittorrent/session.h"
#include "scanfoldersmodel.h"

namespace
{
    const int PathColumn = 0;
    const int DownloadAtTorrentColumn = 1;
    const int DownloadPath = 2;
}

class ScanFoldersModel::PathData
{
public:
    PathData(const QString &path)
        : path(path)
        , downloadAtPath(false)
        , downloadPath(path)
    {
    }

    PathData(const QString &path, bool downloadAtPath, const QString &downloadPath)
        : path(path)
        , downloadAtPath(downloadAtPath)
        , downloadPath(downloadPath)
    {
    }

    const QString path; //watching directory
    bool downloadAtPath; //if TRUE save data to watching directory
    QString downloadPath; //if 'downloadAtPath' FALSE use this path for save data
};

ScanFoldersModel *ScanFoldersModel::m_instance = 0;

bool ScanFoldersModel::initInstance(QObject *parent)
{
    if (!m_instance) {
        m_instance = new ScanFoldersModel(parent);
        return true;
    }

    return false;
}

void ScanFoldersModel::freeInstance()
{
    if (m_instance) {
        delete m_instance;
        m_instance = 0;
    }
}

ScanFoldersModel *ScanFoldersModel::instance()
{
    return m_instance;
}

ScanFoldersModel::ScanFoldersModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_fsWatcher(0)
{
    configure();
    connect(Preferences::instance(), SIGNAL(changed()), SLOT(configure()));
}

ScanFoldersModel::~ScanFoldersModel()
{
    qDeleteAll(m_pathList);
}

int ScanFoldersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_pathList.count();
}

int ScanFoldersModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 3;
}

QVariant ScanFoldersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || (index.row() >= rowCount()))
        return QVariant();

    const PathData *pathData = m_pathList.at(index.row());
    QVariant value;

    switch (index.column()) {
    case PathColumn:
        if (role == Qt::DisplayRole)
            value = Utils::Fs::toNativePath(pathData->path);
        break;
    case DownloadAtTorrentColumn:
        if (role == Qt::CheckStateRole)
            value = pathData->downloadAtPath ? Qt::Checked : Qt::Unchecked;
        break;
    case DownloadPath:
        if (role == Qt::DisplayRole || role == Qt::EditRole || role == Qt::ToolTipRole)
            value = Utils::Fs::toNativePath(pathData->downloadPath);
        break;
    }

    return value;
}

QVariant ScanFoldersModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if ((orientation != Qt::Horizontal) || (role != Qt::DisplayRole) || (section < 0) || (section >= columnCount()))
        return QVariant();

    QVariant title;

    switch (section) {
    case PathColumn:
        title = tr("Watched Folder");
        break;
    case DownloadAtTorrentColumn:
        title = tr("Download here");
        break;
    case DownloadPath:
        title = tr("Download path");
        break;
    }

    return title;
}

Qt::ItemFlags ScanFoldersModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || (index.row() >= rowCount()))
        return QAbstractTableModel::flags(index);

    const PathData *pathData = m_pathList.at(index.row());
    Qt::ItemFlags flags;

    switch (index.column()) {
    case PathColumn:
        flags = QAbstractTableModel::flags(index);
        break;
    case DownloadAtTorrentColumn:
        flags = QAbstractTableModel::flags(index) | Qt::ItemIsUserCheckable;
        break;
    case DownloadPath:
        if (pathData->downloadAtPath == false)
            flags = QAbstractTableModel::flags(index) | Qt::ItemIsEditable | Qt::ItemIsEnabled;
        else
            flags = QAbstractTableModel::flags(index) ^ Qt::ItemIsEnabled; //dont edit DownloadPath if checked 'downloadAtPath'
        break;
    }

    return flags;
}

bool ScanFoldersModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || (index.row() >= rowCount()) || (index.column() > DownloadPath))
        return false;

    bool success = true;

    switch (index.column()) {
    case PathColumn:
        success = false;
        break;
    case DownloadAtTorrentColumn:
        if (role == Qt::CheckStateRole) {
            Q_ASSERT(index.column() == DownloadAtTorrentColumn);
            m_pathList[index.row()]->downloadAtPath = (value.toInt() == Qt::Checked);
            emit dataChanged(index, index);
            success = true;
        }
        break;
    case DownloadPath:
        Q_ASSERT(index.column() == DownloadPath);
        m_pathList[index.row()]->downloadPath = value.toString();
        emit dataChanged(index, index);
        success = true;
        break;
    }

    return success;
}

ScanFoldersModel::PathStatus ScanFoldersModel::addPath(const QString &path, bool downloadAtPath, const QString &downloadPath)
{
    QDir dir(path);
    if (!dir.exists()) return DoesNotExist;
    if (!dir.isReadable()) return CannotRead;

    const QString &canonicalPath = dir.canonicalPath();
    if (findPathData(canonicalPath) != -1) return AlreadyInList;

    if (!m_fsWatcher) {
        m_fsWatcher = new FileSystemWatcher(this);
        connect(m_fsWatcher, SIGNAL(torrentsAdded(const QStringList &)), this, SLOT(addTorrentsToSession(const QStringList  &)));
    }

    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    QString downloadToPath = downloadPath.isEmpty() ? path : downloadPath;
    m_pathList << new PathData(canonicalPath, downloadAtPath, downloadToPath);
    endInsertRows();

    // Start scanning
    m_fsWatcher->addPath(canonicalPath);
    return Ok;
}

void ScanFoldersModel::removePath(int row)
{
    Q_ASSERT((row >= 0) && (row < rowCount()));
    beginRemoveRows(QModelIndex(), row, row);
    m_fsWatcher->removePath(m_pathList.at(row)->path);
    m_pathList.removeAt(row);
    endRemoveRows();
}

bool ScanFoldersModel::removePath(const QString &path)
{
    const int row = findPathData(path);
    if (row == -1) return false;

    removePath(row);
    return true;
}

ScanFoldersModel::PathStatus ScanFoldersModel::setDownloadAtPath(int row, bool downloadAtPath)
{
    Q_ASSERT((row >= 0) && (row < rowCount()));

    bool &oldValue = m_pathList[row]->downloadAtPath;
    if (oldValue != downloadAtPath) {
        if (downloadAtPath) {
            QTemporaryFile testFile(m_pathList[row]->path + "/tmpFile");
            if (!testFile.open()) return CannotWrite;
        }

        oldValue = downloadAtPath;
        const QModelIndex changedIndex = index(row, DownloadAtTorrentColumn);
        emit dataChanged(changedIndex, changedIndex);
    }

    return Ok;
}

bool ScanFoldersModel::downloadInTorrentFolder(const QString &filePath) const
{
    const int row = findPathData(QFileInfo(filePath).dir().path());
    Q_ASSERT(row != -1);
    return m_pathList.at(row)->downloadAtPath;
}

QString ScanFoldersModel::downloadPathTorrentFolder(const QString &filePath) const
{
    const int row = findPathData(QFileInfo(filePath).dir().path());
    Q_ASSERT(row != -1);
    return  m_pathList.at(row)->downloadPath;
}

int ScanFoldersModel::findPathData(const QString &path) const
{
    for (int i = 0; i < m_pathList.count(); ++i)
        if (m_pathList.at(i)->path == Utils::Fs::fromNativePath(path))
            return i;

    return -1;
}

void ScanFoldersModel::makePersistent()
{
    Preferences *const pref = Preferences::instance();
    QStringList paths;
    QList<bool> downloadInFolderInfo;
    QStringList downloadPaths;
    foreach (const PathData *pathData, m_pathList) {
        paths << pathData->path;
        downloadInFolderInfo << pathData->downloadAtPath;
        downloadPaths << pathData->downloadPath;
    }

    pref->setScanDirs(paths);
    pref->setDownloadInScanDirs(downloadInFolderInfo);
    pref->setScanDirsDownloadPaths(downloadPaths);
}

void ScanFoldersModel::configure()
{
    Preferences *const pref = Preferences::instance();

    int i = 0;
    QStringList downloadPaths = pref->getScanDirsDownloadPaths();
    QList<bool> downloadInDirList = pref->getDownloadInScanDirs();
    foreach (const QString &dir, pref->getScanDirs()) {
        bool downloadInDir = downloadInDirList.value(i, true);
        QString downloadPath = downloadPaths.value(i); //empty string if out-of-bounds
        addPath(dir, downloadInDir, downloadPath);
        ++i;
    }
}

void ScanFoldersModel::addTorrentsToSession(const QStringList &pathList)
{
    foreach (const QString &file, pathList) {
        qDebug("File %s added", qPrintable(file));
        if (file.endsWith(".magnet")) {
            QFile f(file);
            if (f.open(QIODevice::ReadOnly)) {
                BitTorrent::Session::instance()->addTorrent(QString::fromLocal8Bit(f.readAll()));
                f.remove();
            }
            else {
                qDebug("Failed to open magnet file: %s", qPrintable(f.errorString()));
            }
        }
        else {
            BitTorrent::AddTorrentParams params;
            if (downloadInTorrentFolder(file))
                params.savePath = QFileInfo(file).dir().path();
            else
                params.savePath = downloadPathTorrentFolder(file); //if empty it will use the default savePath

            BitTorrent::TorrentInfo torrentInfo = BitTorrent::TorrentInfo::loadFromFile(file);
            if (torrentInfo.isValid()) {
                BitTorrent::Session::instance()->addTorrent(torrentInfo, params);
                Utils::Fs::forceRemove(file);
            }
            else {
                qDebug("Ignoring incomplete torrent file: %s", qPrintable(file));
            }
        }
    }
}

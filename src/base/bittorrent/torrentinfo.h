/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015  Vladimir Golovnev <glassez@yandex.ru>
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

#ifndef BITTORRENT_TORRENTINFO_H
#define BITTORRENT_TORRENTINFO_H

#include <QtGlobal>

#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include "base/indexrange.h"

class QString;
class QUrl;
class QDateTime;
class QStringList;
class QByteArray;
template<typename T> class QList;
template<typename T> class QVector;

namespace BitTorrent
{
    class InfoHash;
    class TrackerEntry;

    class TorrentInfo
    {
    public:
#if LIBTORRENT_VERSION_NUM < 10100
        typedef boost::intrusive_ptr<const libtorrent::torrent_info> NativeConstPtr;
        typedef boost::intrusive_ptr<libtorrent::torrent_info> NativePtr;
#else
        typedef boost::shared_ptr<const libtorrent::torrent_info> NativeConstPtr;
        typedef boost::shared_ptr<libtorrent::torrent_info> NativePtr;
#endif

        explicit TorrentInfo(NativeConstPtr nativeInfo = NativeConstPtr());
        TorrentInfo(const TorrentInfo &other);

        static TorrentInfo loadFromFile(const QString &path, QString &error);
        static TorrentInfo loadFromFile(const QString &path);

        TorrentInfo &operator=(const TorrentInfo &other);

        bool isValid() const;
        InfoHash hash() const;
        QString name() const;
        QDateTime creationDate() const;
        QString creator() const;
        QString comment() const;
        bool isPrivate() const;
        qlonglong totalSize() const;
        int filesCount() const;
        int pieceLength() const;
        int pieceLength(int index) const;
        int piecesCount() const;
        QString filePath(int index) const;
        QStringList filePaths() const;
        QString fileName(int index) const;
        QString origFilePath(int index) const;
        qlonglong fileSize(int index) const;
        qlonglong fileOffset(int index) const;
        QList<TrackerEntry> trackers() const;
        QList<QUrl> urlSeeds() const;
        QByteArray metadata() const;
        QStringList filesForPiece(int pieceIndex) const;
        QVector<int> fileIndicesForPiece(int pieceIndex) const;
        QVector<QByteArray> pieceHashes() const;

        using PieceRange = IndexRange<int>;
        // returns pair of the first and the last pieces into which
        // the given file extends (maybe partially).
        PieceRange filePieces(const QString &file) const;
        PieceRange filePieces(int fileIndex) const;

        void renameFile(uint index, const QString &newPath);

        bool hasRootFolder() const;
        void stripRootFolder();

        NativePtr nativeInfo() const;

    private:
        // returns file index or -1 if fileName is not found
        int fileIndex(const QString &fileName) const;
        NativePtr m_nativeInfo;
    };
}

#endif // BITTORRENT_TORRENTINFO_H

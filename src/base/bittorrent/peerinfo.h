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

#ifndef BITTORRENT_PEERINFO_H
#define BITTORRENT_PEERINFO_H

#include <libtorrent/peer_info.hpp>

#include <QHostAddress>
#include <QBitArray>
#include <QCoreApplication>

namespace BitTorrent
{
    class TorrentHandle;

    struct PeerAddress
    {
        QHostAddress ip;
        ushort port;

        PeerAddress();
        PeerAddress(QHostAddress ip, ushort port);
    };

    class PeerInfo
    {
        Q_DECLARE_TR_FUNCTIONS(PeerInfo)

    public:
        PeerInfo(const TorrentHandle *torrent, const libtorrent::peer_info &nativeInfo);

        bool fromDHT() const;
        bool fromPeX() const;
        bool fromLSD() const;

        bool isInteresting() const;
        bool isChocked() const;
        bool isRemoteInterested() const;
        bool isRemoteChocked() const;
        bool isSupportsExtensions() const;
        bool isLocalConnection() const;

        bool isHandshake() const;
        bool isConnecting() const;
        bool isQueued() const;
        bool isOnParole() const;
        bool isSeed() const;

        bool optimisticUnchoke() const;
        bool isSnubbed() const;
        bool isUploadOnly() const;
        bool isEndgameMode() const;
        bool isHolepunched() const;

        bool useI2PSocket() const;
        bool useUTPSocket() const;
        bool useSSLSocket() const;

        bool isRC4Encrypted() const;
        bool isPlaintextEncrypted() const;

        PeerAddress address() const;
        QString client() const;
        qreal progress() const;
        int payloadUpSpeed() const;
        int payloadDownSpeed() const;
        qlonglong totalUpload() const;
        qlonglong totalDownload() const;
        QBitArray pieces() const;
        QString connectionType() const;
        qreal relevance() const;
        QString flags() const;
        QString flagsDescription() const;
#ifndef DISABLE_COUNTRIES_RESOLUTION
        QString country() const;
#endif

    private:
        void calcRelevance(const TorrentHandle *torrent);
        void determineFlags();

        libtorrent::peer_info m_nativeInfo;
        qreal m_relevance;
        QString m_flags;
        QString m_flagsDescription;
    };
}

#endif // BITTORRENT_PEERINFO_H

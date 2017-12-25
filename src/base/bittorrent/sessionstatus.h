/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015, 2017  Vladimir Golovnev <glassez@yandex.ru>
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

#ifndef BITTORRENT_SESSIONSTATUS_H
#define BITTORRENT_SESSIONSTATUS_H

#include <QtGlobal>

namespace BitTorrent
{
    struct SessionStatus
    {
        bool hasIncomingConnections = false;

        // Current download rate for the BT
        // session. Payload means that it only take into
        // account "useful" part of the rate
        quint64 payloadDownloadRate = 0;

        // Current upload rate for the BT
        // session. Payload means that it only take into
        // account "useful" part of the rate
        quint64 payloadUploadRate = 0;

        // Additional download/upload rates
        quint64 uploadRate = 0;
        quint64 downloadRate = 0;
        quint64 ipOverheadUploadRate = 0;
        quint64 ipOverheadDownloadRate = 0;
        quint64 dhtUploadRate = 0;
        quint64 dhtDownloadRate = 0;
        quint64 trackerUploadRate = 0;
        quint64 trackerDownloadRate = 0;

        quint64 totalDownload = 0;
        quint64 totalUpload = 0;
        quint64 totalPayloadDownload = 0;
        quint64 totalPayloadUpload = 0;
        quint64 ipOverheadUpload = 0;
        quint64 ipOverheadDownload = 0;
        quint64 dhtUpload = 0;
        quint64 dhtDownload = 0;
        quint64 trackerUpload = 0;
        quint64 trackerDownload = 0;
        quint64 totalWasted = 0;
        quint64 diskReadQueue = 0;
        quint64 diskWriteQueue = 0;
        quint64 dhtNodes = 0;
        quint64 peersCount = 0;
    };
}

#endif // BITTORRENT_SESSIONSTATUS_H

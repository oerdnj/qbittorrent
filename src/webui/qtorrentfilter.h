/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2014  Vladimir Golovnev <glassez@yandex.ru>
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

#ifndef QTORRENTFILTER_H
#define QTORRENTFILTER_H

#include "qtorrenthandle.h"

class QTorrentFilter
{
public:
    enum
    {
        All,
        Downloading,
        Seeding,
        Completed,
        Paused,
        Resumed,
        Active,
        Inactive
    };

    // label: pass empty string for "no label" or null string (QString()) for "any label"
    QTorrentFilter(QString filter, QString label = QString());
    bool apply(const QTorrentHandle& h) const;

private:
    int type_;
    QString label_;

    bool isTorrentDownloading(const QTorrentHandle &h) const;
    bool isTorrentSeeding(const QTorrentHandle &h) const;
    bool isTorrentCompleted(const QTorrentHandle &h) const;
    bool isTorrentPaused(const QTorrentHandle &h) const;
    bool isTorrentResumed(const QTorrentHandle &h) const;
    bool isTorrentActive(const QTorrentHandle &h) const;
    bool isTorrentInactive(const QTorrentHandle &h) const;
    bool torrentHasLabel(const QTorrentHandle &h) const;
};

#endif // QTORRENTFILTER_H

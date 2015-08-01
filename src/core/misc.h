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

#ifndef MISC_H
#define MISC_H

#include <vector>
#include <QString>
#include <QStringList>
#include <ctime>
#include <QPoint>
#include <QFile>
#include <QDir>
#include <QUrl>
#ifndef DISABLE_GUI
#include <QIcon>
#endif

#include <libtorrent/version.hpp>
#include <libtorrent/error_code.hpp>

namespace libtorrent {
#if LIBTORRENT_VERSION_NUM < 10000
    class big_number;
    typedef big_number sha1_hash;
#else
    class sha1_hash;
#endif
    struct lazy_entry;
}

const qlonglong MAX_ETA = 8640000;
enum shutDownAction { NO_SHUTDOWN, SHUTDOWN_COMPUTER, SUSPEND_COMPUTER, HIBERNATE_COMPUTER };

/*  Miscellaneaous functions that can be useful */
namespace misc
{
    QString toQString(const std::string &str);
    QString toQString(const char* str);
    QString toQString(time_t t, Qt::DateFormat f = Qt::DefaultLocaleLongDate);
    QString toQStringU(const std::string &str);
    QString toQStringU(const char* str);
    QString toQString(const libtorrent::sha1_hash &hash);

#ifndef DISABLE_GUI
    void shutdownComputer(shutDownAction action = SHUTDOWN_COMPUTER);
#endif

    QString parseHtmlLinks(const QString &raw_text);

    bool isUrl(const QString &s);

#ifndef DISABLE_GUI
    // Get screen center
    QPoint screenCenter(QWidget *win);
#endif
    int pythonVersion();
    QString pythonExecutable();
    QString pythonVersionComplete();
    // return best userfriendly storage unit (B, KiB, MiB, GiB, TiB)
    // use Binary prefix standards from IEC 60027-2
    // see http://en.wikipedia.org/wiki/Kilobyte
    // value must be given in bytes
    QString friendlyUnit(qreal val, bool is_speed = false);
    bool isPreviewable(const QString& extension);
    QString magnetUriToName(const QString& magnet_uri);
    QString magnetUriToHash(const QString& magnet_uri);
    QString bcLinkToMagnet(QString bc_link);
    // Take a number of seconds and return an user-friendly
    // time duration like "1d 2h 10m".
    QString userFriendlyDuration(qlonglong seconds);
    QString getUserIDString();

    // Convert functions
    QStringList toStringList(const QList<bool> &l);
    QList<int> intListfromStringList(const QStringList &l);
    QList<bool> boolListfromStringList(const QStringList &l);

    QString accurateDoubleToString(const double &n, const int &precision);

#ifndef DISABLE_GUI
    bool naturalSort(QString left, QString right, bool& result);
#endif

    // Implements constant-time comparison to protect against timing attacks
    // Taken from https://crackstation.net/hashing-security.htm
    bool slowEquals(const QByteArray &a, const QByteArray &b);
    void loadBencodedFile(const QString &filename, std::vector<char> &buffer, libtorrent::lazy_entry &entry, libtorrent::error_code &ec);

    void msleep(unsigned long msecs);
}

#endif

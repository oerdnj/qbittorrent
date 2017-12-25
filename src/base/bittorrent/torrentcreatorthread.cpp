/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2010  Christophe Dumez
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

#include "torrentcreatorthread.h"

#include <fstream>

#include <boost/bind.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/storage.hpp>
#include <libtorrent/torrent_info.hpp>

#include <QFile>

#include "base/utils/fs.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"

namespace
{
    // do not include files and folders whose
    // name starts with a .
    bool fileFilter(const std::string &f)
    {
        return !Utils::Fs::fileName(QString::fromStdString(f)).startsWith('.');
    }
}

namespace libt = libtorrent;
using namespace BitTorrent;

TorrentCreatorThread::TorrentCreatorThread(QObject *parent)
    : QThread(parent)
    , m_private(false)
    , m_pieceSize(0)
{
}

TorrentCreatorThread::~TorrentCreatorThread()
{
    requestInterruption();
    wait();
}

void TorrentCreatorThread::create(const QString &inputPath, const QString &savePath, const QStringList &trackers,
                                  const QStringList &urlSeeds, const QString &comment, bool isPrivate, int pieceSize)
{
    m_inputPath = Utils::Fs::fromNativePath(inputPath);
    m_savePath = Utils::Fs::fromNativePath(savePath);
    if (QFile(m_savePath).exists())
        Utils::Fs::forceRemove(m_savePath);
    m_trackers = trackers;
    m_urlSeeds = urlSeeds;
    m_comment = comment;
    m_private = isPrivate;
    m_pieceSize = pieceSize;

    start();
}

void TorrentCreatorThread::sendProgressSignal(int numHashes, int numPieces)
{
    emit updateProgress(static_cast<int>((numHashes * 100.) / numPieces));
}

void TorrentCreatorThread::run()
{
    emit updateProgress(0);

    QString creator_str("qBittorrent " QBT_VERSION);
    try {
        libt::file_storage fs;
        // Adding files to the torrent
        libt::add_files(fs, Utils::Fs::toNativePath(m_inputPath).toStdString(), fileFilter);

        if (isInterruptionRequested()) return;

        libt::create_torrent t(fs, m_pieceSize);

        // Add url seeds
        foreach (const QString &seed, m_urlSeeds)
            t.add_url_seed(seed.trimmed().toStdString());

        int tier = 0;
        bool newline = false;
        foreach (const QString &tracker, m_trackers) {
            if (tracker.isEmpty()) {
                if (newline)
                    continue;
                ++tier;
                newline = true;
                continue;
            }
            t.add_tracker(tracker.trimmed().toStdString(), tier);
            newline = false;
        }

        if (isInterruptionRequested()) return;

        // calculate the hash for all pieces
        const QString parentPath = Utils::Fs::branchPath(m_inputPath) + "/";
        libt::set_piece_hashes(t, Utils::Fs::toNativePath(parentPath).toStdString(), boost::bind(&TorrentCreatorThread::sendProgressSignal, this, _1, t.num_pieces()));
        // Set qBittorrent as creator and add user comment to
        // torrent_info structure
        t.set_creator(creator_str.toUtf8().constData());
        t.set_comment(m_comment.toUtf8().constData());
        // Is private ?
        t.set_priv(m_private);

        if (isInterruptionRequested()) return;

        // create the torrent and print it to out
        qDebug("Saving to %s", qUtf8Printable(m_savePath));
#ifdef _MSC_VER
        wchar_t *savePathW = new wchar_t[m_savePath.length() + 1];
        int len = Utils::Fs::toNativePath(m_savePath).toWCharArray(savePathW);
        savePathW[len] = L'\0';
        std::ofstream outfile(savePathW, std::ios_base::out | std::ios_base::binary);
        delete[] savePathW;
#else
        std::ofstream outfile(Utils::Fs::toNativePath(m_savePath).toLocal8Bit().constData(), std::ios_base::out | std::ios_base::binary);
#endif
        if (outfile.fail())
            throw std::exception();

        if (isInterruptionRequested()) return;

        libt::bencode(std::ostream_iterator<char>(outfile), t.generate());
        outfile.close();

        emit updateProgress(100);
        emit creationSuccess(m_savePath, parentPath);
    }
    catch (std::exception& e) {
        emit creationFailure(QString::fromStdString(e.what()));
    }
}

int TorrentCreatorThread::calculateTotalPieces(const QString &inputPath, const int pieceSize)
{
    if (inputPath.isEmpty())
        return 0;

    libt::file_storage fs;
    libt::add_files(fs, Utils::Fs::toNativePath(inputPath).toStdString(), fileFilter);
    return libt::create_torrent(fs, pieceSize).num_pieces();
}

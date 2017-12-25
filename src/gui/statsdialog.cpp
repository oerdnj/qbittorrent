/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2013  Nick Tiskov <daymansmail@gmail.com>
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

#include "statsdialog.h"

#include "base/bittorrent/cachestatus.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/sessionstatus.h"
#include "base/bittorrent/torrenthandle.h"
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "ui_statsdialog.h"

StatsDialog::StatsDialog(QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::StatsDialog)
{
    m_ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &StatsDialog::close);

    update();
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::statsUpdated
            , this, &StatsDialog::update);

    show();
}

StatsDialog::~StatsDialog()
{
    delete m_ui;
}

void StatsDialog::update()
{
    const BitTorrent::SessionStatus &ss = BitTorrent::Session::instance()->status();
    const BitTorrent::CacheStatus &cs = BitTorrent::Session::instance()->cacheStatus();

    // Alltime DL/UL
    quint64 atd = BitTorrent::Session::instance()->getAlltimeDL();
    quint64 atu = BitTorrent::Session::instance()->getAlltimeUL();
    m_ui->labelAlltimeDL->setText(Utils::Misc::friendlyUnit(atd));
    m_ui->labelAlltimeUL->setText(Utils::Misc::friendlyUnit(atu));
    // Total waste (this session)
    m_ui->labelWaste->setText(Utils::Misc::friendlyUnit(ss.totalWasted));
    // Global ratio
    m_ui->labelGlobalRatio->setText(
                ((atd > 0) && (atu > 0))
                ? Utils::String::fromDouble(static_cast<qreal>(atu) / atd, 2)
                : "-");
    // Cache hits
    qreal readRatio = cs.readRatio;
    m_ui->labelCacheHits->setText((readRatio >= 0) ? Utils::String::fromDouble(100 * readRatio, 2) : "-");
    // Buffers size
    m_ui->labelTotalBuf->setText(Utils::Misc::friendlyUnit(cs.totalUsedBuffers * 16 * 1024));
    // Disk overload (100%) equivalent
    // From lt manual: disk_write_queue and disk_read_queue are the number of peers currently waiting on a disk write or disk read
    // to complete before it receives or sends any more data on the socket. It's a metric of how disk bound you are.

    // num_peers is not reliable (adds up peers, which didn't even overcome tcp handshake)
    quint32 peers = 0;
    foreach (BitTorrent::TorrentHandle *const torrent, BitTorrent::Session::instance()->torrents())
        peers += torrent->peersCount();

    m_ui->labelWriteStarve->setText(QString("%1%")
                                    .arg(((ss.diskWriteQueue > 0) && (peers > 0))
                                         ? Utils::String::fromDouble((100. * ss.diskWriteQueue) / peers, 2)
                                         : "0"));
    m_ui->labelReadStarve->setText(QString("%1%")
                                   .arg(((ss.diskReadQueue > 0) && (peers > 0))
                                        ? Utils::String::fromDouble((100. * ss.diskReadQueue) / peers, 2)
                                        : "0"));
    // Disk queues
    m_ui->labelQueuedJobs->setText(QString::number(cs.jobQueueLength));
    m_ui->labelJobsTime->setText(tr("%1 ms", "18 milliseconds").arg(cs.averageJobTime));
    m_ui->labelQueuedBytes->setText(Utils::Misc::friendlyUnit(cs.queuedBytes));

    // Total connected peers
    m_ui->labelPeers->setText(QString::number(ss.peersCount));
}

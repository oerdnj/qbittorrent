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

#include "transferlistdelegate.h"

#include <QModelIndex>
#include <QStyleOptionViewItemV2>
#include <QApplication>
#include <QPainter>
#include "base/utils/misc.h"
#include "base/utils/string.h"
#include "torrentmodel.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrenthandle.h"
#include "base/types.h"
#include "base/preferences.h"
#include "base/unicodestrings.h"

#ifdef Q_OS_WIN
#ifndef QBT_USES_QT5
#include <QPlastiqueStyle>
#else
#include <QProxyStyle>
#endif
#endif

TransferListDelegate::TransferListDelegate(QObject *parent)
    : QItemDelegate(parent)
{
}

void TransferListDelegate::paint(QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
    painter->save();
    bool isHideState = true;
    if (Preferences::instance()->getHideZeroComboValues() == 1) {  // paused torrents only
        QModelIndex stateIndex = index.sibling(index.row(), TorrentModel::TR_STATUS);
        if (stateIndex.data().toInt() != BitTorrent::TorrentState::PausedDownloading)
            isHideState = false;
    }
    const bool hideValues = Preferences::instance()->getHideZeroValues() & isHideState;

    QStyleOptionViewItemV2 opt = QItemDelegate::setOptions(index, option);
    QItemDelegate::drawBackground(painter, opt, index);
    switch (index.column()) {
    case TorrentModel::TR_AMOUNT_DOWNLOADED:
    case TorrentModel::TR_AMOUNT_UPLOADED:
    case TorrentModel::TR_AMOUNT_DOWNLOADED_SESSION:
    case TorrentModel::TR_AMOUNT_UPLOADED_SESSION:
    case TorrentModel::TR_AMOUNT_LEFT:
    case TorrentModel::TR_COMPLETED:
    case TorrentModel::TR_SIZE:
    case TorrentModel::TR_TOTAL_SIZE: {
        qlonglong size = index.data().toLongLong();
        if (hideValues && !size)
            break;
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, option.rect, Utils::Misc::friendlyUnit(size));
        break;
    }
    case TorrentModel::TR_ETA: {
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, option.rect, Utils::Misc::userFriendlyDuration(index.data().toLongLong()));
        break;
    }
    case TorrentModel::TR_SEEDS:
    case TorrentModel::TR_PEERS: {
        qlonglong value = index.data().toLongLong();
        qlonglong total = index.data(Qt::UserRole).toLongLong();
        if (hideValues && (!value && !total))
            break;
        QString display = QString::number(value) + " (" + QString::number(total) + ")";
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, opt.rect, display);
        break;
    }
    case TorrentModel::TR_STATUS: {
        const int state = index.data().toInt();
        QString display = getStatusString(state);
        QItemDelegate::drawDisplay(painter, opt, opt.rect, display);
        break;
    }
    case TorrentModel::TR_UPSPEED:
    case TorrentModel::TR_DLSPEED: {
        const qulonglong speed = index.data().toULongLong();
        if (hideValues && !speed)
            break;
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, opt.rect, Utils::Misc::friendlyUnit(speed, true));
        break;
    }
    case TorrentModel::TR_UPLIMIT:
    case TorrentModel::TR_DLLIMIT: {
        const qlonglong limit = index.data().toLongLong();
        if (hideValues && !limit)
            break;
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, opt.rect, limit > 0 ? Utils::Misc::friendlyUnit(limit, true) : QString::fromUtf8(C_INFINITY));
        break;
    }
    case TorrentModel::TR_TIME_ELAPSED: {
        qlonglong elapsedTime = index.data().toLongLong();
        qlonglong seedingTime = index.data(Qt::UserRole).toLongLong();
        QString txt;
        if (seedingTime > 0)
            txt += tr("%1 (seeded for %2)", "e.g. 4m39s (seeded for 3m10s)")
                   .arg(Utils::Misc::userFriendlyDuration(elapsedTime))
                   .arg(Utils::Misc::userFriendlyDuration(seedingTime));
        QItemDelegate::drawDisplay(painter, opt, opt.rect, txt);
        break;
    }
    case TorrentModel::TR_ADD_DATE:
    case TorrentModel::TR_SEED_DATE:
        QItemDelegate::drawDisplay(painter, opt, opt.rect, index.data().toDateTime().toLocalTime().toString(Qt::DefaultLocaleShortDate));
        break;
    case TorrentModel::TR_RATIO_LIMIT:
    case TorrentModel::TR_RATIO: {
        const qreal ratio = index.data().toDouble();
        if (hideValues && (ratio <= 0))
            break;
        QString str = ((ratio == -1) || (ratio > BitTorrent::TorrentHandle::MAX_RATIO)) ? QString::fromUtf8(C_INFINITY) : Utils::String::fromDouble(ratio, 2);
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, opt.rect, str);
        break;
    }
    case TorrentModel::TR_PRIORITY: {
        const int priority = index.data().toInt();
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        if (priority > 0) {
            QItemDelegate::paint(painter, opt, index);
        }
        else {
            QItemDelegate::drawDisplay(painter, opt, opt.rect, "*");
        }
        break;
    }
    case TorrentModel::TR_PROGRESS: {
        QStyleOptionProgressBarV2 newopt;
        qreal progress = index.data().toDouble() * 100.;
        newopt.rect = opt.rect;
        newopt.text = ((progress == 100.0) ? QString("100%") : Utils::String::fromDouble(progress, 1) + "%");
        newopt.progress = (int)progress;
        newopt.maximum = 100;
        newopt.minimum = 0;
        newopt.state |= QStyle::State_Enabled;
        newopt.textVisible = true;
#ifndef Q_OS_WIN
        QApplication::style()->drawControl(QStyle::CE_ProgressBar, &newopt, painter);
#else
        // XXX: To avoid having the progress text on the right of the bar
#ifndef QBT_USES_QT5
        QPlastiqueStyle st;
#else
        QProxyStyle st("fusion");
#endif
        st.drawControl(QStyle::CE_ProgressBar, &newopt, painter, 0);
#endif
        break;
    }
    case TorrentModel::TR_LAST_ACTIVITY: {
        qlonglong elapsed = index.data().toLongLong();
        if (hideValues && ((elapsed < 0) || (elapsed >= MAX_ETA)))
            break;

        QString elapsedString;
        if (elapsed == 0)
            // Show '< 1m ago' when elapsed time is 0
            elapsed = 1;
        else if (elapsed < 0)
            elapsedString = Utils::Misc::userFriendlyDuration(elapsed);
        else
            elapsedString = tr("%1 ago", "e.g.: 1h 20m ago").arg(Utils::Misc::userFriendlyDuration(elapsed));
        opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
        QItemDelegate::drawDisplay(painter, opt, option.rect, elapsedString);
        break;
    }
    default:
        QItemDelegate::paint(painter, option, index);
    }
    painter->restore();
}

QWidget* TransferListDelegate::createEditor(QWidget*, const QStyleOptionViewItem &, const QModelIndex &) const
{
    // No editor here
    return 0;
}

QSize TransferListDelegate::sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const
{
    // Reimplementing sizeHint() because the 'name' column contains text+icon.
    // When that WHOLE column goes out of view(eg user scrolls horizontally)
    // the rows shrink if the text's height is smaller than the icon's height.
    // This happens because icon from the 'name' column is no longer drawn.

    static int nameColHeight = -1;
    if (nameColHeight == -1) {
        QModelIndex nameColumn = index.sibling(index.row(), TorrentModel::TR_NAME);
        nameColHeight = QItemDelegate::sizeHint(option, nameColumn).height();
    }

    QSize size = QItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(nameColHeight, size.height()));
    return size;
}

QString TransferListDelegate::getStatusString(const int state) const
{
    QString str;

    switch (state) {
    case BitTorrent::TorrentState::Downloading:
        str = tr("Downloading");
        break;
    case BitTorrent::TorrentState::StalledDownloading:
        str = tr("Stalled", "Torrent is waiting for download to begin");
        break;
    case BitTorrent::TorrentState::DownloadingMetadata:
        str = tr("Downloading metadata", "used when loading a magnet link");
        break;
    case BitTorrent::TorrentState::ForcedDownloading:
        str = tr("[F] Downloading", "used when the torrent is forced started. You probably shouldn't translate the F.");
        break;
    case BitTorrent::TorrentState::Allocating:
        str = tr("Allocating", "qBittorrent is allocating the files on disk");
        break;
    case BitTorrent::TorrentState::Uploading:
    case BitTorrent::TorrentState::StalledUploading:
        str = tr("Seeding", "Torrent is complete and in upload-only mode");
        break;
    case BitTorrent::TorrentState::ForcedUploading:
        str = tr("[F] Seeding", "used when the torrent is forced started. You probably shouldn't translate the F.");
        break;
    case BitTorrent::TorrentState::QueuedDownloading:
    case BitTorrent::TorrentState::QueuedUploading:
        str = tr("Queued", "i.e. torrent is queued");
        break;
    case BitTorrent::TorrentState::CheckingDownloading:
    case BitTorrent::TorrentState::CheckingUploading:
        str = tr("Checking", "Torrent local data is being checked");
        break;
    case BitTorrent::TorrentState::QueuedForChecking:
        str = tr("Queued for checking", "i.e. torrent is queued for hash checking");
        break;
    case BitTorrent::TorrentState::CheckingResumeData:
        str = tr("Checking resume data", "used when loading the torrents from disk after qbt is launched. It checks the correctness of the .fastresume file. Normally it is completed in a fraction of a second, unless loading many many torrents.");
        break;
    case BitTorrent::TorrentState::PausedDownloading:
        str = tr("Paused");
        break;
    case BitTorrent::TorrentState::PausedUploading:
        str = tr("Completed");
        break;
    case BitTorrent::TorrentState::MissingFiles:
        str = tr("Missing Files");
        break;
    case BitTorrent::TorrentState::Error:
        str = tr("Errored", "torrent status, the torrent has an error");
        break;
    default:
        str = "";
    }

    return str;
}

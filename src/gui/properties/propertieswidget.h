/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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

#ifndef PROPERTIESWIDGET_H
#define PROPERTIESWIDGET_H

#include <QShortcut>
#include <QWidget>

#include "base/bittorrent/torrenthandle.h"

class QAction;
class QPushButton;
class QTimer;
class QTreeView;

class DownloadedPiecesBar;
class LineEdit;
class MainWindow;
class PeerListWidget;
class PieceAvailabilityBar;
class PropListDelegate;
class PropTabBar;
class SpeedWidget;
class torrent_file;
class TorrentContentFilterModel;
class TrackerList;
class TransferListWidget;

namespace Ui
{
    class PropertiesWidget;
}

class PropertiesWidget : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(PropertiesWidget)

public:
    enum SlideState
    {
        REDUCED,
        VISIBLE
    };

    PropertiesWidget(QWidget *parent, MainWindow *mainWindow, TransferListWidget *transferList);
    ~PropertiesWidget();
    BitTorrent::TorrentHandle *getCurrentTorrent() const;
    TrackerList *getTrackerList() const;
    PeerListWidget *getPeerList() const;
    QTreeView *getFilesList() const;
    SpeedWidget *getSpeedWidget() const;

public slots:
    void setVisibility(bool visible);
    void loadDynamicData();
    void clear();
    void readSettings();
    void saveSettings();
    void reloadPreferences();
    void openDoubleClickedFile(const QModelIndex &);
    void loadTrackers(BitTorrent::TorrentHandle *const torrent);

protected:
    QPushButton *getButtonFromIndex(int index);
    void applyPriorities();

protected slots:
    void loadTorrentInfos(BitTorrent::TorrentHandle *const torrent);
    void updateTorrentInfos(BitTorrent::TorrentHandle *const torrent);
    void loadUrlSeeds();
    void askWebSeed();
    void deleteSelectedUrlSeeds();
    void copySelectedWebSeedsToClipboard() const;
    void editWebSeed();
    void displayFilesListMenu(const QPoint &pos);
    void displayWebSeedListMenu(const QPoint &pos);
    void filteredFilesChanged();
    void showPiecesDownloaded(bool show);
    void showPiecesAvailability(bool show);
    void renameSelectedFile();
    void openSelectedFile();

private:
    void openFile(const QModelIndex &index);
    void openFolder(const QModelIndex &index, bool containingFolder);

    Ui::PropertiesWidget *m_ui;
    TransferListWidget *m_transferList;
    MainWindow *m_mainWindow;
    BitTorrent::TorrentHandle *m_torrent;
    QTimer *m_refreshTimer;
    SlideState m_state;
    TorrentContentFilterModel *m_propListModel;
    PropListDelegate *m_propListDelegate;
    PeerListWidget *m_peerList;
    TrackerList *m_trackerList;
    SpeedWidget *m_speedWidget;
    QList<int> m_slideSizes;
    DownloadedPiecesBar *m_downloadedPieces;
    PieceAvailabilityBar *m_piecesAvailability;
    PropTabBar *m_tabBar;
    LineEdit *m_contentFilterLine;
    QShortcut *m_editHotkeyFile;
    QShortcut *m_editHotkeyWeb;
    QShortcut *m_deleteHotkeyWeb;
    QShortcut *m_openHotkeyFile;

private slots:
    void filterText(const QString &filter);
    void updateSavePath(BitTorrent::TorrentHandle *const torrent);
};

#endif // PROPERTIESWIDGET_H

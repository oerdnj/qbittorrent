/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2010  Christophe Dumez, Arnaud Demaiziere
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
 * Contact: chris@qbittorrent.org, arnaud@qbittorrent.org
 */

#ifndef RSSMANAGER_H
#define RSSMANAGER_H

#include <QTimer>
#include <QSharedPointer>

#include "rssfolder.h"

class RssDownloadRuleList;
class RssParser;

class RssManager;
typedef QSharedPointer<RssManager> RssManagerPtr;

class RssManager: public RssFolder {
  Q_OBJECT

public:
  RssManager();
  virtual ~RssManager();

  RssParser* rssParser() const;
  RssDownloadRuleList* downloadRules() const;

public slots:
  void loadStreamList();
  void saveStreamList() const;
  void forwardFeedContentChanged(const QString& url);
  void forwardFeedInfosChanged(const QString& url, const QString& displayName, uint unreadCount);
  void forwardFeedIconChanged(const QString& url, const QString& iconPath);
  void moveFile(const RssFilePtr& file, const RssFolderPtr& destinationFolder);
  void updateRefreshInterval(uint val);

signals:
  void feedContentChanged(const QString& url);
  void feedInfosChanged(const QString& url, const QString& displayName, uint unreadCount);
  void feedIconChanged(const QString& url, const QString& iconPath);

private:
  QTimer m_refreshTimer;
  uint m_refreshInterval;
  RssDownloadRuleList* m_downloadRules;
  RssParser* m_rssParser;
};

#endif // RSSMANAGER_H

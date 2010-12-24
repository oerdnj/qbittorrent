/*
 * Bittorrent Client using Qt4 and libtorrent.
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

#ifndef RSSDOWNLOADRULE_H
#define RSSDOWNLOADRULE_H

#include <QStringList>
#include <QVariantHash>

class RssFeed;

class RssDownloadRule
{

public:
  explicit RssDownloadRule();
  static RssDownloadRule fromOldFormat(const QVariantHash& rule_hash, const QString &feed_url, const QString &rule_name); // Before v2.5.0
  static RssDownloadRule fromNewFormat(const QVariantHash &rule_hash);
  QVariantHash toVariantHash() const;
  bool matches(const QString &article_title) const;
  void setMustContain(const QString &tokens);
  void setMustNotContain(const QString &tokens);
  inline QStringList rssFeeds() const { return m_rssFeeds; }
  inline void setRssFeeds(const QStringList& rss_feeds) { m_rssFeeds = rss_feeds; }
  inline QString name() const { return m_name; }
  inline void setName(const QString &name) { m_name = name; }
  inline QString savePath() const { return m_savePath; }
  void setSavePath(const QString &save_path);
  inline QString label() const { return m_label; }
  inline void setLabel(const QString &_label) { m_label = _label; }
  inline bool isEnabled() const { return m_enabled; }
  inline void setEnabled(bool enable) { m_enabled = enable; }
  inline bool isValid() const { return !m_name.isEmpty(); }
  inline QString mustContain() const { return m_mustContain.join(" "); }
  inline QString mustNotContain() const { return m_mustNotContain.join(" "); }
  QStringList findMatchingArticles(const RssFeed* feed) const;
  // Operators
  bool operator==(const RssDownloadRule &other);

private:
  QString m_name;
  QStringList m_mustContain;
  QStringList m_mustNotContain;
  QString m_savePath;
  QString m_label;
  bool m_enabled;
  QStringList m_rssFeeds;
};

#endif // RSSDOWNLOADRULE_H

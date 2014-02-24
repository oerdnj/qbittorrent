/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2011  Christophe Dumez
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

#include <QMutexLocker>
#include <QList>
#include <QDateTime>
#include <vector>

#include "qbtsession.h"
#include "misc.h"
#include "torrentspeedmonitor.h"
#include "qinisettings.h"

using namespace libtorrent;

template<class T> struct Sample {
  Sample(const T down = 0, const T up = 0) : download(down), upload(up) {}
  T download;
  T upload;
};

class SpeedSample {

public:
  SpeedSample() {}
  void addSample(int speedDL, int speedUL);
  Sample<qreal> average() const;
  void clear();

private:
  static const int max_samples = 30;

private:
  QList<Sample<int> > m_speedSamples;
};

TorrentSpeedMonitor::TorrentSpeedMonitor(QBtSession* session) :
  QThread(session), m_abort(false), m_session(session),
  sessionUL(0), sessionDL(0), lastWrite(0), dirty(false)
{
  connect(m_session, SIGNAL(deletedTorrent(QString)), SLOT(removeSamples(QString)));
  connect(m_session, SIGNAL(pausedTorrent(QTorrentHandle)), SLOT(removeSamples(QTorrentHandle)));
  loadStats();
}

TorrentSpeedMonitor::~TorrentSpeedMonitor() {
  m_abort = true;
  m_abortCond.wakeOne();
  wait();
  if (dirty)
    lastWrite = 0;
  saveStats();
}

void TorrentSpeedMonitor::run()
{
  do {
    m_mutex.lock();
    getSamples();
    saveStats();
    m_abortCond.wait(&m_mutex, 1000);
    m_mutex.unlock();
  } while(!m_abort);
}

void SpeedSample::addSample(int speedDL, int speedUL)
{
  m_speedSamples << Sample<int>(speedDL, speedUL);
  if (m_speedSamples.size() > max_samples)
    m_speedSamples.removeFirst();
}

Sample<qreal> SpeedSample::average() const
{
  if (m_speedSamples.empty())
    return Sample<qreal>();

  qlonglong sumDL = 0;
  qlonglong sumUL = 0;

  foreach (const Sample<int>& s, m_speedSamples) {
    sumDL += s.download;
    sumUL += s.upload;
  }

  const qreal numSamples = m_speedSamples.size();
  return Sample<qreal>(sumDL/numSamples, sumUL/numSamples);
}

void SpeedSample::clear()
{
  m_speedSamples.clear();
}

void TorrentSpeedMonitor::removeSamples(const QString &hash)
{
  m_samples.remove(hash);
}

void TorrentSpeedMonitor::removeSamples(const QTorrentHandle& h) {
  try {
    m_samples.remove(h.hash());
  } catch(invalid_handle&) {}
}

qlonglong TorrentSpeedMonitor::getETA(const QString &hash) const
{
  QMutexLocker locker(&m_mutex);
  QTorrentHandle h = m_session->getTorrentHandle(hash);
  if (h.is_paused() || !m_samples.contains(hash))
    return MAX_ETA;

  const Sample<qreal> speed_average = m_samples[hash].average();

  if (h.is_seed()) {
    if (!speed_average.upload)
      return MAX_ETA;

    bool _unused;
    qreal max_ratio = m_session->getMaxRatioPerTorrent(hash, &_unused);
    if (max_ratio < 0)
      return MAX_ETA;

    libtorrent::size_type realDL = h.all_time_download();
    if (realDL <= 0)
      realDL = h.total_wanted();

    return (realDL * max_ratio - h.all_time_upload()) / speed_average.upload;
  }

  if (!speed_average.download)
    return MAX_ETA;

  return (h.total_wanted() - h.total_wanted_done()) / speed_average.download;
}

quint64 TorrentSpeedMonitor::getAlltimeDL() const {
  QMutexLocker l(&m_mutex);
  return alltimeDL + sessionDL;
}

quint64 TorrentSpeedMonitor::getAlltimeUL() const {
  QMutexLocker l(&m_mutex);
  return alltimeUL + sessionUL;
}

void TorrentSpeedMonitor::getSamples()
{
  const std::vector<torrent_handle> torrents = m_session->getSession()->get_torrents();

  std::vector<torrent_handle>::const_iterator it = torrents.begin();
  std::vector<torrent_handle>::const_iterator itend = torrents.end();
  for ( ; it != itend; ++it) {
    try {
#if LIBTORRENT_VERSION_NUM >= 1600
      torrent_status st = it->status(0x0);
#else
      torrent_status st = it->status();
#endif
      if (!st.paused) {
        int up = st.upload_payload_rate;
        int down = st.download_payload_rate;
        m_samples[misc::toQString(it->info_hash())].addSample(down, up);
      }
    } catch(invalid_handle&) {}
  }
  libtorrent::session_status ss = m_session->getSessionStatus();
  if (ss.total_download > sessionDL) {
    sessionDL = ss.total_download;
    dirty = true;
  }
  if (ss.total_upload > sessionUL) {
    sessionUL = ss.total_upload;
    dirty = true;
  }
}

void TorrentSpeedMonitor::saveStats() const {
  if (!(dirty && (QDateTime::currentMSecsSinceEpoch() - lastWrite >= 15*60*1000) ))
    return;
  QIniSettings s("qBittorrent", "qBittorrent-data");
  QVariantHash v;
  v.insert("AlltimeDL", alltimeDL + sessionDL);
  v.insert("AlltimeUL", alltimeUL + sessionUL);
  s.setValue("Stats/AllStats", v);
  dirty = false;
  lastWrite = QDateTime::currentMSecsSinceEpoch();
}

void TorrentSpeedMonitor::loadStats() {
  // Temp code. Versions v3.1.4 and v3.1.5 saved the data in the qbittorrent.ini file.
  // This code reads the data from there, writes it to the new file, and removes the keys
  // from the old file. This code should be removed after some time has passed.
  // e.g. When we reach v3.3.0
  QIniSettings s_old;
  QIniSettings s("qBittorrent", "qBittorrent-data");
  QVariantHash v;

  // Let's test if the qbittorrent.ini holds the key
  if (s_old.contains("Stats/AllStats")) {
    v = s_old.value("Stats/AllStats").toHash();
    dirty = true;

    // If the user has used qbt > 3.1.5 and then reinstalled/used
    // qbt < 3.1.6, there will be stats in qbittorrent-data.ini too
    // so we need to merge those 2.
    if (s.contains("Stats/AllStats")) {
      QVariantHash tmp = s.value("Stats/AllStats").toHash();
      v["AlltimeDL"] = v["AlltimeDL"].toULongLong() + tmp["AlltimeDL"].toULongLong();
      v["AlltimeUL"] = v["AlltimeUL"].toULongLong() + tmp["AlltimeUL"].toULongLong();
    }
  }
  else
    v = s.value("Stats/AllStats").toHash();

  alltimeDL = v["AlltimeDL"].toULongLong();
  alltimeUL = v["AlltimeUL"].toULongLong();

  if (dirty) {
    saveStats();
    s_old.remove("Stats/AllStats");
  }
}

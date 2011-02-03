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
#include <vector>

#include "qbtsession.h"
#include "misc.h"
#include "torrentspeedmonitor.h"

using namespace libtorrent;

class SpeedSample {

public:
  SpeedSample(){}
  void addSample(int s);
  qreal average() const;
  void clear();

private:
  static const int max_samples = 30;

private:
  QList<int> m_speedSamples;
};

TorrentSpeedMonitor::TorrentSpeedMonitor(QBtSession* session) :
  QThread(session), m_abort(false), m_session(session)
{
  connect(m_session, SIGNAL(deletedTorrent(QString)), SLOT(removeSamples(QString)));
  connect(m_session, SIGNAL(pausedTorrent(QTorrentHandle)), SLOT(removeSamples(QTorrentHandle)));
}

TorrentSpeedMonitor::~TorrentSpeedMonitor() {
  m_abort = true;
  m_abortCond.wakeOne();
  wait();
}

void TorrentSpeedMonitor::run()
{
  do {
    m_mutex.lock();
    getSamples();
    m_abortCond.wait(&m_mutex, 1000);
    m_mutex.unlock();
  } while(!m_abort);
}

void SpeedSample::addSample(int s)
{
  m_speedSamples << s;
  if(m_speedSamples.size() > max_samples)
    m_speedSamples.removeFirst();
}

qreal SpeedSample::average() const
{
  if(m_speedSamples.empty()) return 0;
  qlonglong sum = 0;
  foreach (int s, m_speedSamples) {
    sum += s;
  }
  return sum/static_cast<float>(m_speedSamples.size());
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
  } catch(invalid_handle&){}
}

qlonglong TorrentSpeedMonitor::getETA(const QString &hash) const
{
  QMutexLocker locker(&m_mutex);
  QTorrentHandle h = m_session->getTorrentHandle(hash);
  if(h.is_paused() || !m_samples.contains(hash)) return -1;
  const qreal speed_average = m_samples.value(hash).average();
  if(speed_average == 0) return -1;
  return (h.total_wanted() - h.total_done()) / speed_average;
}

void TorrentSpeedMonitor::getSamples()
{
  const std::vector<torrent_handle> torrents = m_session->getSession()->get_torrents();
  std::vector<torrent_handle>::const_iterator it;
  for(it = torrents.begin(); it != torrents.end(); it++) {
    try {
      if(!it->is_paused())
        m_samples[misc::toQString(it->info_hash())].addSample(it->status().download_payload_rate);
    } catch(invalid_handle&){}
  }
}

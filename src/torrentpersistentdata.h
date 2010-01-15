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

#ifndef TORRENTPERSISTENTDATA_H
#define TORRENTPERSISTENTDATA_H

#include <QSettings>
#include <QVariant>
#include <libtorrent/magnet_uri.hpp>
#include "qtorrenthandle.h"
#include "misc.h"
#include <vector>

#ifdef QT_4_5
#include <QHash>
#else
#include <QMap>
#define QHash QMap
#define toHash toMap
#endif

class TorrentTempData {
public:
  static bool hasTempData(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    return all_data.contains(hash);
  }

  static void deleteTempData(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    if(all_data.contains(hash)) {
      all_data.remove(hash);
      settings.setValue("torrents-tmp", all_data);
    }
  }

  static void setFilesPriority(QString hash,  std::vector<int> pp) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    std::vector<int>::iterator pp_it = pp.begin();
    QVariantList pieces_priority;
    while(pp_it != pp.end()) {
      pieces_priority << *pp_it;
      pp_it++;
    }
    data["files_priority"] = pieces_priority;
    all_data[hash] = data;
    settings.setValue("torrents-tmp", all_data);
  }

  static void setFilesPath(QString hash, QStringList path_list) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["files_path"] = path_list;
    all_data[hash] = data;
    settings.setValue("torrents-tmp", all_data);
  }

  static void setSavePath(QString hash, QString save_path) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["save_path"] = save_path;
    all_data[hash] = data;
    settings.setValue("torrents-tmp", all_data);
  }

  static void setLabel(QString hash, QString label) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    qDebug("Saving label %s to tmp data", label.toLocal8Bit().data());
    data["label"] = label;
    all_data[hash] = data;
    settings.setValue("torrents-tmp", all_data);
  }

  static void setSequential(QString hash, bool sequential) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["sequential"] = sequential;
    all_data[hash] = data;
    settings.setValue("torrents-tmp", all_data);
  }

  static bool isSequential(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    if(data.contains("sequential"))
      return data["sequential"].toBool();
    return false;

  }

#ifdef LIBTORRENT_0_15
  static void setSeedingMode(QString hash,bool seed){
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["seeding"] = seed;
    all_data[hash] = data;
    settings.setValue("torrents-tmp", all_data);
  }

  static bool isSeedingMode(QString hash){
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    if(data.contains("seeding"))
      return data["seeding"].toBool();
    return false;
  }
#endif

  static QString getSavePath(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    if(data.contains("save_path"))
      return data["save_path"].toString();
    qDebug("Warning Temp::getSavePath returns null string!");
    return QString::null;
  }

  static QStringList getFilesPath(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    if(data.contains("files_path"))
      return data["files_path"].toStringList();
    return QStringList();
  }

  static QString getLabel(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    qDebug("Got label %s from tmp data", data.value("label", "").toString().toLocal8Bit().data());
    return data.value("label", "").toString();
  }

  static std::vector<int> getFilesPriority(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents-tmp", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    std::vector<int> fp;
    if(data.contains("files_priority")) {
      QVariantList list_var = data["files_priority"].toList();
      foreach(const QVariant& var, list_var) {
        fp.push_back(var.toInt());
      }
    }
    return fp;
  }
};

class TorrentPersistentData {
public:

  static bool isKnownTorrent(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    return all_data.contains(hash);
  }

  static QStringList knownTorrents() {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    return all_data.keys();
  }

  static void deletePersistentData(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    if(all_data.contains(hash)) {
      all_data.remove(hash);
      settings.setValue("torrents", all_data);
    }
  }

  static void saveTorrentPersistentData(QTorrentHandle h, bool is_magnet = false) {
    Q_ASSERT(h.is_valid());
    qDebug("Saving persistent data for %s", h.hash().toLocal8Bit().data());
    // First, remove temp data
    TorrentTempData::deleteTempData(h.hash());
    Q_ASSERT(!TorrentTempData::hasTempData(h.hash()));
    // Save persistent data
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data;
    data["is_magnet"] = is_magnet;
    if(is_magnet) {
      data["magnet_uri"] = misc::toQString(make_magnet_uri(h.get_torrent_handle()));
    }
    data["seed"] = h.is_seed();
    data["priority"] = h.queue_position();
    data["save_path"] = h.save_path();
    // Save data
    all_data[h.hash()] = data;
    settings.setValue("torrents", all_data);
    qDebug("TorrentPersistentData: Saving save_path %s, hash: %s", h.save_path().toLocal8Bit().data(), h.hash().toLocal8Bit().data());
  }

  // Setters

  static void saveSavePath(QString hash, QString save_path) {
    Q_ASSERT(!hash.isEmpty());
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["save_path"] = save_path;
    all_data[hash] = data;
    settings.setValue("torrents", all_data);
    qDebug("TorrentPersistentData: Saving save_path: %s, hash: %s", save_path.toLocal8Bit().data(), hash.toLocal8Bit().data());
  }

  static void saveLabel(QString hash, QString label) {
    Q_ASSERT(!hash.isEmpty());
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["label"] = label;
    all_data[hash] = data;
    settings.setValue("torrents", all_data);
  }

  static void saveName(QString hash, QString name) {
    Q_ASSERT(!hash.isEmpty());
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    data["name"] = name;
    all_data[hash] = data;
    settings.setValue("torrents", all_data);
  }

  static void savePriority(QTorrentHandle h) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[h.hash()].toHash();
    data["priority"] = h.queue_position();
    all_data[h.hash()] = data;
    settings.setValue("torrents", all_data);
  }

  static void saveSeedStatus(QTorrentHandle h) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[h.hash()].toHash();
    data["seed"] = h.is_seed();
    all_data[h.hash()] = data;
    settings.setValue("torrents", all_data);
  }

  // Getters
  static QString getSavePath(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    //qDebug("TorrentPersistentData: getSavePath %s", data["save_path"].toString().toLocal8Bit().data());
    return data["save_path"].toString();
  }

  static QString getLabel(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    return data.value("label", "").toString();
  }

  static QString getName(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    return data.value("name", "").toString();
  }

  static int getPriority(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    return data["priority"].toInt();
  }

  static bool isSeed(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    return data.value("seed", false).toBool();
  }

  static bool isMagnet(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    return data["is_magnet"].toBool();
  }

  static QString getMagnetUri(QString hash) {
    QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent-resume"));
    QHash<QString, QVariant> all_data = settings.value("torrents", QHash<QString, QVariant>()).toHash();
    QHash<QString, QVariant> data = all_data[hash].toHash();
    Q_ASSERT(data["is_magnet"].toBool());
    return data["magnet_uri"].toString();
  }

};

#undef QHash
#undef toHash

#endif // TORRENTPERSISTENTDATA_H

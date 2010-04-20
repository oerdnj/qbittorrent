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

#ifndef GEOIP_H
#define GEOIP_H

#include <libtorrent/session.hpp>
#include <QString>
#include <QDir>
#include <QIcon>
#include <QFile>
#include "misc.h"

using namespace libtorrent;

class GeoIP {
protected:
#ifdef WITH_GEOIP_EMBEDDED
  static QString geoipFolder(bool embedded=false) {
    if(embedded)
      return ":/geoip/";
    return misc::QDesktopServicesDataLocation()+"geoip"+QDir::separator();
#else
  static QString geoipFolder(bool) {
    if(QFile::exists("/usr/local/share/GeoIP/GeoIP.dat"))
      return "/usr/local/share/GeoIP/";
    if(QFile::exists("/var/lib/GeoIP/GeoIP.dat"))
      return "/var/lib/GeoIP/";
    return "/usr/share/GeoIP/";
#endif
  }

  static QString geoipDBpath(bool embedded=false) {
    return geoipFolder(embedded)+"GeoIP.dat";
  }

#ifdef WITH_GEOIP_EMBEDDED
  static void exportEmbeddedDb() {
    if(!QFile::exists(geoipDBpath(false)) || QFile(geoipDBpath(false)).size() != QFile(geoipDBpath(true)).size()) { // Export is required
      qDebug("A local Geoip database update is required, proceeding...");
      // Create geoip folder is necessary
      QDir gfolder(geoipFolder(false));
      if(!gfolder.exists()) {
        if(!gfolder.mkpath(geoipFolder(false))) return;
      }
      // Remove destination files
      if(QFile::exists(geoipDBpath(false)))
        QFile::remove(geoipDBpath(false));
      // Copy from executable to hard disk
      QFile::copy(geoipDBpath(true), geoipDBpath(false));
      qDebug("Local Geoip database was updated");
    }
  }
#endif

public:
  static void loadDatabase(session *s) {
 #ifdef WITH_GEOIP_EMBEDDED
    exportEmbeddedDb();
#endif
    if(QFile::exists(geoipDBpath(false))) {
      qDebug("Loading GeoIP database from %s...", qPrintable(geoipDBpath(false)));
      if(!s->load_country_db(geoipDBpath(false).toLocal8Bit().constData())) {
        std::cerr << "Failed to load Geoip Database at " << qPrintable(geoipDBpath(false)) << std::endl;
      }
    } else {
      qDebug("ERROR: Impossible to find local Geoip Database");
    }
  }

  // TODO: Support more countries
  // http://www.iso.org/iso/country_codes/iso_3166_code_lists/english_country_names_and_code_elements.htm
  static QIcon CountryISOCodeToIcon(char* iso) {
    switch(iso[0]) {
    case 0:
    case '-':
    case '!':
      //qDebug("Not returning any icon because iso is invalid: %s", iso);
      return QIcon();
    case 'A':
      if(iso[1] == 'U') return QIcon(":/Icons/flags/australia.png");
      if(iso[1] == 'R') return QIcon(":/Icons/flags/argentina.png");
      if(iso[1] == 'T') return QIcon(":/Icons/flags/austria.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/united_arab_emirates.png");
      break;
    case 'B':
      if(iso[1] == 'R') return QIcon(":/Icons/flags/brazil.png");
      if(iso[1] == 'G') return QIcon(":/Icons/flags/bulgaria.png");
      if(iso[1] == 'Y') return QIcon(":/Icons/flags/belarus.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/belgium.png");
      if(iso[1] == 'A') return QIcon(":/Icons/flags/bosnia.png");
      break;
    case 'C':
      if(iso[1] == 'A') return QIcon(":/Icons/flags/canada.png");
      if(iso[1] == 'Z') return QIcon(":/Icons/flags/czech.png");
      if(iso[1] == 'N') return QIcon(":/Icons/flags/china.png");
      if(iso[1] == 'R') return QIcon(":/Icons/flags/costa_rica.png");
      if(iso[1] == 'H') return QIcon(":/Icons/flags/suisse.png");
      break;
    case 'D':
      if(iso[1] == 'E') return QIcon(":/Icons/flags/germany.png");
      if(iso[1] == 'K') return QIcon(":/Icons/flags/denmark.png");
      if(iso[1] == 'Z') return QIcon(":/Icons/flags/algeria.png");
      break;
    case 'E':
      if(iso[1] == 'S') return QIcon(":/Icons/flags/spain.png");
      if(iso[1] == 'G') return QIcon(":/Icons/flags/egypt.png");
      break;
    case 'F':
      if(iso[1] == 'I') return QIcon(":/Icons/flags/finland.png");
      if(iso[1] == 'R') return QIcon(":/Icons/flags/france.png");
      break;
    case 'G':
      if(iso[1] == 'B') return QIcon(":/Icons/flags/united_kingdom.png");
      if(iso[1] == 'R') return QIcon(":/Icons/flags/greece.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/georgia.png");
      break;
    case 'H':
      if(iso[1] == 'U') return QIcon(":/Icons/flags/hungary.png");
      if(iso[1] == 'K') return QIcon(":/Icons/flags/china.png");
      if(iso[1] == 'R') return QIcon(":/Icons/flags/croatia.png");
      break;
    case 'I':
      if(iso[1] == 'T') return QIcon(":/Icons/flags/italy.png");
      if(iso[1] == 'N') return QIcon(":/Icons/flags/india.png");
      if(iso[1] == 'L') return QIcon(":/Icons/flags/israel.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/ireland.png");
      if(iso[1] == 'S') return QIcon(":/Icons/flags/iceland.png");
      if(iso[1] == 'D') return QIcon(":/Icons/flags/indonesia.png");
      break;
    case 'J':
      if(iso[1] == 'P') return QIcon(":/Icons/flags/japan.png");
      break;
    case 'K':
      if(iso[1] == 'R') return QIcon(":/Icons/flags/south_korea.png");
      break;
    case 'L':
      if(iso[1] == 'U') return QIcon(":/Icons/flags/luxembourg.png");
      break;
    case 'M':
      if(iso[1] == 'Y') return QIcon(":/Icons/flags/malaysia.png");
      if(iso[1] == 'X') return QIcon(":/Icons/flags/mexico.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/serbia.png");
      if(iso[1] == 'A') return QIcon(":/Icons/flags/morocco.png");
      break;
    case 'N':
      if(iso[1] == 'L') return QIcon(":/Icons/flags/netherlands.png");
      if(iso[1] == 'O') return QIcon(":/Icons/flags/norway.png");
      if(iso[1] == 'Z') return QIcon(":/Icons/flags/newzealand.png");
      break;
    case 'P':
      if(iso[1] == 'T') return QIcon(":/Icons/flags/portugal.png");
      if(iso[1] == 'L') return QIcon(":/Icons/flags/poland.png");
      if(iso[1] == 'K') return QIcon(":/Icons/flags/pakistan.png");
      if(iso[1] == 'H') return QIcon(":/Icons/flags/philippines.png");
      break;
    case 'R':
      if(iso[1] == 'U') return QIcon(":/Icons/flags/russia.png");
      if(iso[1] == 'O') return QIcon(":/Icons/flags/romania.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/france.png");
      if(iso[1] == 'S') return QIcon(":/Icons/flags/serbia.png");
      break;
    case 'S':
      if(iso[1] == 'A') return QIcon(":/Icons/flags/saoudi_arabia.png");
      if(iso[1] == 'E') return QIcon(":/Icons/flags/sweden.png");
      if(iso[1] == 'K') return QIcon(":/Icons/flags/slovakia.png");
      if(iso[1] == 'G') return QIcon(":/Icons/flags/singapore.png");
      if(iso[1] == 'I') return QIcon(":/Icons/flags/slovenia.png");
      break;
    case 'T':
      if(iso[1] == 'W') return QIcon(":/Icons/flags/china.png");
      if(iso[1] == 'R') return QIcon(":/Icons/flags/turkey.png");
      if(iso[1] == 'H') return QIcon(":/Icons/flags/thailand.png");
      break;
    case 'U':
      if(iso[1] == 'S') return QIcon(":/Icons/flags/usa.png");
      if(iso[1] == 'M') return QIcon(":/Icons/flags/usa.png");
      if(iso[1] == 'A') return QIcon(":/Icons/flags/ukraine.png");
      break;
    case 'Z':
      if(iso[1] == 'A') return QIcon(":/Icons/flags/south_africa.png");
      break;
    }
    qDebug("Unrecognized country code: %c%c", iso[0], iso[1]);
    return QIcon();
  }
};

#endif // GEOIP_H

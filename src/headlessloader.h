/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez, Frédéric Lassabe
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

#ifndef HEADLESSLOADER_H
#define HEADLESSLOADER_H

#include <QObject>
#include <QCoreApplication>
#include "preferences.h"
#include "bittorrent.h"

class HeadlessLoader: public QObject {
  Q_OBJECT

public:
  HeadlessLoader(QStringList torrentCmdLine) {
    // Enable Web UI
    Preferences::setWebUiEnabled(true);
    // Instanciate Bittorrent Object
    BTSession = new Bittorrent();
    connect(BTSession, SIGNAL(newConsoleMessage(QString)), this, SLOT(displayConsoleMessage(QString)));
    // Resume unfinished torrents
    BTSession->startUpTorrents();
    // Process command line parameters
    processParams(torrentCmdLine);
    // Display some information to the user
    std::cout << std::endl << "******** " << qPrintable(tr("Information")) << " ********" << std::endl;
    std::cout << qPrintable(tr("To control qBittorrent, access the Web UI at http://localhost:%1").arg(QString::number(Preferences::getWebUiPort()))) << std::endl;
    std::cout << qPrintable(tr("The Web UI administrator user name is: %1").arg(Preferences::getWebUiUsername())) << std::endl;
    if(Preferences::getWebUiPassword() == "f6fdffe48c908deb0f4c3bd36c032e72") {
      std::cout << qPrintable(tr("The Web UI administrator password is still the default one: %1").arg("adminadmin")) << std::endl;
      std::cout << qPrintable(tr("This is a security risk, please consider changing your password from program preferences.")) << std::endl;
    }
  }

  ~HeadlessLoader() {
    delete BTSession;
  }

public slots:
  // Call this function to exit qBittorrent headless loader
  // and return to prompt (object will be deleted by main)
  void exit() {
    qApp->quit();
  }

  void displayConsoleMessage(QString msg) {
    std::cout << qPrintable(msg) << std::endl;
  }

  void processParams(const QString& params_str) {
    processParams(params_str.split(" ", QString::SkipEmptyParts));
  }

  // As program parameters, we can get paths or urls.
  // This function parse the parameters and call
  // the right addTorrent function, considering
  // the parameter type.
  void processParams(const QStringList& params) {
    foreach(QString param, params) {
      param = param.trimmed();
      if(param.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive) || param.startsWith(QString::fromUtf8("ftp://"), Qt::CaseInsensitive) || param.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive)) {
        BTSession->downloadFromUrl(param);
      }else{
        if(param.startsWith("bc://bt/", Qt::CaseInsensitive)) {
          qDebug("Converting bc link to magnet link");
          param = misc::bcLinkToMagnet(param);
        }
        if(param.startsWith("magnet:", Qt::CaseInsensitive)) {
          BTSession->addMagnetUri(param);
        } else {
          BTSession->addTorrent(param);
        }
      }
    }
  }

private:
  Bittorrent *BTSession;

};

#endif

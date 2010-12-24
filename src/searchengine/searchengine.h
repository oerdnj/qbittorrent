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

#ifndef SEARCH_H
#define SEARCH_H

#include <QProcess>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QStringListModel>
#include "ui_search.h"
#include "engineselectdlg.h"
#include "searchtab.h"
#include "supportedengines.h"

class downloadThread;
class QTimer;
class SearchEngine;
class MainWindow;

class SearchEngine : public QWidget, public Ui::search_engine{
  Q_OBJECT
  Q_DISABLE_COPY(SearchEngine)

public:
  SearchEngine(MainWindow *mp_mainWindow);
  ~SearchEngine();
  QString selectedCategory() const;

  static float getPluginVersion(QString filePath) {
    QFile plugin(filePath);
    if(!plugin.exists()){
      qDebug("%s plugin does not exist, returning 0.0", qPrintable(filePath));
      return 0.0;
    }
    if(!plugin.open(QIODevice::ReadOnly | QIODevice::Text)){
      return 0.0;
    }
    float version = 0.0;
    while (!plugin.atEnd()){
      QByteArray line = plugin.readLine();
      if(line.startsWith("#VERSION: ")){
        line = line.split(' ').last().trimmed();
        version = line.toFloat();
        qDebug("plugin %s version: %.2f", qPrintable(filePath), version);
        break;
      }
    }
    return version;
  }

public slots:
  void on_download_button_clicked();
  void downloadTorrent(QString engine_url, QString torrent_url);
  void giveFocusToSearchInput();

protected slots:
  // Search slots
  void tab_changed(int);//to prevent the use of the download button when the tab is empty
  void on_search_button_clicked();
#if QT_VERSION < 0x040500
  void closeTab_button_clicked();
#else
  void closeTab(int index);
#endif
  void appendSearchResult(QString line);
  void searchFinished(int exitcode,QProcess::ExitStatus);
  void readSearchOutput();
  void searchStarted();
  void startSearchHistory();
  void updateNova();
  void saveSearchHistory();
  void on_enginesButton_clicked();
  void propagateSectionResized(int index, int oldsize , int newsize);
  void saveResultsColumnsWidth();
  void downloadFinished(int exitcode, QProcess::ExitStatus);
  void displayPatternContextMenu(QPoint);
  void createCompleter();
  void fillCatCombobox();
  void searchTextEdited(QString);
#ifdef Q_WS_WIN
  bool addPythonPathToEnv();
  void installPython();
  void pythonDownloadSuccess(QString url, QString file_path);
  void pythonDownloadFailure(QString url, QString error);
#endif

private:
  // Search related
  QProcess *searchProcess;
  QList<QProcess*> downloaders;
  bool search_stopped;
  bool no_search_results;
  QByteArray search_result_line_truncated;
  unsigned long nb_search_results;
  QPointer<QCompleter> searchCompleter;
  QStringListModel searchHistory;
  SupportedEngines *supported_engines;
  QTimer *searchTimeout;
  QPointer<SearchTab> currentSearchTab;
#if QT_VERSION < 0x040500
  QPushButton *closeTab_button;
#endif
  QList<QPointer<SearchTab> > all_tab; // To store all tabs
  const SearchCategories full_cat_names;
  MainWindow *mp_mainWindow;
#ifdef Q_WS_WIN
  bool has_python;
#endif
};

#endif

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

#ifndef ENGINE_SELECT_DLG_H
#define ENGINE_SELECT_DLG_H

#include "ui_engineselect.h"
#include "supportedengines.h"

QT_BEGIN_NAMESPACE
class QDropEvent;
QT_END_NAMESPACE

class engineSelectDlg : public QDialog, public Ui::engineSelect{
  Q_OBJECT

  private:
    void downloadFromUrl(const QString &url);

    SupportedEngines *supported_engines;
    const QString m_updateUrl;

  public:
    engineSelectDlg(QWidget *parent, SupportedEngines *supported_engines);
    ~engineSelectDlg();
    QList<QTreeWidgetItem*> findItemsWithUrl(QString url);
    QTreeWidgetItem* findItemWithID(QString id);

  protected:
    bool parseVersionsFile(QString versions_file);
    bool isUpdateNeeded(QString plugin_name, qreal new_version) const;

  signals:
    void enginesChanged();

  protected slots:
    void on_closeButton_clicked();
    void loadSupportedSearchEngines();
    void addNewEngine(QString engine_name);
    void toggleEngineState(QTreeWidgetItem*, int);
    void setRowColor(int row, QString color);
    void processDownloadedFile(const QString &url, QString filePath);
    void handleDownloadFailure(const QString &url, const QString &reason);
    void displayContextMenu(const QPoint& pos);
    void enableSelection(bool enable);
    void on_actionUninstall_triggered();
    void on_updateButton_clicked();
    void on_installButton_clicked();
    void dropEvent(QDropEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void installPlugin(QString plugin_path, QString plugin_name);
    void askForLocalPlugin();
    void askForPluginUrl();
};

#endif

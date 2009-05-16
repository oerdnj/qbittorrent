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

#ifndef DOWNLOADTHREAD_H
#define DOWNLOADTHREAD_H

#include <QThread>
#include <QFile>
#include <QTemporaryFile>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QStringList>
#include <curl/curl.h>
#include <QQueue>

class subDownloadThread : public QThread {
  Q_OBJECT
  private:
    QString url;
    bool abort;

  public:
    subDownloadThread(QObject *parent, QString url);
    ~subDownloadThread();
    QString errorCodeToString(CURLcode status);

  signals:
    // For subthreads
    void downloadFinishedST(subDownloadThread* st, QString url, QString file_path);
    void downloadFailureST(subDownloadThread* st, QString url, QString reason);

  protected:
    void run();
};

class downloadThread : public QThread {
  Q_OBJECT

  private:
    QQueue<QString> urls_queue;
    QMutex mutex;
    QWaitCondition condition;
    bool abort;
    QList<subDownloadThread*> subThreads;

  signals:
    void downloadFinished(QString url, QString file_path);
    void downloadFailure(QString url, QString reason);

  public:
    downloadThread(QObject* parent);

    ~downloadThread();

    void downloadUrl(QString url);
    void setProxy(QString IP, int port, QString username, QString password);

  protected:
    void run();

  protected slots:
    void propagateDownloadedFile(subDownloadThread* st, QString url, QString path);
    void propagateDownloadFailure(subDownloadThread* st, QString url, QString reason);
};

#endif

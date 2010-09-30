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

#include <QTemporaryFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QNetworkCookie>
#include <QNetworkCookieJar>

#include "downloadthread.h"
#include "preferences.h"
#include "qinisettings.h"

enum ProxyType {HTTP=1, SOCKS5=2, HTTP_PW=3, SOCKS5_PW=4, SOCKS4=5};

/** Download Thread **/

downloadThread::downloadThread(QObject* parent) : QObject(parent) {
  connect(&networkManager, SIGNAL(finished (QNetworkReply*)), this, SLOT(processDlFinished(QNetworkReply*)));
#ifndef QT_NO_OPENSSL
  connect(&networkManager, SIGNAL(sslErrors(QNetworkReply*,QList<QSslError>)), this, SLOT(ignoreSslErrors(QNetworkReply*,QList<QSslError>)));
#endif
}

void downloadThread::processDlFinished(QNetworkReply* reply) {
  QString url = reply->url().toEncoded().data();
  qDebug("Download finished: %s", qPrintable(url));
  if(reply->error() != QNetworkReply::NoError) {
    // Failure
    qDebug("Download failure (%s), reason: %s", qPrintable(url), qPrintable(errorCodeToString(reply->error())));
    emit downloadFailure(url, errorCodeToString(reply->error()));
  } else {
    QVariant redirection = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if(redirection.isValid()) {
      // We should redirect
      qDebug("Redirecting from %s to %s", qPrintable(url), qPrintable(redirection.toUrl().toString()));
      redirect_mapping.insert(redirection.toUrl().toString(), url);
      downloadUrl(redirection.toUrl().toString());
      return;
    }
    // Checking if it was redirecting, restoring initial URL
    if(redirect_mapping.contains(url)) {
      url = redirect_mapping.take(url);
    }
    // Success
    QString filePath;
    QTemporaryFile *tmpfile = new QTemporaryFile;
    tmpfile->setAutoRemove(false);
    if (tmpfile->open()) {
      filePath = tmpfile->fileName();
      qDebug("Temporary filename is: %s", qPrintable(filePath));
      if(reply->open(QIODevice::ReadOnly)) {
        // TODO: Support GZIP compression
        tmpfile->write(reply->readAll());
        reply->close();
        tmpfile->close();
        delete tmpfile;
        // Send finished signal
        emit downloadFinished(url, filePath);
      } else {
        // Error when reading the request
        tmpfile->close();
        delete tmpfile;
        emit downloadFailure(url, tr("I/O Error"));
      }
    } else {
      delete tmpfile;
      emit downloadFailure(url, tr("I/O Error"));
    }
  }
  // Clean up
  reply->deleteLater();
}

void downloadThread::loadCookies(const QString &host_name, QString url) {
  const QList<QByteArray> raw_cookies = Preferences::getHostNameCookies(host_name);
  QNetworkCookieJar *cookie_jar = networkManager.cookieJar();
  QList<QNetworkCookie> cookies;
  qDebug("Loading cookies for host name: %s", qPrintable(host_name));
  foreach(const QByteArray& raw_cookie, raw_cookies) {
    QList<QByteArray> cookie_parts = raw_cookie.split('=');
    if(cookie_parts.size() == 2) {
      qDebug("Loading cookie: %s", raw_cookie.constData());
      cookies << QNetworkCookie(cookie_parts.first(), cookie_parts.last());
    }
  }
  cookie_jar->setCookiesFromUrl(cookies, url);
  networkManager.setCookieJar(cookie_jar);
}

void downloadThread::downloadTorrentUrl(QString url){
  // Load cookies
  QString host_name = QUrl::fromEncoded(url.toLocal8Bit()).host();
  if(!host_name.isEmpty())
    loadCookies(host_name, url);
  // Process request
  QNetworkReply *reply = downloadUrl(url);
  connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(checkDownloadSize(qint64,qint64)));
}

QNetworkReply* downloadThread::downloadUrl(QString url){
  // Update proxy settings
  applyProxySettings();
  // Load cookies
  QString host_name = QUrl::fromEncoded(url.toLocal8Bit()).host();
  if(!host_name.isEmpty())
    loadCookies(host_name, url);
  // Process download request
  qDebug("url is %s", qPrintable(url));
  const QUrl qurl = QUrl::fromEncoded(url.toLocal8Bit());
  QNetworkRequest request(qurl);
  // Spoof Firefox 3.5 user agent to avoid
  // Web server banning
  request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; U; Linux i686 (x86_64); en-US; rv:1.9.1.5) Gecko/20091102 Firefox/3.5.5");
  qDebug("Downloading %s...", request.url().toEncoded().data());
  qDebug("%d cookies for this URL", networkManager.cookieJar()->cookiesForUrl(url).size());
  for(int i=0; i<networkManager.cookieJar()->cookiesForUrl(url).size(); ++i) {
    qDebug("%s=%s", networkManager.cookieJar()->cookiesForUrl(url).at(i).name().data(), networkManager.cookieJar()->cookiesForUrl(url).at(i).value().data());
    qDebug("Domain: %s, Path: %s", qPrintable(networkManager.cookieJar()->cookiesForUrl(url).at(i).domain()), qPrintable(networkManager.cookieJar()->cookiesForUrl(url).at(i).path()));
  }
  return networkManager.get(request);
}

void downloadThread::checkDownloadSize(qint64 bytesReceived, qint64 bytesTotal) {
  if(bytesTotal > 0) {
    QNetworkReply *reply = static_cast<QNetworkReply*>(sender());
    // Total number of bytes is available
    if(bytesTotal > 1048576) {
      // More than 1MB, this is probably not a torrent file, aborting...
      reply->abort();
    } else {
      disconnect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(checkDownloadSize(qint64,qint64)));
    }
  } else {
    if(bytesReceived  > 1048576) {
      // More than 1MB, this is probably not a torrent file, aborting...
      QNetworkReply *reply = static_cast<QNetworkReply*>(sender());
      reply->abort();
    }
  }
}

void downloadThread::applyProxySettings() {
  QNetworkProxy proxy;
  QIniSettings settings("qBittorrent", "qBittorrent");
  int intValue = settings.value(QString::fromUtf8("Preferences/Connection/HTTPProxyType"), 0).toInt();
  if(intValue > 0) {
    // Proxy enabled
    QString IP = settings.value(QString::fromUtf8("Preferences/Connection/HTTPProxy/IP"), "0.0.0.0").toString();
    proxy.setHostName(IP);
    QString port = settings.value(QString::fromUtf8("Preferences/Connection/HTTPProxy/Port"), 8080).toString();
    qDebug("Using proxy: %s", qPrintable(IP));
    proxy.setPort(port.toUShort());
    // Default proxy type is HTTP, we must change if it is SOCKS5
    if(intValue == SOCKS5 || intValue == SOCKS5_PW) {
      qDebug("Proxy is SOCKS5, not HTTP");
      proxy.setType(QNetworkProxy::Socks5Proxy);
    } else {
      proxy.setType(QNetworkProxy::HttpProxy);
    }
    // Authentication?
    if(intValue > 2) {
      qDebug("Proxy requires authentication, authenticating");
      QString username = settings.value(QString::fromUtf8("Preferences/Connection/HTTPProxy/Username"), QString()).toString();
      proxy.setUser(username);
      QString password = settings.value(QString::fromUtf8("Preferences/Connection/HTTPProxy/Password"), QString()).toString();
      proxy.setPassword(password);
    }

  } else {
    proxy.setType(QNetworkProxy::NoProxy);
  }
  networkManager.setProxy(proxy);
}

QString downloadThread::errorCodeToString(QNetworkReply::NetworkError status) {
  switch(status){
  case QNetworkReply::HostNotFoundError:
    return tr("The remote host name was not found (invalid hostname)");
  case QNetworkReply::OperationCanceledError:
    return tr("The operation was canceled");
  case QNetworkReply::RemoteHostClosedError:
    return tr("The remote server closed the connection prematurely, before the entire reply was received and processed");
  case QNetworkReply::TimeoutError:
    return tr("The connection to the remote server timed out");
  case QNetworkReply::SslHandshakeFailedError:
    return tr("SSL/TLS handshake failed");
  case QNetworkReply::ConnectionRefusedError:
    return tr("The remote server refused the connection");
  case QNetworkReply::ProxyConnectionRefusedError:
    return tr("The connection to the proxy server was refused");
  case QNetworkReply::ProxyConnectionClosedError:
    return tr("The proxy server closed the connection prematurely");
  case QNetworkReply::ProxyNotFoundError:
    return tr("The proxy host name was not found");
  case QNetworkReply::ProxyTimeoutError:
    return tr("The connection to the proxy timed out or the proxy did not reply in time to the request sent");
  case QNetworkReply::ProxyAuthenticationRequiredError:
    return tr("The proxy requires authentication in order to honour the request but did not accept any credentials offered");
  case QNetworkReply::ContentAccessDenied:
    return tr("The access to the remote content was denied (401)");
  case QNetworkReply::ContentOperationNotPermittedError:
    return tr("The operation requested on the remote content is not permitted");
  case QNetworkReply::ContentNotFoundError:
    return tr("The remote content was not found at the server (404)");
  case QNetworkReply::AuthenticationRequiredError:
    return tr("The remote server requires authentication to serve the content but the credentials provided were not accepted");
  case QNetworkReply::ProtocolUnknownError:
    return tr("The Network Access API cannot honor the request because the protocol is not known");
  case QNetworkReply::ProtocolInvalidOperationError:
    return tr("The requested operation is invalid for this protocol");
  case QNetworkReply::UnknownNetworkError:
    return tr("An unknown network-related error was detected");
  case QNetworkReply::UnknownProxyError:
    return tr("An unknown proxy-related error was detected");
  case QNetworkReply::UnknownContentError:
    return tr("An unknown error related to the remote content was detected");
  case QNetworkReply::ProtocolFailure:
    return tr("A breakdown in protocol was detected");
  default:
    return tr("Unknown error");
  }
}

#ifndef QT_NO_OPENSSL
void downloadThread::ignoreSslErrors(QNetworkReply* reply,QList<QSslError> errors) {
  Q_UNUSED(errors)
  // Ignore all SSL errors
  reply->ignoreSslErrors();
}
#endif

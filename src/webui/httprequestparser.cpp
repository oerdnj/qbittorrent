/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Ishan Arora and Christophe Dumez
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


#include "httprequestparser.h"
#include <QUrl>
#include <QDebug>

HttpRequestParser::HttpRequestParser(): m_error(false)
{
}

HttpRequestParser::~HttpRequestParser()
{
}

bool HttpRequestParser::isError() const {
  return m_error;
}

QString HttpRequestParser::url() const {
  return m_path;
}

QByteArray HttpRequestParser::message() const {
  return m_data;
}

QString HttpRequestParser::get(const QString& key) const {
  return m_getMap.value(key);
}

QString HttpRequestParser::post(const QString& key) const {
  return m_postMap.value(key);
}

const QByteArray& HttpRequestParser::torrent() const {
  return m_torrentContent;
}

void HttpRequestParser::writeHeader(const QByteArray& ba) {
  // Parse header
  m_header = QHttpRequestHeader(ba);
  QUrl url = QUrl::fromEncoded(m_header.path().toAscii());
  m_path = url.path();

  // Parse GET parameters
  QListIterator<QPair<QString, QString> > i(url.queryItems());
  while (i.hasNext()) {
    QPair<QString, QString> pair = i.next();
    m_getMap[pair.first] = pair.second;
  }
}

void HttpRequestParser::writeMessage(const QByteArray& ba) {
  // Parse message content
  Q_ASSERT (m_header.hasContentLength());

  m_data = ba;
  qDebug() << Q_FUNC_INFO << "m_data.size(): " << m_data.size();

  // Parse POST data
  if(m_header.contentType() == "application/x-www-form-urlencoded") {
    QUrl url;
    url.setEncodedQuery(m_data);
    QListIterator<QPair<QString, QString> > i(url.queryItems());
    while (i.hasNext())	{
      QPair<QString, QString> pair = i.next();
      m_postMap[pair.first] = pair.second;
    }
    return;
  }

  // Parse multipart/form data (torrent file)
  /**
        m_data has the following format (if boundary is "cH2ae0GI3KM7GI3Ij5ae0ei4Ij5Ij5")

--cH2ae0GI3KM7GI3Ij5ae0ei4Ij5Ij5
Content-Disposition: form-data; name=\"Filename\"

PB020344.torrent
--cH2ae0GI3KM7GI3Ij5ae0ei4Ij5Ij5
Content-Disposition: form-data; name=\"torrentfile"; filename=\"PB020344.torrent\"
Content-Type: application/x-bittorrent

BINARY DATA IS HERE
--cH2ae0GI3KM7GI3Ij5ae0ei4Ij5Ij5
Content-Disposition: form-data; name=\"Upload\"

Submit Query
--cH2ae0GI3KM7GI3Ij5ae0ei4Ij5Ij5--
**/
  if (m_header.contentType().startsWith("multipart/form-data")) {
    qDebug() << Q_FUNC_INFO << "header is: " << m_header.toString();

    int filename_index = m_data.indexOf("filename=");
    if (filename_index >= 0) {
      QByteArray boundary = m_data.left(m_data.indexOf("\r\n"));
      qDebug() << "Boundary is " << boundary << "\n\n";
      qDebug() << "Before binary data: " << m_data.left(m_data.indexOf("\r\n\r\n", filename_index+9)) << "\n\n";
      m_torrentContent = m_data.mid(m_data.indexOf("\r\n\r\n", filename_index+9) + 4);
      int binaryend_index = m_torrentContent.indexOf("\r\n"+boundary);
      if (binaryend_index >= 0) {
        qDebug() << "found end boundary :)";
        m_torrentContent = m_torrentContent.left(binaryend_index);
      }
      qDebug() << Q_FUNC_INFO << "m_torrentContent.size(): " << m_torrentContent.size()<< "\n\n";
    } else {
      m_error = true;
    }
  }
}

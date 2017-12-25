/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2012  Christophe Dumez <chris@qbittorrent.org>
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

#include "rss_parser.h"

#include <QDebug>
#include <QDateTime>
#include <QMetaObject>
#include <QRegExp>
#include <QStringList>
#include <QVariant>
#include <QXmlStreamReader>

#include "../rss_article.h"

namespace
{
    const char shortDay[][4] = {
        "Mon", "Tue", "Wed",
        "Thu", "Fri", "Sat",
        "Sun"
    };

    const char longDay[][10] = {
        "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday",
        "Sunday"
    };

    const char shortMonth[][4] = {
        "Jan", "Feb", "Mar", "Apr",
        "May", "Jun", "Jul", "Aug",
        "Sep", "Oct", "Nov", "Dec"
    };

    // Ported to Qt from KDElibs4
    QDateTime parseDate(const QString &string)
    {
        const QString str = string.trimmed();
        if (str.isEmpty())
            return QDateTime::currentDateTime();

        int nyear  = 6;   // indexes within string to values
        int nmonth = 4;
        int nday   = 2;
        int nwday  = 1;
        int nhour  = 7;
        int nmin   = 8;
        int nsec   = 9;
        // Also accept obsolete form "Weekday, DD-Mon-YY HH:MM:SS ±hhmm"
        QRegExp rx("^(?:([A-Z][a-z]+),\\s*)?(\\d{1,2})(\\s+|-)([^-\\s]+)(\\s+|-)(\\d{2,4})\\s+(\\d\\d):(\\d\\d)(?::(\\d\\d))?\\s+(\\S+)$");
        QStringList parts;
        if (!str.indexOf(rx)) {
            // Check that if date has '-' separators, both separators are '-'.
            parts = rx.capturedTexts();
            bool h1 = (parts[3] == QLatin1String("-"));
            bool h2 = (parts[5] == QLatin1String("-"));
            if (h1 != h2)
                return QDateTime::currentDateTime();
        }
        else {
            // Check for the obsolete form "Wdy Mon DD HH:MM:SS YYYY"
            rx = QRegExp("^([A-Z][a-z]+)\\s+(\\S+)\\s+(\\d\\d)\\s+(\\d\\d):(\\d\\d):(\\d\\d)\\s+(\\d\\d\\d\\d)$");
            if (str.indexOf(rx))
                return QDateTime::currentDateTime();
            nyear  = 7;
            nmonth = 2;
            nday   = 3;
            nwday  = 1;
            nhour  = 4;
            nmin   = 5;
            nsec   = 6;
            parts = rx.capturedTexts();
        }

        bool ok[4];
        const int day = parts[nday].toInt(&ok[0]);
        int year = parts[nyear].toInt(&ok[1]);
        const int hour = parts[nhour].toInt(&ok[2]);
        const int minute = parts[nmin].toInt(&ok[3]);
        if (!ok[0] || !ok[1] || !ok[2] || !ok[3])
            return QDateTime::currentDateTime();

        int second = 0;
        if (!parts[nsec].isEmpty()) {
            second = parts[nsec].toInt(&ok[0]);
            if (!ok[0])
                return QDateTime::currentDateTime();
        }

        bool leapSecond = (second == 60);
        if (leapSecond)
            second = 59;   // apparently a leap second - validate below, once time zone is known
        int month = 0;
        for ( ; (month < 12)  &&  (parts[nmonth] != shortMonth[month]); ++month);
        int dayOfWeek = -1;
        if (!parts[nwday].isEmpty()) {
            // Look up the weekday name
            while (++dayOfWeek < 7 && (shortDay[dayOfWeek] != parts[nwday]));
            if (dayOfWeek >= 7)
                for (dayOfWeek = 0; dayOfWeek < 7 && (longDay[dayOfWeek] != parts[nwday]); ++dayOfWeek);
        }

        //       if (month >= 12 || dayOfWeek >= 7
        //       ||  (dayOfWeek < 0  &&  format == RFCDateDay))
        //         return QDateTime;
        int i = parts[nyear].size();
        if (i < 4) {
            // It's an obsolete year specification with less than 4 digits
            year += (i == 2  &&  year < 50) ? 2000 : 1900;
        }

        // Parse the UTC offset part
        int offset = 0;           // set default to '-0000'
        bool negOffset = false;
        if (parts.count() > 10) {
            rx = QRegExp("^([+-])(\\d\\d)(\\d\\d)$");
            if (!parts[10].indexOf(rx)) {
                // It's a UTC offset ±hhmm
                parts = rx.capturedTexts();
                offset = parts[2].toInt(&ok[0]) * 3600;
                int offsetMin = parts[3].toInt(&ok[1]);
                if (!ok[0] || !ok[1] || offsetMin > 59)
                    return QDateTime();
                offset += offsetMin * 60;
                negOffset = (parts[1] == QLatin1String("-"));
                if (negOffset)
                    offset = -offset;
            }
            else {
                // Check for an obsolete time zone name
                QByteArray zone = parts[10].toLatin1();
                if (zone.length() == 1  &&  isalpha(zone[0])  &&  toupper(zone[0]) != 'J') {
                    negOffset = true;    // military zone: RFC 2822 treats as '-0000'
                }
                else if (zone != "UT" && zone != "GMT") { // treated as '+0000'
                    offset = (zone == "EDT")
                            ? -4 * 3600
                            : ((zone == "EST") || (zone == "CDT"))
                              ? -5 * 3600
                              : ((zone == "CST") || (zone == "MDT"))
                                ? -6 * 3600
                                : (zone == "MST" || zone == "PDT")
                                  ? -7 * 3600
                                  : (zone == "PST")
                                    ? -8 * 3600
                                    : 0;
                    if (!offset) {
                        // Check for any other alphabetic time zone
                        bool nonalpha = false;
                        for (int i = 0, end = zone.size(); (i < end) && !nonalpha; ++i)
                            nonalpha = !isalpha(zone[i]);
                        if (nonalpha)
                            return QDateTime();
                        // TODO: Attempt to recognize the time zone abbreviation?
                        negOffset = true;    // unknown time zone: RFC 2822 treats as '-0000'
                    }
                }
            }
        }

        QDate qdate(year, month + 1, day);   // convert date, and check for out-of-range
        if (!qdate.isValid())
            return QDateTime::currentDateTime();

        QTime qTime(hour, minute, second);
        QDateTime result(qdate, qTime, Qt::UTC);
        if (offset)
            result = result.addSecs(-offset);
        if (!result.isValid())
            return QDateTime::currentDateTime();    // invalid date/time

        if (leapSecond) {
            // Validate a leap second time. Leap seconds are inserted after 23:59:59 UTC.
            // Convert the time to UTC and check that it is 00:00:00.
            if ((hour*3600 + minute*60 + 60 - offset + 86400*5) % 86400)   // (max abs(offset) is 100 hours)
                return QDateTime::currentDateTime();    // the time isn't the last second of the day
        }

        return result;
    }
}

using namespace RSS::Private;

const int ParsingResultTypeId = qRegisterMetaType<ParsingResult>();

Parser::Parser(QString lastBuildDate)
{
    m_result.lastBuildDate = lastBuildDate;
}

void Parser::parse(const QByteArray &feedData)
{
    QMetaObject::invokeMethod(this, "parse_impl", Qt::QueuedConnection
                              , Q_ARG(QByteArray, feedData));
}

// read and create items from a rss document
void Parser::parse_impl(const QByteArray &feedData)
{
    QXmlStreamReader xml(feedData);
    bool foundChannel = false;

    while (xml.readNextStartElement()) {
        if (xml.name() == "rss") {
            // Find channels
            while (xml.readNextStartElement()) {
                if (xml.name() == "channel") {
                    parseRSSChannel(xml);
                    foundChannel = true;
                    break;
                }
                else {
                    qDebug() << "Skip rss item: " << xml.name();
                    xml.skipCurrentElement();
                }
            }
            break;
        }
        else if (xml.name() == "feed") { // Atom feed
            parseAtomChannel(xml);
            foundChannel = true;
            break;
        }
        else {
            qDebug() << "Skip root item: " << xml.name();
            xml.skipCurrentElement();
        }
    }

    if (!foundChannel) {
        m_result.error = tr("Invalid RSS feed.");
    }
    else if (xml.hasError()) {
        m_result.error = tr("%1 (line: %2, column: %3, offset: %4).")
                .arg(xml.errorString()).arg(xml.lineNumber())
                .arg(xml.columnNumber()).arg(xml.characterOffset());
    }
    else {
        // Sort article list chronologically
        // NOTE: We don't need to sort it here if articles are always
        // sorted in fetched XML in reverse chronological order
        std::sort(m_result.articles.begin(), m_result.articles.end()
                  , [](const QVariantHash &a1, const QVariantHash &a2)
        {
            return a1["date"].toDateTime() < a2["date"].toDateTime();
        });
    }

    emit finished(m_result);
    m_result.articles.clear(); // clear articles only
}

void Parser::parseRssArticle(QXmlStreamReader &xml)
{
    QVariantHash article;

    while (!xml.atEnd()) {
        xml.readNext();
        const QString name(xml.name().toString());

        if (xml.isEndElement() && (name == QLatin1String("item")))
            break;

        if (xml.isStartElement()) {
            if (name == QLatin1String("title")) {
                article[Article::KeyTitle] = xml.readElementText().trimmed();
            }
            else if (name == QLatin1String("enclosure")) {
                if (xml.attributes().value("type") == QLatin1String("application/x-bittorrent"))
                    article[Article::KeyTorrentURL] = xml.attributes().value(QLatin1String("url")).toString();
            }
            else if (name == QLatin1String("link")) {
                const QString text {xml.readElementText().trimmed()};
                if (text.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
                    article[Article::KeyTorrentURL] = text; // magnet link instead of a news URL
                else
                    article[Article::KeyLink] = text;
            }
            else if (name == QLatin1String("description")) {
                article[Article::KeyDescription] = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
            else if (name == QLatin1String("pubDate")) {
                article[Article::KeyDate] = parseDate(xml.readElementText().trimmed());
            }
            else if (name == QLatin1String("author")) {
                article[Article::KeyAuthor] = xml.readElementText().trimmed();
            }
            else if (name == QLatin1String("guid")) {
                article[Article::KeyId] = xml.readElementText().trimmed();
            }
            else {
                article[name] = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }
    }

    m_result.articles.prepend(article);
}

void Parser::parseRSSChannel(QXmlStreamReader &xml)
{
    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("title")) {
                m_result.title = xml.readElementText();
            }
            else if (xml.name() == QLatin1String("lastBuildDate")) {
                QString lastBuildDate = xml.readElementText();
                if (!lastBuildDate.isEmpty()) {
                    if (m_result.lastBuildDate == lastBuildDate) {
                        qDebug() << "The RSS feed has not changed since last time, aborting parsing.";
                        return;
                    }
                    m_result.lastBuildDate = lastBuildDate;
                }
            }
            else if (xml.name() == QLatin1String("item")) {
                parseRssArticle(xml);
            }
        }
    }
}

void Parser::parseAtomArticle(QXmlStreamReader &xml)
{
    QVariantHash article;
    bool doubleContent = false;

    while (!xml.atEnd()) {
        xml.readNext();
        const QString name(xml.name().toString());

        if (xml.isEndElement() && (name == QLatin1String("entry")))
            break;

        if (xml.isStartElement()) {
            if (name == QLatin1String("title")) {
                article[Article::KeyTitle] = xml.readElementText().trimmed();
            }
            else if (name == QLatin1String("link")) {
                QString link = (xml.attributes().isEmpty()
                                ? xml.readElementText().trimmed()
                                : xml.attributes().value(QLatin1String("href")).toString());

                if (link.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
                    article[Article::KeyTorrentURL] = link; // magnet link instead of a news URL
                else
                    // Atom feeds can have relative links, work around this and
                    // take the stress of figuring article full URI from UI
                    // Assemble full URI
                    article[Article::KeyLink] = (m_baseUrl.isEmpty() ? link : m_baseUrl + link);

            }
            else if ((name == QLatin1String("summary")) || (name == QLatin1String("content"))) {
                if (doubleContent) { // Duplicate content -> ignore
                    xml.skipCurrentElement();
                    continue;
                }

                // Try to also parse broken articles, which don't use html '&' escapes
                // Actually works great for non-broken content too
                QString feedText = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                if (!feedText.isEmpty()) {
                    article[Article::KeyDescription] = feedText;
                    doubleContent = true;
                }
            }
            else if (name == QLatin1String("updated")) {
                // ATOM uses standard compliant date, don't do fancy stuff
                QDateTime articleDate = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODate);
                article[Article::KeyDate] = (articleDate.isValid() ? articleDate : QDateTime::currentDateTime());
            }
            else if (name == QLatin1String("author")) {
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("name"))
                        article[Article::KeyAuthor] = xml.readElementText().trimmed();
                    else
                        xml.skipCurrentElement();
                }
            }
            else if (name == QLatin1String("id")) {
                article[Article::KeyId] = xml.readElementText().trimmed();
            }
            else {
                article[name] = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }
    }

    m_result.articles.prepend(article);
}

void Parser::parseAtomChannel(QXmlStreamReader &xml)
{
    m_baseUrl = xml.attributes().value("xml:base").toString();

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("title")) {
                m_result.title = xml.readElementText();
            }
            else if (xml.name() == QLatin1String("updated")) {
                QString lastBuildDate = xml.readElementText();
                if (!lastBuildDate.isEmpty()) {
                    if (m_result.lastBuildDate == lastBuildDate) {
                        qDebug() << "The RSS feed has not changed since last time, aborting parsing.";
                        return;
                    }
                    m_result.lastBuildDate = lastBuildDate;
                }
            }
            else if (xml.name() == QLatin1String("entry")) {
                parseAtomArticle(xml);
            }
        }
    }
}

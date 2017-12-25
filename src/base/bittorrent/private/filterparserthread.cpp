/*
 * Bittorrent Client using Qt and libt.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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
 */

#include "filterparserthread.h"

#include <cctype>

#include <QDataStream>
#include <QFile>

#include "base/logger.h"

namespace libt = libtorrent;

namespace
{
    class IPv4Parser
    {
    public:
        bool tryParse(const char *str)
        {
            unsigned char octetIndex = 0;

            const char *octetStart = str;
            char *endptr;
            for (; *str; ++str) {
                if (*str == '.') {
                    long int extractedNum = strtol(octetStart, &endptr, 10);
                    if ((extractedNum >= 0L) && (extractedNum <= 255L))
                        m_buf[octetIndex++] = static_cast<unsigned char>(extractedNum);
                    else
                        return false;

                    if (endptr != str)
                        return false;
                    if (octetIndex == 4)
                        return true;

                    octetStart = str + 1;
                }
            }

            if (str != octetStart) {
                long int extractedNum = strtol(octetStart, &endptr, 10);
                if ((extractedNum >= 0L) && (extractedNum <= 255L))
                    m_buf[octetIndex] = static_cast<unsigned char>(strtol(octetStart, &endptr, 10));
                else
                    return false;

                if ((endptr == str) && (octetIndex == 3))
                    return true;
            }

            return false;
        }

        libt::address_v4::bytes_type parsed() const
        {
            return m_buf;
        }

    private:
        libt::address_v4::bytes_type m_buf;
    };

    bool parseIPAddress(const char *data, libt::address &address)
    {
        IPv4Parser parser;
        boost::system::error_code ec;

        if (parser.tryParse(data))
            address = libt::address_v4(parser.parsed());
        else
            address = libt::address_v6::from_string(data, ec);

        return !ec;
    }

    const int BUFFER_SIZE = 2 * 1024 * 1024; // 2 MiB
    const int MAX_LOGGED_ERRORS = 5;
}

FilterParserThread::FilterParserThread(QObject *parent)
    : QThread(parent)
    , m_abort(false)
{
}

FilterParserThread::~FilterParserThread()
{
    m_abort = true;
    wait();
}

// Parser for eMule ip filter in DAT format
int FilterParserThread::parseDATFilterFile()
{
    int ruleCount = 0;
    QFile file(m_filePath);
    if (!file.exists()) return ruleCount;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LogMsg(tr("I/O Error: Could not open IP filter file in read mode."), Log::CRITICAL);
        return ruleCount;
    }

    std::vector<char> buffer(BUFFER_SIZE, 0); // seems a bit faster than QVector
    qint64 bytesRead = 0;
    int offset = 0;
    int start = 0;
    int endOfLine = -1;
    int nbLine = 0;
    int parseErrorCount = 0;
    const auto addLog = [&parseErrorCount](const QString &msg)
    {
        if (parseErrorCount <= MAX_LOGGED_ERRORS)
            LogMsg(msg, Log::CRITICAL);
    };

    while (true) {
        bytesRead = file.read(buffer.data() + offset, BUFFER_SIZE - offset - 1);
        if (bytesRead < 0)
            break;
        int dataSize = bytesRead + offset;
        if (bytesRead == 0 && dataSize == 0)
            break;

        for (start = 0; start < dataSize; ++start) {
            endOfLine = -1;
            // The file might have ended without the last line having a newline
            if (!(bytesRead == 0 && dataSize > 0)) {
                for (int i = start; i < dataSize; ++i) {
                    if (buffer[i] == '\n') {
                        endOfLine = i;
                        // We need to NULL the newline in case the line has only an IP range.
                        // In that case the parser won't work for the end IP, because it ends
                        // with the newline and not with a number.
                        buffer[i] = '\0';
                        break;
                    }
                }
            }
            else {
                endOfLine = dataSize;
                buffer[dataSize] = '\0';
            }

            if (endOfLine == -1) {
                // read the next chunk from file
                // but first move(copy) the leftover data to the front of the buffer
                offset = dataSize - start;
                memmove(buffer.data(), buffer.data() + start, offset);
                break;
            }
            else {
                ++nbLine;
            }

            if ((buffer[start] == '#')
                || ((buffer[start] == '/') && ((start + 1 < dataSize) && (buffer[start + 1] == '/')))) {
                start = endOfLine;
                continue;
            }

            // Each line should follow this format:
            // 001.009.096.105 - 001.009.096.105 , 000 , Some organization
            // The 3rd entry is access level and if above 127 the IP range isn't blocked.
            int firstComma = findAndNullDelimiter(buffer.data(), ',', start, endOfLine);
            if (firstComma != -1)
                findAndNullDelimiter(buffer.data(), ',', firstComma + 1, endOfLine);

            // Check if there is an access value (apparently not mandatory)
            if (firstComma != -1) {
                // There is possibly one
                const long int nbAccess = strtol(buffer.data() + firstComma + 1, nullptr, 10);
                // Ignoring this rule because access value is too high
                if (nbAccess > 127L) {
                    start = endOfLine;
                    continue;
                }
            }

            // IP Range should be split by a dash
            int endOfIPRange = ((firstComma == -1) ? (endOfLine - 1) : (firstComma - 1));
            int delimIP = findAndNullDelimiter(buffer.data(), '-', start, endOfIPRange);
            if (delimIP == -1) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            libt::address startAddr;
            int newStart = trim(buffer.data(), start, delimIP - 1);
            if (!parseIPAddress(buffer.data() + newStart, startAddr)) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed. Start IP of the range is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            libt::address endAddr;
            newStart = trim(buffer.data(), delimIP + 1, endOfIPRange);
            if (!parseIPAddress(buffer.data() + newStart, endAddr)) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed. End IP of the range is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            if ((startAddr.is_v4() != endAddr.is_v4())
                || (startAddr.is_v6() != endAddr.is_v6())) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed. One IP is IPv4 and the other is IPv6!").arg(nbLine));
                start = endOfLine;
                continue;
            }

            start = endOfLine;

            // Now Add to the filter
            try {
                m_filter.add_rule(startAddr, endAddr, libt::ip_filter::blocked);
                ++ruleCount;
            }
            catch (std::exception &e) {
                ++parseErrorCount;
                addLog(tr("IP filter exception thrown for line %1. Exception is: %2")
                       .arg(nbLine).arg(QString::fromLocal8Bit(e.what())));
            }
        }

        if (start >= dataSize)
            offset = 0;
    }

    if (parseErrorCount > MAX_LOGGED_ERRORS)
        LogMsg(tr("%1 extra IP filter parsing errors occurred.", "513 extra IP filter parsing errors occurred.")
               .arg(parseErrorCount - MAX_LOGGED_ERRORS), Log::CRITICAL);
    return ruleCount;
}

// Parser for PeerGuardian ip filter in p2p format
int FilterParserThread::parseP2PFilterFile()
{
    int ruleCount = 0;
    QFile file(m_filePath);
    if (!file.exists()) return ruleCount;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LogMsg(tr("I/O Error: Could not open IP filter file in read mode."), Log::CRITICAL);
        return ruleCount;
    }

    std::vector<char> buffer(BUFFER_SIZE, 0); // seems a bit faster than QVector
    qint64 bytesRead = 0;
    int offset = 0;
    int start = 0;
    int endOfLine = -1;
    int nbLine = 0;
    int parseErrorCount = 0;
    const auto addLog = [&parseErrorCount](const QString &msg)
    {
        if (parseErrorCount <= MAX_LOGGED_ERRORS)
            LogMsg(msg, Log::CRITICAL);
    };

    while (true) {
        bytesRead = file.read(buffer.data() + offset, BUFFER_SIZE - offset - 1);
        if (bytesRead < 0)
            break;
        int dataSize = bytesRead + offset;
        if (bytesRead == 0 && dataSize == 0)
            break;

        for (start = 0; start < dataSize; ++start) {
            endOfLine = -1;
            // The file might have ended without the last line having a newline
            if (!(bytesRead == 0 && dataSize > 0)) {
                for (int i = start; i < dataSize; ++i) {
                    if (buffer[i] == '\n') {
                        endOfLine = i;
                        // We need to NULL the newline in case the line has only an IP range.
                        // In that case the parser won't work for the end IP, because it ends
                        // with the newline and not with a number.
                        buffer[i] = '\0';
                        break;
                    }
                }
            }
            else {
                endOfLine = dataSize;
                buffer[dataSize] = '\0';
            }

            if (endOfLine == -1) {
                // read the next chunk from file
                // but first move(copy) the leftover data to the front of the buffer
                offset = dataSize - start;
                memmove(buffer.data(), buffer.data() + start, offset);
                break;
            }
            else {
                ++nbLine;
            }

            if ((buffer[start] == '#')
                || ((buffer[start] == '/') && ((start + 1 < dataSize) && (buffer[start + 1] == '/')))) {
                start = endOfLine;
                continue;
            }

            // Each line should follow this format:
            // Some organization:1.0.0.0-1.255.255.255
            // The "Some organization" part might contain a ':' char itself so we find the last occurrence
            int partsDelimiter = findAndNullDelimiter(buffer.data(), ':', start, endOfLine, true);
            if (partsDelimiter == -1) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            // IP Range should be split by a dash
            int delimIP = findAndNullDelimiter(buffer.data(), '-', partsDelimiter + 1, endOfLine);
            if (delimIP == -1) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            libt::address startAddr;
            int newStart = trim(buffer.data(), partsDelimiter + 1, delimIP - 1);
            if (!parseIPAddress(buffer.data() + newStart, startAddr)) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed. Start IP of the range is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            libt::address endAddr;
            newStart = trim(buffer.data(), delimIP + 1, endOfLine);
            if (!parseIPAddress(buffer.data() + newStart, endAddr)) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed. End IP of the range is malformed.").arg(nbLine));
                start = endOfLine;
                continue;
            }

            if ((startAddr.is_v4() != endAddr.is_v4())
                || (startAddr.is_v6() != endAddr.is_v6())) {
                ++parseErrorCount;
                addLog(tr("IP filter line %1 is malformed. One IP is IPv4 and the other is IPv6!").arg(nbLine));
                start = endOfLine;
                continue;
            }

            start = endOfLine;

            try {
                m_filter.add_rule(startAddr, endAddr, libt::ip_filter::blocked);
                ++ruleCount;
            }
            catch (std::exception &e) {
                ++parseErrorCount;
                addLog(tr("IP filter exception thrown for line %1. Exception is: %2")
                       .arg(nbLine).arg(QString::fromLocal8Bit(e.what())));
            }
        }

        if (start >= dataSize)
            offset = 0;
    }

    if (parseErrorCount > MAX_LOGGED_ERRORS)
        LogMsg(tr("%1 extra IP filter parsing errors occurred.", "513 extra IP filter parsing errors occurred.")
               .arg(parseErrorCount - MAX_LOGGED_ERRORS), Log::CRITICAL);
    return ruleCount;
}

int FilterParserThread::getlineInStream(QDataStream &stream, std::string &name, char delim)
{
    char c;
    int total_read = 0;
    int read;
    do {
        read = stream.readRawData(&c, 1);
        total_read += read;
        if (read > 0) {
            if (c != delim) {
                name += c;
            }
            else {
                // Delim found
                return total_read;
            }
        }
    }
    while(read > 0);

    return total_read;
}

// Parser for PeerGuardian ip filter in p2p format
int FilterParserThread::parseP2BFilterFile()
{
    int ruleCount = 0;
    QFile file(m_filePath);
    if (!file.exists()) return ruleCount;

    if (!file.open(QIODevice::ReadOnly)) {
        LogMsg(tr("I/O Error: Could not open IP filter file in read mode."), Log::CRITICAL);
        return ruleCount;
    }

    QDataStream stream(&file);
    // Read header
    char buf[7];
    unsigned char version;
    if (!stream.readRawData(buf, sizeof(buf))
        || memcmp(buf, "\xFF\xFF\xFF\xFFP2B", 7)
        || !stream.readRawData(reinterpret_cast<char*>(&version), sizeof(version))) {
        LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
        return ruleCount;
    }

    if ((version == 1) || (version == 2)) {
        qDebug ("p2b version 1 or 2");
        unsigned int start, end;

        std::string name;
        while(getlineInStream(stream, name, '\0') && !m_abort) {
            if (!stream.readRawData(reinterpret_cast<char*>(&start), sizeof(start))
                || !stream.readRawData(reinterpret_cast<char*>(&end), sizeof(end))) {
                LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
                return ruleCount;
            }

            // Network byte order to Host byte order
            // asio address_v4 constructor expects it
            // that way
            libt::address_v4 first(ntohl(start));
            libt::address_v4 last(ntohl(end));
            // Apply to bittorrent session
            try {
                m_filter.add_rule(first, last, libt::ip_filter::blocked);
                ++ruleCount;
            }
            catch (std::exception &) {}
        }
    }
    else if (version == 3) {
        qDebug ("p2b version 3");
        unsigned int namecount;
        if (!stream.readRawData(reinterpret_cast<char*>(&namecount), sizeof(namecount))) {
            LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
            return ruleCount;
        }

        namecount = ntohl(namecount);
        // Reading names although, we don't really care about them
        for (unsigned int i = 0; i < namecount; ++i) {
            std::string name;
            if (!getlineInStream(stream, name, '\0')) {
                LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
                return ruleCount;
            }

            if (m_abort) return ruleCount;
        }

        // Reading the ranges
        unsigned int rangecount;
        if (!stream.readRawData(reinterpret_cast<char*>(&rangecount), sizeof(rangecount))) {
            LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
            return ruleCount;
        }

        rangecount = ntohl(rangecount);
        unsigned int name, start, end;
        for (unsigned int i = 0; i < rangecount; ++i) {
            if (!stream.readRawData(reinterpret_cast<char*>(&name), sizeof(name))
                || !stream.readRawData(reinterpret_cast<char*>(&start), sizeof(start))
                || !stream.readRawData(reinterpret_cast<char*>(&end), sizeof(end))) {
                LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
                return ruleCount;
            }

            // Network byte order to Host byte order
            // asio address_v4 constructor expects it
            // that way
            libt::address_v4 first(ntohl(start));
            libt::address_v4 last(ntohl(end));
            // Apply to bittorrent session
            try {
                m_filter.add_rule(first, last, libt::ip_filter::blocked);
                ++ruleCount;
            }
            catch (std::exception &) {}

            if (m_abort) return ruleCount;
        }
    }
    else {
        LogMsg(tr("Parsing Error: The filter file is not a valid PeerGuardian P2B file."), Log::CRITICAL);
    }

    return ruleCount;
}

// Process ip filter file
// Supported formats:
//  * eMule IP list (DAT): http://wiki.phoenixlabs.org/wiki/DAT_Format
//  * PeerGuardian Text (P2P): http://wiki.phoenixlabs.org/wiki/P2P_Format
//  * PeerGuardian Binary (P2B): http://wiki.phoenixlabs.org/wiki/P2B_Format
void FilterParserThread::processFilterFile(const QString &filePath)
{
    if (isRunning()) {
        // Already parsing a filter, m_abort first
        m_abort = true;
        wait();
    }

    m_abort = false;
    m_filePath = filePath;
    m_filter = libt::ip_filter();
    // Run it
    start();
}

libt::ip_filter FilterParserThread::IPfilter()
{
    return m_filter;
}

void FilterParserThread::run()
{
    qDebug("Processing filter file");
    int ruleCount = 0;
    if (m_filePath.endsWith(".p2p", Qt::CaseInsensitive)) {
        // PeerGuardian p2p file
        ruleCount = parseP2PFilterFile();
    }
    else if (m_filePath.endsWith(".p2b", Qt::CaseInsensitive)) {
        // PeerGuardian p2b file
        ruleCount = parseP2BFilterFile();
    }
    else if (m_filePath.endsWith(".dat", Qt::CaseInsensitive)) {
        // eMule DAT format
        ruleCount = parseDATFilterFile();
    }

    if (m_abort) return;

    try {
        emit IPFilterParsed(ruleCount);
    }
    catch (std::exception &) {
        emit IPFilterError();
    }

    qDebug("IP Filter thread: finished parsing, filter applied");
}

int FilterParserThread::findAndNullDelimiter(char *const data, char delimiter, int start, int end, bool reverse)
{
    if (!reverse) {
        for (int i = start; i <= end; ++i) {
            if (data[i] == delimiter) {
                data[i] = '\0';
                return i;
            }
        }
    }
    else {
        for (int i = end; i >= start; --i) {
            if (data[i] == delimiter) {
                data[i] = '\0';
                return i;
            }
        }
    }

    return -1;
}

int FilterParserThread::trim(char* const data, int start, int end)
{
    if (start >= end) return start;
    int newStart = start;

    for (int i = start; i <= end; ++i) {
        if (isspace(data[i]) != 0) {
            data[i] = '\0';
        }
        else {
            newStart = i;
            break;
        }
    }

    for (int i = end; i >= start; --i) {
        if (isspace(data[i]) != 0)
            data[i] = '\0';
        else
            break;
    }

    return newStart;
}

/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2016  Eugene Shalygin <eugene.shalygin@gmail.com>
 * Copyright (C) 2014  Vladimir Golovnev <glassez@yandex.ru>
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

#ifndef APP_OPTIONS_H
#define APP_OPTIONS_H

#include <stdexcept>

#include <QString>
#include <QStringList>

#include "base/tristatebool.h"

class QProcessEnvironment;

struct QBtCommandLineParameters
{
    bool showHelp, relativeFastresumePaths, portableMode, skipChecking, sequential, firstLastPiecePriority;
#ifndef Q_OS_WIN
    bool showVersion;
#endif
#ifndef DISABLE_GUI
    bool noSplash;
#else
    bool shouldDaemonize;
#endif
    int webUiPort;
    TriStateBool addPaused, skipDialog;
    QStringList torrents;
    QString profileDir, configurationName, savePath, category, unknownParameter;

    QBtCommandLineParameters(const QProcessEnvironment&);
    QStringList paramList() const;
};

class CommandLineParameterError: public std::runtime_error
{
public:
    CommandLineParameterError(const QString &messageForUser);
    const QString& messageForUser() const;

private:
    const QString m_messageForUser;
};

QBtCommandLineParameters parseCommandLine(const QStringList &args);
void displayUsage(const QString &prgName);

#endif // APP_OPTIONS_H

/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2016  Eugene Shalygin <eugene.shalygin@gmail.com>
 * Copyright (C) 2012  Christophe Dumez
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
 */

#include "profile_p.h"

#include <QCoreApplication>

#include "base/utils/fs.h"

Private::Profile::Profile(const QString &configurationName)
    : m_configurationSuffix {configurationName.isEmpty() ? QString() : QLatin1Char('_') + configurationName}
{
}

QString Private::Profile::configurationSuffix() const
{
    return m_configurationSuffix;
}

QString Private::Profile::profileName() const
{
    return QCoreApplication::applicationName() + configurationSuffix();
}

Private::DefaultProfile::DefaultProfile(const QString &configurationName)
    : Profile(configurationName)
{
}

QString Private::DefaultProfile::baseDirectory() const
{
    return QDir::homePath();
}

QString Private::DefaultProfile::cacheLocation() const
{
    return locationWithConfigurationName(QStandardPaths::CacheLocation);
}

QString Private::DefaultProfile::configLocation() const
{
#if defined(Q_OS_WIN)
    // On Windows QSettings stores files in FOLDERID_RoamingAppData\AppName
    return locationWithConfigurationName(QStandardPaths::AppDataLocation);
#else
    return locationWithConfigurationName(QStandardPaths::AppConfigLocation);
#endif
}

QString Private::DefaultProfile::dataLocation() const
{
#if defined(Q_OS_WIN) || defined (Q_OS_MAC)
    return locationWithConfigurationName(QStandardPaths::AppLocalDataLocation);
#else
    // on Linux gods know why qBittorrent creates 'data' subdirectory in ~/.local/share/
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QLatin1String("/data/") + profileName() + QLatin1Char('/');
#endif
}

QString Private::DefaultProfile::downloadLocation() const
{
#if defined(Q_OS_WIN)
    if (QSysInfo::windowsVersion() <= QSysInfo::WV_XP)  // Windows XP
        return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).absoluteFilePath(
            QCoreApplication::translate("fsutils", "Downloads"));
#endif
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

SettingsPtr Private::DefaultProfile::applicationSettings(const QString &name) const
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    return SettingsPtr(new QSettings(QSettings::IniFormat, QSettings::UserScope, profileName(), name));
#else
    return SettingsPtr(new QSettings(profileName(), name));
#endif
}

QString Private::DefaultProfile::locationWithConfigurationName(QStandardPaths::StandardLocation location) const
{
    return QStandardPaths::writableLocation(location) + configurationSuffix();
}

Private::CustomProfile::CustomProfile(const QString &rootPath, const QString &configurationName)
    : Profile {configurationName}
    , m_rootDirectory {QDir(rootPath).absoluteFilePath(this->profileName())}
{
}

QString Private::CustomProfile::baseDirectory() const
{
    return m_rootDirectory.canonicalPath();
}

QString Private::CustomProfile::cacheLocation() const
{
    return m_rootDirectory.absoluteFilePath(QLatin1String(cacheDirName));
}

QString Private::CustomProfile::configLocation() const
{
    return m_rootDirectory.absoluteFilePath(QLatin1String(configDirName));
}

QString Private::CustomProfile::dataLocation() const
{
    return m_rootDirectory.absoluteFilePath(QLatin1String(dataDirName));
}

QString Private::CustomProfile::downloadLocation() const
{
    return m_rootDirectory.absoluteFilePath(QLatin1String(downloadsDirName));
}

SettingsPtr Private::CustomProfile::applicationSettings(const QString &name) const
{
    // here we force QSettings::IniFormat format always because we need it to be portable across platforms
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    constexpr const char *CONF_FILE_EXTENSION = ".ini";
#else
    constexpr const char *CONF_FILE_EXTENSION = ".conf";
#endif
    const QString settingsFileName {QDir(configLocation()).absoluteFilePath(name + QLatin1String(CONF_FILE_EXTENSION))};
    return SettingsPtr(new QSettings(settingsFileName, QSettings::IniFormat));
}

QString Private::NoConvertConverter::fromPortablePath(const QString &portablePath) const
{
    return portablePath;
}

QString Private::NoConvertConverter::toPortablePath(const QString &path) const
{
    return path;
}

Private::Converter::Converter(const QString &basePath)
    : m_baseDir {basePath}
{
    m_baseDir.makeAbsolute();
}

QString Private::Converter::toPortablePath(const QString &path) const
{
    if (path.isEmpty() || m_baseDir.path().isEmpty())
        return path;

#ifdef Q_OS_WIN
    if (QDir::isAbsolutePath(path)) {
        QChar driveLeter = path[0].toUpper();
        QChar baseDriveLetter = m_baseDir.path()[0].toUpper();
        bool onSameDrive = (driveLeter.category() == QChar::Letter_Uppercase) && (driveLeter == baseDriveLetter);
        if (!onSameDrive)
            return path;
    }
#endif
    return m_baseDir.relativeFilePath(path);
}

QString Private::Converter::fromPortablePath(const QString &portablePath) const
{
    if (QDir::isAbsolutePath(portablePath))
        return portablePath;

    return QDir::cleanPath(m_baseDir.absoluteFilePath(portablePath));
}

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

#include "profile.h"

#include <QCoreApplication>

#include "private/profile_p.h"

Profile *Profile::m_instance = nullptr;

Profile::Profile(Private::Profile *impl, Private::PathConverter *pathConverter)
    : m_profileImpl(impl)
    , m_pathConverterImpl(pathConverter)
{
    ensureDirectoryExists(SpecialFolder::Cache);
    ensureDirectoryExists(SpecialFolder::Config);
    ensureDirectoryExists(SpecialFolder::Data);
    ensureDirectoryExists(SpecialFolder::Downloads);
}

// to generate correct call to ProfilePrivate::~ProfileImpl()
Profile::~Profile() = default;

void Profile::initialize(const QString &rootProfilePath, const QString &configurationName,
                         bool convertPathsToProfileRelative)
{
    QScopedPointer<Private::Profile> profile(rootProfilePath.isEmpty()
                                             ? static_cast<Private::Profile *>(new Private::DefaultProfile(configurationName))
                                             : static_cast<Private::Profile *>(new Private::CustomProfile(rootProfilePath, configurationName)));

    QScopedPointer<Private::PathConverter> converter(convertPathsToProfileRelative
                                                     ? static_cast<Private::PathConverter *>(new Private::Converter(profile->baseDirectory()))
                                                     : static_cast<Private::PathConverter *>(new Private::NoConvertConverter()));
    m_instance = new Profile(profile.take(), converter.take());
}

const Profile &Profile::instance()
{
    return *m_instance;
}

QString Profile::location(SpecialFolder folder) const
{
    QString result;
    switch (folder) {
    case SpecialFolder::Cache:
        result = m_profileImpl->cacheLocation();
        break;
    case SpecialFolder::Config:
        result = m_profileImpl->configLocation();
        break;
    case SpecialFolder::Data:
        result = m_profileImpl->dataLocation();
        break;
    case SpecialFolder::Downloads:
        result = m_profileImpl->downloadLocation();
        break;
    }

    if (!result.endsWith(QLatin1Char('/')))
        result += QLatin1Char('/');
    return result;
}

QString Profile::profileName() const
{
    return m_profileImpl->profileName();
}

SettingsPtr Profile::applicationSettings(const QString &name) const
{
    return m_profileImpl->applicationSettings(name);
}

void Profile::ensureDirectoryExists(SpecialFolder folder)
{
    QString locationPath = location(folder);
    if (!locationPath.isEmpty() && !QDir().mkpath(locationPath))
        qFatal("Could not create required directory '%s'", qUtf8Printable(locationPath));
}

QString Profile::toPortablePath(const QString &absolutePath) const
{
    return m_pathConverterImpl->toPortablePath(absolutePath);
}

QString Profile::fromPortablePath(const QString &portablePath) const
{
    return m_pathConverterImpl->fromPortablePath(portablePath);
}

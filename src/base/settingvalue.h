/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2016  Vladimir Golovnev <glassez@yandex.ru>
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

#ifndef SETTINGVALUE_H
#define SETTINGVALUE_H

#include <functional>
#include <type_traits>
#include <QMetaEnum>
#include <QString>
#include <QVariant>

#include "settingsstorage.h"

template <typename T>
class CachedSettingValue
{
    using ProxyFunc = std::function<T (const T&)>;

public:
    explicit CachedSettingValue(const char *keyName, const T &defaultValue = T())
        : m_keyName(QLatin1String(keyName))
        , m_value(loadValue(defaultValue))
    {
    }

    explicit CachedSettingValue(const char *keyName, const T &defaultValue
            , const ProxyFunc &proxyFunc)
        : m_keyName(QLatin1String(keyName))
        , m_value(proxyFunc(loadValue(defaultValue)))
    {
    }

    T value() const
    {
        return m_value;
    }

    operator T() const
    {
        return value();
    }

    CachedSettingValue<T> &operator=(const T &newValue)
    {
        m_value = newValue;
        storeValue(m_value);
        return *this;
    }

private:
    // regular load/save pair
    template <typename U, typename std::enable_if<!std::is_enum<U>::value, int>::type = 0>
    U loadValue(const U &defaultValue)
    {
        return SettingsStorage::instance()->loadValue(m_keyName, defaultValue).template value<T>();
    }

    template <typename U, typename std::enable_if<!std::is_enum<U>::value, int>::type = 0>
    void storeValue(const U &value)
    {
        SettingsStorage::instance()->storeValue(m_keyName, value);
    }

    // load/save pair for an enum
    // saves literal value of the enum constant, obtained from QMetaEnum
    template <typename U, typename std::enable_if<std::is_enum<U>::value, int>::type = 0>
    U loadValue(const U &defaultValue)
    {
        static_assert(std::is_same<int, typename std::underlying_type<U>::type>::value,
                      "Enumeration underlying type has to be int");

        bool ok = false;
        const U res = static_cast<U>(QMetaEnum::fromType<U>().keyToValue(
            SettingsStorage::instance()->loadValue(m_keyName).toString().toLatin1().constData(), &ok));
        return ok ? res : defaultValue;
    }

    template <typename U, typename std::enable_if<std::is_enum<U>::value, int>::type = 0>
    void storeValue(const U &value)
    {
        SettingsStorage::instance()->storeValue(m_keyName,
            QString::fromLatin1(QMetaEnum::fromType<U>().valueToKey(static_cast<int>(value))));
    }

    const QString m_keyName;
    T m_value;
};

#endif // SETTINGVALUE_H

/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "utility_win.h"
#include "utility.h"

#include "asserts.h"
#include "filesystembase.h"

#include <comdef.h>
#include <qt_windows.h>
#include <shlguid.h>
#include <shlobj.h>
#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QSettings>

extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;

namespace {
const QString systemRunPathC()
{
    return QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

const QString runPathC()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

const QString systemThemesC()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
}

}

namespace OCC {

void Utility::setupFavLink(const QString &) { }

bool Utility::hasSystemLaunchOnStartup(const QString &appName)
{
    QSettings settings(systemRunPathC(), QSettings::NativeFormat);
    return settings.contains(appName);
}

bool Utility::hasLaunchOnStartup(const QString &appName)
{
    QSettings settings(runPathC(), QSettings::NativeFormat);
    return settings.contains(appName);
}

void Utility::setLaunchOnStartup(const QString &appName, const QString &guiName, bool enable)
{
    Q_UNUSED(guiName)
    QSettings settings(runPathC(), QSettings::NativeFormat);
    if (enable) {
        settings.setValue(appName, QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
        settings.remove(appName);
    }
}

bool Utility::hasDarkSystray()
{
    const QSettings settings(systemThemesC(), QSettings::NativeFormat);
    return !settings.value(QStringLiteral("SystemUsesLightTheme"), false).toBool();
}

void Utility::UnixTimeToFiletime(time_t t, FILETIME *filetime)
{
    LONGLONG ll = (t * 10000000LL) + 116444736000000000LL;
    filetime->dwLowDateTime = (DWORD)ll;
    filetime->dwHighDateTime = ll >> 32;
}

void Utility::FiletimeToLargeIntegerFiletime(const FILETIME *filetime, LARGE_INTEGER *hundredNSecs)
{
    hundredNSecs->LowPart = filetime->dwLowDateTime;
    hundredNSecs->HighPart = filetime->dwHighDateTime;
}

void Utility::UnixTimeToLargeIntegerFiletime(time_t t, LARGE_INTEGER *hundredNSecs)
{
    hundredNSecs->QuadPart = (t * 10000000LL) + 116444736000000000LL;
}


QString Utility::formatWinError(long errorCode)
{
    return QStringLiteral("WindowsError: %1: %2").arg(QString::number(errorCode, 16), QString::fromWCharArray(_com_error(errorCode).ErrorMessage()));
}


Utility::NtfsPermissionLookupRAII::NtfsPermissionLookupRAII()
{
    qt_ntfs_permission_lookup++;
}

Utility::NtfsPermissionLookupRAII::~NtfsPermissionLookupRAII()
{
    qt_ntfs_permission_lookup--;
}


Utility::Handle::Handle(HANDLE h, std::function<void(HANDLE)> &&close, uint32_t error)
    : _handle(h)
    , _close(std::move(close))
    , _error(error)
{
    if (_handle == INVALID_HANDLE_VALUE && _error == NO_ERROR) {
        _error = GetLastError();
    }
}

Utility::Handle Utility::Handle::createHandle(const std::filesystem::path &path, const CreateHandleParameter &p)
{
    uint32_t flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS;
    if (!p.followSymlinks) {
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    }
    if (p.async) {
        flags |= FILE_FLAG_OVERLAPPED;
    }
    return Utility::Handle{CreateFileW(path.native().data(), p.accessMode, p.shareMode, nullptr, p.creationFlags, flags, nullptr)};
}

Utility::Handle::Handle(HANDLE h)
    : Handle(h, &CloseHandle)
{
}

Utility::Handle::~Handle()
{
    close();
}

void Utility::Handle::close()
{
    if (_handle != INVALID_HANDLE_VALUE) {
        _close(_handle);
        _handle = INVALID_HANDLE_VALUE;
    }
}

uint32_t Utility::Handle::error() const
{
    return _error;
}

bool Utility::Handle::hasError() const
{
    return _error != NO_ERROR;
}

QString Utility::Handle::errorMessage() const
{
    return formatWinError(_error);
}


} // namespace OCC

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

#include "libsync/common/asserts.h"
#include "libsync/common/filesystembase.h"
#include "libsync/filesystem.h"
#include "resources/jsontheme.h"

#include <comdef.h>
#include <qt_windows.h>
#include <shlguid.h>
#include <shlobj.h>
#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

using namespace Qt::Literals::StringLiterals;

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

void Utility::setupDesktopIni(const QString &folder, const QString &displayName, const QString &description, bool override)
{
    const QFileInfo desktopIniPath{u"%1/Desktop.ini"_s.arg(folder)};
    if (!desktopIniPath.exists() || override) {
        // we can't use QSettings as we require the file to be utf16 encoded
        qCInfo(lcUtility) << u"Creating" << desktopIniPath.filePath() << u"to set a folder icon in Explorer.";
        QFile file(desktopIniPath.filePath());
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream out(&file);
            out.setEncoding(QStringConverter::Utf16);
            out.setGenerateByteOrderMark(true);
            const QString eol = u"\r\n"_s;
            out << u"[.ShellClassInfo]"_s << eol << //
                u"IconResource="_s << QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) << eol << //
                u"ConfirmFileOp=1"_s << eol;
            if (!displayName.isEmpty()) {
                out << u"LocalizedResourcename="_s << displayName << eol;
            }
            out << u"InfoTip="_s;
            if (description.isEmpty()) {
                out << Resources::JsonTheme::instance().applicationDisplayName();
                if (!displayName.isEmpty()) {
                    out << u" - "_s << displayName;
                }
            } else {
                out << description;
            }
            out << eol;
        } else {
            qCWarning(lcUtility) << u"Failed to write" << desktopIniPath.filePath() << file.errorString();
        }
    }

    const QString longFolderPath = FileSystem::longWinPath(folder);
    const QString longDesktopIniPath = FileSystem::longWinPath(desktopIniPath.absoluteFilePath());
    // Set the folder as system and Desktop.ini as hidden+system for explorer to pick it.
    // https://msdn.microsoft.com/en-us/library/windows/desktop/cc144102
    const DWORD folderAttrs = GetFileAttributesW(reinterpret_cast<const wchar_t *>(longFolderPath.utf16()));
    if (!SetFileAttributesW(reinterpret_cast<const wchar_t *>(longFolderPath.utf16()), folderAttrs | FILE_ATTRIBUTE_SYSTEM)) {
        const auto error = GetLastError();
        qCWarning(lcUtility) << u"SetFileAttributesW failed on" << longFolderPath << Utility::formatWinError(error);
    }
    if (!SetFileAttributesW(reinterpret_cast<const wchar_t *>(longDesktopIniPath.utf16()), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
        const auto error = GetLastError();
        qCWarning(lcUtility) << u"SetFileAttributesW failed on" << longDesktopIniPath << Utility::formatWinError(error);
    }
}

void Utility::setupFavLink(const QString &folder, const QString &displayName, const QString &description, bool override)
{
    setupDesktopIni(folder, displayName, description, override);
}

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


void Utility::UnixTimeToLargeIntegerFiletime(time_t t, LARGE_INTEGER *hundredNSecs)
{
    hundredNSecs->QuadPart = FileSystem::time_tToFileTime(t).time_since_epoch().count();
}

QString Utility::formatWinError(long errorCode)
{
    return QStringLiteral("WindowsError: 0x%1: %2")
        .arg(QString::number(static_cast<ulong>(errorCode), 16), QString::fromWCharArray(_com_error(errorCode).ErrorMessage()));
}

Utility::Handle::Handle(HANDLE h, const std::filesystem::path &path, std::function<void(HANDLE)> &&close, uint32_t error)
    : _handle(h)
    , _close(std::move(close))
    , _error(error)
    , _path(path)
{
    if (_handle == INVALID_HANDLE_VALUE && _error == NO_ERROR) {
        _error = GetLastError();
    }
}

Utility::Handle Utility::Handle::createHandle(const std::filesystem::path &path, const CreateHandleParameter &p, std::function<void(HANDLE)> &&close)
{
    uint32_t flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS;
    if (!p.followSymlinks) {
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    }
    if (p.async) {
        flags |= FILE_FLAG_OVERLAPPED;
    }
    return Utility::Handle{
        CreateFileW(path.lexically_normal().native().data(), p.accessMode, p.shareMode, nullptr, p.creationFlags, flags, nullptr), path, std::move(close)};
}

Utility::Handle::Handle(HANDLE h, const std::filesystem::path &path)
    : Handle(h, path, &CloseHandle)
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

const std::filesystem::path &Utility::Handle::path() const
{
    return _path;
}

QString Utility::Handle::errorMessage() const
{
    return formatWinError(_error);
}


} // namespace OCC

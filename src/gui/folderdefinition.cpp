/*
 * Copyright (C) by Hannah von Reth <h.vonreth@opencloud.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "folderdefinition.h"

#include <QDir>
#include <QSettings>

using namespace OCC;

Q_LOGGING_CATEGORY(lcFolder, "gui.folder.definition", QtInfoMsg)

namespace {

auto davUrlC()
{
    return "davUrl";
}

auto spaceIdC()
{
    return "spaceId";
}

auto displayNameC()
{
    return "displayString";
}

auto deployedC()
{
    return "deployed";
}

auto priorityC()
{
    return "priority";
}
}

FolderDefinition::FolderDefinition(const QUuid &accountUuid, const QUrl &davUrl, const QString &spaceId, const QString &displayName)
    : _webDavUrl(davUrl)
    , _spaceId(spaceId)
    , _displayName(displayName)
    , _accountUUID(accountUuid)
{
    Q_ASSERT(!_accountUUID.isNull());
}

void FolderDefinition::setPriority(uint32_t newPriority)
{
    _priority = newPriority;
}

QUuid FolderDefinition::accountUUID() const
{
    return _accountUUID;
}

uint32_t FolderDefinition::priority() const
{
    return _priority;
}

void FolderDefinition::save(QSettings &settings, const FolderDefinition &folder)
{
    settings.setValue("accountUUID", folder.accountUUID());
    settings.setValue("localPath", folder.localPath());
    settings.setValue("journalPath", folder.journalPath);
    settings.setValue(spaceIdC(), folder.spaceId());
    settings.setValue(davUrlC(), folder.webDavUrl());
    settings.setValue(displayNameC(), folder.displayName());
    settings.setValue("paused", folder.paused);
    settings.setValue("ignoreHiddenFiles", folder.ignoreHiddenFiles);
    settings.setValue(deployedC(), folder.isDeployed());
    settings.setValue(priorityC(), folder.priority());

    settings.setValue("virtualFilesMode", Utility::enumToString(folder.virtualFilesMode));
}

FolderDefinition FolderDefinition::load(QSettings &settings)
{
    FolderDefinition folder{settings.value("accountUUID").toUuid(), settings.value(davUrlC()).toUrl(), settings.value(spaceIdC()).toString(),
        settings.value(displayNameC()).toString()};

    folder.setLocalPath(settings.value("localPath").toString());
    folder.journalPath = settings.value("journalPath").toString();
    folder.paused = settings.value("paused").toBool();
    folder.ignoreHiddenFiles = settings.value("ignoreHiddenFiles", QVariant(true)).toBool();
    folder._deployed = settings.value(deployedC(), false).toBool();
    folder._priority = settings.value(priorityC(), 0).toUInt();

    folder.virtualFilesMode = Vfs::Off;

    QString vfsModeString = settings.value("virtualFilesMode").toString();
#ifdef Q_OS_WIN
    // we always use vfs on windows if available
    if (auto result = Vfs::checkAvailability(folder.localPath(), Vfs::WindowsCfApi); result) {
        vfsModeString = Utility::enumToString(Vfs::WindowsCfApi);
    } else {
        qCWarning(lcFolder) << u"Failed to upgrade" << folder.localPath() << u"to" << Vfs::WindowsCfApi << result.error();
    }
#endif
    if (!vfsModeString.isEmpty()) {
        if (auto mode = Vfs::modeFromString(vfsModeString)) {
            folder.virtualFilesMode = *mode;
        } else {
            qCWarning(lcFolder) << u"Unknown virtualFilesMode:" << vfsModeString << u"assuming 'off'";
        }
    }
    return folder;
}

void FolderDefinition::setLocalPath(const QString &path)
{
    _localPath = QDir::fromNativeSeparators(path);
    if (!_localPath.endsWith(QLatin1Char('/'))) {
        _localPath.append(QLatin1Char('/'));
    }
}

QString FolderDefinition::absoluteJournalPath() const
{
    return QDir(localPath()).filePath(journalPath);
}

QString FolderDefinition::displayName() const
{
    return _displayName;
}

void FolderDefinition::setDisplayName(const QString &s)
{
    _displayName = s;
}

bool FolderDefinition::isDeployed() const
{
    return _deployed;
}

QUrl FolderDefinition::webDavUrl() const
{
    Q_ASSERT(_webDavUrl.isValid());
    return _webDavUrl;
}

QString FolderDefinition::localPath() const
{
    return _localPath;
}

QString FolderDefinition::spaceId() const
{
    // we might call the function to check for the id
    // anyhow one of the conditions needs to be true
    Q_ASSERT(_webDavUrl.isValid() || !_spaceId.isEmpty());
    return _spaceId;
}

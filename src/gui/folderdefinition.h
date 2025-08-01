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

#pragma once

#include "common/vfs.h"
#include "gui/opencloudguilib.h"

#include <QString>
#include <QUrl>
#include <QUuid>

namespace OCC {


/**
 * @brief The FolderDefinition class
 * @ingroup gui
 */
class OPENCLOUD_GUI_EXPORT FolderDefinition
{
public:
    FolderDefinition(const QUuid &accountUuid, const QUrl &davUrl, const QString &spaceId, const QString &displayName);
    /// path to the journal, usually relative to localPath
    QString journalPath;

    /// whether the folder is paused
    bool paused = false;
    /// whether the folder syncs hidden files
    bool ignoreHiddenFiles = true;
    /// Which virtual files setting the folder uses
    Vfs::Mode virtualFilesMode = Vfs::Off;

    /// Saves the folder definition into the current settings.
    static void save(QSettings &settings, const FolderDefinition &folder);

    /// Reads a folder definition from the current settings.
    static FolderDefinition load(QSettings &settings);

    /// Ensure / as separator and trailing /.
    void setLocalPath(const QString &path);

    /// journalPath relative to localPath.
    QString absoluteJournalPath() const;

    QString localPath() const;

    QUrl webDavUrl() const;

    // could change in the case of spaces
    void setWebDavUrl(const QUrl &url) { _webDavUrl = url; }

    // when using spaces we don't store the dav URL but the space id
    // this id is then used to look up the dav URL
    QString spaceId() const;

    void setSpaceId(const QString &spaceId) { _spaceId = spaceId; }

    QString displayName() const;
    void setDisplayName(const QString &s);

    /**
     * The folder is deployed by an admin
     * We will hide the remove option and the disable/enable vfs option.
     */
    bool isDeployed() const;

    /**
     * Higher values mean more imortant
     * Used for sorting
     */
    uint32_t priority() const;

    void setPriority(uint32_t newPriority);

    QUuid accountUUID() const;

private:
    QUrl _webDavUrl;

    QString _spaceId;
    QString _displayName;
    /// path on local machine (always trailing /)
    QString _localPath;
    bool _deployed = false;

    uint32_t _priority = 0;

    QUuid _accountUUID;
};

}

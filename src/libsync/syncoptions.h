/*
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include "common/filesystembase.h"
#include "common/vfs.h"
#include "opencloudsynclib.h"

#include <QRegularExpression>
#include <QSharedPointer>
#include <QString>

#include <chrono>
#include <functional>


namespace OCC {

/**
 * Value class containing the options given to the sync engine
 */
class OPENCLOUD_SYNC_EXPORT SyncOptions
{
public:
    explicit SyncOptions(QSharedPointer<Vfs> vfs);
    ~SyncOptions();

    /** If remotely deleted files are needed to move to trash */
    bool _moveFilesToTrash = false;

    /** Create a virtual file for new files instead of downloading. May not be null */
    QSharedPointer<Vfs> _vfs;

    /** The maximum number of active jobs in parallel  */
    std::function<int()> _parallelNetworkJobs = [] { return 6; };

    /** A regular expression to match file names
     * If no pattern is provided the default is an invalid regular expression.
     */
    QRegularExpression fileRegex() const;


    // TODO: remove
    /**
     * A pattern like *.txt, matching only file names
     */
    void setFilePattern(const QString &pattern);

    /**
     * A pattern like /own.*\/.*txt matching the full path
     */
    void setPathPattern(const QString &pattern);


private:
    /**
     * Only sync files that mathc the expression
     * Invalid pattern by default.
     */
    QRegularExpression _fileRegex = QRegularExpression(QStringLiteral("("));
};

}

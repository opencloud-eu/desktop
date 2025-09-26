// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/accountfwd.h"

#include <QObject>
#include <QVersionNumber>

namespace OCC {
class UpdateNotifier : public QObject
{
    Q_OBJECT
public:
    UpdateNotifier(QObject *parent = nullptr);

    /**
     * Check whether there is a new release of the application available.
     * This will only check once per application start.
     */
    void checkForUpdates(const AccountPtr &account);

protected:
    QString channel() const;

    /**
     * Returns the current version of the application
     * When using a beta build we return 4 parts version including the build number
     * for stable releases we return the 3 part semver version
     */
    QVersionNumber version() const;

private:
    bool _checkedForUpdate = false;
};
}

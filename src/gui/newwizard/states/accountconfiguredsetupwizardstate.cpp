/*
 * Copyright (C) Fabian Müller <fmueller@owncloud.com>
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

#include "gui/newwizard/states/accountconfiguredsetupwizardstate.h"
#include "gui/folderman.h"
#include "gui/newwizard/pages/accountconfiguredwizardpage.h"
#include "libsync/filesystem.h"

namespace OCC::Wizard {

AccountConfiguredSetupWizardState::AccountConfiguredSetupWizardState(SetupWizardContext *context)
    : AbstractSetupWizardState(context)
{
    // We need some sync root for spaces. It's never a Space folder.
    // We pass an invalid UUID, because we don't "own" a syncroot yet, and all checks against UUIDs should fail.
    const QString defaultSyncTargetDir = FolderMan::suggestSyncFolder(FolderMan::NewFolderType::SpacesSyncRoot, {});
    QString syncTargetDir = _context->accountBuilder().syncTargetDir();

    if (syncTargetDir.isEmpty()) {
        syncTargetDir = defaultSyncTargetDir;
    }

    _page = new AccountConfiguredWizardPage(defaultSyncTargetDir, syncTargetDir);
}

SetupWizardState AccountConfiguredSetupWizardState::state() const
{
    return SetupWizardState::AccountConfiguredState;
}

void AccountConfiguredSetupWizardState::evaluatePage()
{
    auto accountConfiguredSetupWizardPage = qobject_cast<AccountConfiguredWizardPage *>(_page);
    Q_ASSERT(accountConfiguredSetupWizardPage != nullptr);

    if (accountConfiguredSetupWizardPage->syncMode() != Wizard::SyncMode::ConfigureUsingFolderWizard) {
        QString syncTargetDir = QDir::fromNativeSeparators(accountConfiguredSetupWizardPage->syncTargetDir());

        // make sure we remember it now so we can show it to the user again upon failures
        _context->accountBuilder().setSyncTargetDir(syncTargetDir);

        const QString errorMessageTemplate = tr("Invalid local download directory: %1");

        if (!QDir::isAbsolutePath(syncTargetDir)) {
            Q_EMIT evaluationFailed(errorMessageTemplate.arg(QStringLiteral("path must be absolute")));
            return;
        }

        if (auto result = Vfs::checkAvailability(syncTargetDir, VfsPluginManager::instance().bestAvailableVfsMode()); !result) {
            Q_EMIT evaluationFailed(errorMessageTemplate.arg(result.error()));
            return;
        }

        // Doesn't matter wether it's a spaces sync root
        QString invalidPathErrorMessage = FolderMan::checkPathValidityRecursive(syncTargetDir, FolderMan::NewFolderType::SpacesSyncRoot, {});
        if (!invalidPathErrorMessage.isEmpty()) {
            Q_EMIT evaluationFailed(errorMessageTemplate.arg(invalidPathErrorMessage));
            return;
        }
    }

    Q_EMIT evaluationSuccessful();
}

} // OCC::Wizard

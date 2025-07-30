/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
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

#include "folderwizard.h"
#include "folderwizard_p.h"

#include "folderwizardlocalpath.h"
#include "folderwizardselectivesync.h"

#include "spacespage.h"

#include "account.h"
#include "gui/application.h"
#include "gui/settingsdialog.h"
#include "theme.h"

#include "gui/accountstate.h"
#include "gui/folderman.h"

#include "libsync/graphapi/space.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>

#include <stdlib.h>

namespace OCC {

Q_LOGGING_CATEGORY(lcFolderWizard, "gui.folderwizard", QtInfoMsg)

QString FolderWizardPrivate::formatWarnings(const QStringList &warnings, bool isError)
{
    QString ret;
    if (warnings.count() == 1) {
        ret = isError ? QCoreApplication::translate("FolderWizard", "<b>Error:</b> %1").arg(warnings.first()) : QCoreApplication::translate("FolderWizard", "<b>Warning:</b> %1").arg(warnings.first());
    } else if (warnings.count() > 1) {
        QStringList w2;
        for (const auto &warning : warnings) {
            w2.append(QStringLiteral("<li>%1</li>").arg(warning));
        }
        ret = isError ? QCoreApplication::translate("FolderWizard", "<b>Error:</b><ul>%1</ul>").arg(w2.join(QString()))
                      : QCoreApplication::translate("FolderWizard", "<b>Warning:</b><ul>%1</ul>").arg(w2.join(QString()));
    }

    return ret;
}

QString FolderWizardPrivate::defaultSyncRoot() const
{
    if (!_account->account()->hasDefaultSyncRoot()) {
        return FolderMan::suggestSyncFolder(FolderMan::NewFolderType::SpacesSyncRoot, _account->account()->uuid());
    } else {
        return _account->account()->defaultSyncRoot();
    }
}

FolderWizardPrivate::FolderWizardPrivate(FolderWizard *q, const AccountStatePtr &account)
    : q_ptr(q)
    , _account(account)
    , _spacesPage(new SpacesPage(account->account(), q))
{
    if (!_account->account()->hasDefaultSyncRoot()) {
        _folderWizardSourcePage = new FolderWizardLocalPath(this);
        q->setPage(FolderWizard::Page_Source, _folderWizardSourcePage);
    }

    q->setPage(FolderWizard::Page_Space, _spacesPage);

    if (VfsPluginManager::instance().bestAvailableVfsMode() != Vfs::WindowsCfApi) {
        _folderWizardSelectiveSyncPage = new FolderWizardSelectiveSync(this);
        q->setPage(FolderWizard::Page_SelectiveSync, _folderWizardSelectiveSyncPage);
    }
}

QString FolderWizardPrivate::localPath() const
{
    return FolderMan::findGoodPathForNewSyncFolder(
        defaultSyncRoot(), _spacesPage->currentSpace()->displayName(), FolderMan::NewFolderType::SpacesFolder, _account->account()->uuid());
}

uint32_t FolderWizardPrivate::priority() const
{
    return _spacesPage->currentSpace()->priority();
}

QUrl FolderWizardPrivate::davUrl() const
{
    auto url = _spacesPage->currentSpace()->webdavUrl();
    if (!url.path().endsWith(QLatin1Char('/'))) {
        url.setPath(url.path() + QLatin1Char('/'));
    }
    return url;
}

QString FolderWizardPrivate::spaceId() const
{
    return _spacesPage->currentSpace()->id();
}

QString FolderWizardPrivate::displayName() const
{
    return _spacesPage->currentSpace()->displayName();
}

const AccountStatePtr &FolderWizardPrivate::accountState()
{
    return _account;
}

bool FolderWizardPrivate::useVirtualFiles() const
{
    return VfsPluginManager::instance().bestAvailableVfsMode() == Vfs::WindowsCfApi;
}

FolderWizard::FolderWizard(const AccountStatePtr &account, QWidget *parent)
    : QWizard(parent)
    , d_ptr(new FolderWizardPrivate(this, account))
{
    setWindowTitle(tr("Add Space"));
    setOptions(QWizard::CancelButtonOnLeft);
    setButtonText(QWizard::FinishButton, tr("Add Space"));
    setWizardStyle(QWizard::ModernStyle);
}

FolderWizard::~FolderWizard()
{
}

FolderMan::SyncConnectionDescription FolderWizard::result()
{
    Q_D(FolderWizard);
    if (d->_folderWizardSourcePage) {
        d->_account->account()->setDefaultSyncRoot(d->_folderWizardSourcePage->localPath());
    }
    const QString localPath = d->localPath();
    // the local path must be a child of defaultSyncRoot
    Q_ASSERT(FileSystem::isChildPathOf(localPath, d->defaultSyncRoot()));

    return {
        d->davUrl(), //
        d->spaceId(), //
        localPath, //
        d->displayName(), //
        d->useVirtualFiles(), //
        d->priority(), //
        d->_folderWizardSelectiveSyncPage ? d->_folderWizardSelectiveSyncPage->selectiveSyncBlackList() : QSet<QString>{} //
    };
}

} // end namespace

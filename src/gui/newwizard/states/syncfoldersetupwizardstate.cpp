

#include "gui/newwizard/states/syncfoldersetupwizardstate.h"
#include "gui/newwizard/pages/syncfoldersetupwizardpage.h"
#include "gui/folderman.h"

namespace OCC::Wizard {

SyncFolderSetupWizardState::SyncFolderSetupWizardState(SetupWizardContext *context)
    : AbstractSetupWizardState(context)
{
    const QString defaultSyncFolder = FolderMan::suggestSyncFolder(FolderMan::NewFolderType::SpacesSyncRoot, {});
    QString syncTargetDir = _context->accountBuilder().syncTargetDir();

    if (syncTargetDir.isEmpty()) {
        syncTargetDir = defaultSyncFolder;
    }

    _page = new SyncFolderSetupWizardPage(syncTargetDir);
}

SetupWizardState SyncFolderSetupWizardState::state() const
{
    return SetupWizardState::SyncFolderSetupState;
}

void SyncFolderSetupWizardState::evaluatePage()
{
    auto *syncFolderPage = qobject_cast<SyncFolderSetupWizardPage *>(_page);
    Q_ASSERT(syncFolderPage != nullptr);

    const QString syncTargetDir = syncFolderPage->syncFolder();
    _context->accountBuilder().setSyncTargetDir(syncTargetDir);

    Q_EMIT evaluationSuccessful();
}

} // OCC::Wizard

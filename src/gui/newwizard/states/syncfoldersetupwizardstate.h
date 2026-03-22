
#pragma once

#include "gui/newwizard/states/abstractsetupwizardstate.h"

namespace OCC::Wizard {

class SyncFolderSetupWizardState : public AbstractSetupWizardState
{
    Q_OBJECT

public:
    explicit SyncFolderSetupWizardState(SetupWizardContext *context);

    SetupWizardState state() const override;
    void evaluatePage() override;
};

} // OCC::Wizard

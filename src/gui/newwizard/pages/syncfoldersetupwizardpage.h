
#pragma once

#include "gui/newwizard/pages/abstractsetupwizardpage.h"

#include <memory>

namespace Ui {
class SyncFolderSetupWizardPage;
}

namespace OCC::Wizard {

class SyncFolderSetupWizardPage : public AbstractSetupWizardPage
{
    Q_OBJECT

public:
    explicit SyncFolderSetupWizardPage(const QString &defaultSyncFolder, QWidget *parent = nullptr);
    ~SyncFolderSetupWizardPage() override;

    QString syncFolder() const;
    bool validateInput() const override;

Q_SIGNALS:
    void contentChanged() override;

private:
    std::unique_ptr<Ui::SyncFolderSetupWizardPage> _ui;
};

} // OCC::Wizard

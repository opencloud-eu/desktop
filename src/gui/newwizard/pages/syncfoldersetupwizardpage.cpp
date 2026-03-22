#include "syncfoldersetupwizardpage.h"

#include <QFileDialog>

#include "libsync/theme.h"
#include "ui_syncfoldersetupwizardpage.h"

namespace OCC::Wizard {

SyncFolderSetupWizardPage::SyncFolderSetupWizardPage(const QString &defaultSyncFolder, QWidget *parent)
    : _ui(std::make_unique<Ui::SyncFolderSetupWizardPage>())
{
    _ui->setupUi(this);
    _ui->syncFolderLineEdit->setText(QDir::toNativeSeparators(defaultSyncFolder));

    connect(_ui->chooseFolderButton, &QToolButton::clicked, this, [this, defaultSyncFolder]() {
        auto dialog = new QFileDialog(this, tr("Select the sync folder location"), defaultSyncFolder);
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly);
        dialog->setOption(QFileDialog::DontCreateDirectories);

        connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &directory) {
            _ui->syncFolderLineEdit->setText(QDir::toNativeSeparators(directory));
            Q_EMIT contentChanged();
        });
        dialog->open();
    });
}

SyncFolderSetupWizardPage::~SyncFolderSetupWizardPage() noexcept
{
    delete _ui;
}

QString SyncFolderSetupWizardPage::syncFolder() const
{
    return QDir::fromNativeSeparators(_ui->syncFolderLineEdit->text());
}

bool SyncFolderSetupWizardPage::validateInput() const
{
    return !syncFolder().isEmpty();
}

} // OCC::Wizard

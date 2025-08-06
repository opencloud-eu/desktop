#include "accountconfiguredwizardpage.h"

#include "common/vfs.h"
#include "gui/messagebox.h"
#include "ui_accountconfiguredwizardpage.h"

#include "libsync/theme.h"
#include "resources/fonticon.h"


#include <QDir>
#include <QFileDialog>


namespace OCC::Wizard {

AccountConfiguredWizardPage::AccountConfiguredWizardPage(const QString &defaultSyncTargetDir, const QString &userChosenSyncTargetDir)
    : _ui(new ::Ui::AccountConfiguredWizardPage)
{
    _ui->setupUi(this);

    // by default, sync everything to an automatically chosen directory, VFS use depends on the OS
    // the defaults are provided by the controller
    _ui->localDirectoryLineEdit->setText(QDir::toNativeSeparators(userChosenSyncTargetDir));
    _ui->syncEverythingRadioButton->setChecked(true);

    // just adjusting the visibility should be sufficient for these branding options
    if (Theme::instance()->wizardSkipAdvancedPage()) {
        _ui->advancedConfigGroupBox->setVisible(false);
    }

    connect(_ui->chooseLocalDirectoryButton, &QToolButton::clicked, this, [this]() {
        auto dialog = new QFileDialog(this, tr("Select the local folder"), _ui->localDirectoryLineEdit->text());
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly);

        connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &directory) {
            // the directory chooser should guarantee that the directory exists
            Q_ASSERT(QDir(directory).exists());

            if (auto result = Vfs::checkAvailability(directory, VfsPluginManager::instance().bestAvailableVfsMode()); !result) {
                auto *box = new MessageBox({u''}, tr("Sync location not supported"), result.error(), QMessageBox::Ok, this);
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->open();
                return;
            }

            _ui->localDirectoryLineEdit->setText(QDir::toNativeSeparators(directory));
        });
        dialog->open();
    });

    connect(_ui->advancedConfigGroupBox, &QGroupBox::toggled, this, [this](bool enabled) {
        // layouts cannot be hidden, therefore we use a plain widget within the group box to "house" the contained widgets
        _ui->advancedConfigGroupBoxContentWidget->setVisible(enabled);
    });

    // for selective sync, we run the folder wizard right after this wizard, thus don't have to specify a local directory
    connect(_ui->configureSyncManuallyRadioButton, &QRadioButton::toggled, this, [this](bool checked) {
        _ui->localDirectoryGroupBox->setEnabled(!checked);
    });

    // toggle once to have the according handlers set up the initial UI state
    _ui->advancedConfigGroupBox->setChecked(true);
    _ui->advancedConfigGroupBox->setChecked(false);

    // allows resetting local directory to default value once changed
    _ui->resetLocalDirectoryButton->setIcon(Resources::FontIcon(u''));
    _ui->chooseLocalDirectoryButton->setIcon(Resources::FontIcon(u''));
    auto enableResetLocalDirectoryButton = [this, defaultSyncTargetDir]() {
        return _ui->localDirectoryLineEdit->text() != QDir::toNativeSeparators(defaultSyncTargetDir);
    };
    _ui->resetLocalDirectoryButton->setEnabled(enableResetLocalDirectoryButton());
    connect(_ui->localDirectoryLineEdit, &QLineEdit::textChanged, this,
        [this, enableResetLocalDirectoryButton]() { _ui->resetLocalDirectoryButton->setEnabled(enableResetLocalDirectoryButton()); });
    connect(_ui->resetLocalDirectoryButton, &QToolButton::clicked, this,
        [this, defaultSyncTargetDir]() { _ui->localDirectoryLineEdit->setText(QDir::toNativeSeparators(defaultSyncTargetDir)); });
}

AccountConfiguredWizardPage::~AccountConfiguredWizardPage() noexcept
{
    delete _ui;
}

QString AccountConfiguredWizardPage::syncTargetDir() const
{
    return QDir::toNativeSeparators(_ui->localDirectoryLineEdit->text());
}

SyncMode AccountConfiguredWizardPage::syncMode() const
{
    if (_ui->syncEverythingRadioButton->isChecked()) {
#ifdef Q_OS_WIN
        if (Vfs::checkAvailability(syncTargetDir(), Vfs::WindowsCfApi)) {
            return SyncMode::UseVfs;
        }
#endif
        return SyncMode::SyncEverything;
    }
    if (_ui->configureSyncManuallyRadioButton->isChecked()) {
        return SyncMode::ConfigureUsingFolderWizard;
    }
    Q_UNREACHABLE();
}

bool AccountConfiguredWizardPage::validateInput() const
{
    // nothing to validate here
    return true;
}

void AccountConfiguredWizardPage::setShowAdvancedSettings(bool showAdvancedSettings)
{
    _ui->advancedConfigGroupBox->setChecked(showAdvancedSettings);
}
}

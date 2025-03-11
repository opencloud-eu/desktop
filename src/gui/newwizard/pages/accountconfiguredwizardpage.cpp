#include "accountconfiguredwizardpage.h"

#include "ui_accountconfiguredwizardpage.h"

#include "gui/application.h"
#include "libsync/theme.h"
#include "resources/fonticon.h"


#include <QDir>
#include <QFileDialog>


namespace OCC::Wizard {

AccountConfiguredWizardPage::AccountConfiguredWizardPage(
    const QString &defaultSyncTargetDir, const QString &userChosenSyncTargetDir, bool vfsIsAvailable, bool enableVfsByDefault)
    : _ui(new ::Ui::AccountConfiguredWizardPage)
{
    _ui->setupUi(this);

    // by default, sync everything to an automatically chosen directory, VFS use depends on the OS
    // the defaults are provided by the controller
    _ui->localDirectoryLineEdit->setText(QDir::toNativeSeparators(userChosenSyncTargetDir));
    _ui->syncEverythingRadioButton->setChecked(true);

    _ui->useVfsRadioButton->setVisible(vfsIsAvailable);

    _ui->useVfsRadioButton->setText(tr("Use &virtual files instead of downloading content immediately"));

    // just adjusting the visibility should be sufficient for these branding options
    if (Theme::instance()->wizardSkipAdvancedPage()) {
        _ui->advancedConfigGroupBox->setVisible(false);
    }

    if (!Theme::instance()->showVirtualFilesOption()) {
        _ui->useVfsRadioButton->setVisible(false);
        enableVfsByDefault = false;
    }

    if (!vfsIsAvailable) {
        enableVfsByDefault = false;
    }

    auto setRecommendedOption = [](QRadioButton *radioButton) {
        radioButton->setText(tr("%1 (recommended)").arg(radioButton->text()));
        radioButton->setChecked(true);
    };

    if (enableVfsByDefault) {
        setRecommendedOption(_ui->useVfsRadioButton);

        // move up top
        _ui->syncModeGroupBoxLayout->removeWidget(_ui->useVfsRadioButton);
        _ui->syncModeGroupBoxLayout->insertWidget(1, _ui->useVfsRadioButton);
    } else {
        setRecommendedOption(_ui->syncEverythingRadioButton);
    }

    if (!vfsIsAvailable) {
        // fallback: it's set as default option in Qt Designer, but we should make sure the option is selected if VFS is not available
        _ui->syncEverythingRadioButton->setChecked(true);

        _ui->useVfsRadioButton->setToolTip(tr("The virtual filesystem feature is not available for this installation."));
    }

    connect(_ui->chooseLocalDirectoryButton, &QToolButton::clicked, this, [=]() {
        auto dialog = new QFileDialog(this, tr("Select the local folder"), _ui->localDirectoryLineEdit->text());
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly);

        connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &directory) {
            // the directory chooser should guarantee that the directory exists
            Q_ASSERT(QDir(directory).exists());

            _ui->localDirectoryLineEdit->setText(QDir::toNativeSeparators(directory));
        });
        dialog->open();
    });

    // vfsIsAvailable is false when experimental features are not enabled and the mode is experimental even if a plugin is found
    if (vfsIsAvailable && Theme::instance()->forceVirtualFilesOption()) {
        // this has no visual effect, but is needed for syncMode()
        _ui->useVfsRadioButton->setChecked(true);

        // we want to hide the entire sync mode selection from the user, not just disable it
        _ui->syncModeGroupBox->setVisible(false);
    }

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
        return SyncMode::SyncEverything;
    }
    if (_ui->configureSyncManuallyRadioButton->isChecked()) {
        return SyncMode::ConfigureUsingFolderWizard;
    }
    if (_ui->useVfsRadioButton->isChecked()) {
        return SyncMode::UseVfs;
    }

    Q_UNREACHABLE();
}

bool AccountConfiguredWizardPage::validateInput()
{
    // nothing to validate here
    return true;
}

void AccountConfiguredWizardPage::setShowAdvancedSettings(bool showAdvancedSettings)
{
    _ui->advancedConfigGroupBox->setChecked(showAdvancedSettings);
}
}

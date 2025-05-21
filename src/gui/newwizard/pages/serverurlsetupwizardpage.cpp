#include "serverurlsetupwizardpage.h"
#include "ui_serverurlsetupwizardpage.h"

#include "libsync/globalconfig.h"
#include "libsync/theme.h"


namespace OCC::Wizard {

ServerUrlSetupWizardPage::ServerUrlSetupWizardPage(const QUrl &serverUrl)
    : _ui(new ::Ui::ServerUrlSetupWizardPage)
{
    _ui->setupUi(this);

    // not the best style, but we hacked such branding into the pages elsewhere, too
    if (GlobalConfig::serverUrl().isValid()) {
        // note that the text should be set before the page is displayed, this way validateInput() will enable the next button
        _ui->urlLineEdit->setText(GlobalConfig::serverUrl().toString());

        _ui->urlLineEdit->hide();
        _ui->serverUrlLabel->hide();
    } else {
        _ui->urlLineEdit->setText(serverUrl.toString());

        connect(this, &AbstractSetupWizardPage::pageDisplayed, this, [this]() {
            _ui->urlLineEdit->setFocus();
        });
    }

    _ui->logoLabel->setText(QString());
    _ui->logoLabel->setPixmap(Theme::instance()->wizardHeaderLogo().pixmap(200, 200));
    //: This is the accessibility text for the logo in the setup wizard page. The parameter is the name for the (branded) application.
    _ui->logoLabel->setAccessibleName(tr("%1 logo").arg(Theme::instance()->appNameGUI()));

    connect(_ui->urlLineEdit, &QLineEdit::textChanged, this, &AbstractSetupWizardPage::contentChanged);
}

QString ServerUrlSetupWizardPage::userProvidedUrl() const
{
    return _ui->urlLineEdit->text().simplified();
}

ServerUrlSetupWizardPage::~ServerUrlSetupWizardPage()
{
    delete _ui;
}

bool ServerUrlSetupWizardPage::validateInput()
{
    return !_ui->urlLineEdit->text().isEmpty();
}
}

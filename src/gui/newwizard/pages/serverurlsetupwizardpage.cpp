#include "serverurlsetupwizardpage.h"
#include "ui_serverurlsetupwizardpage.h"

#include "libsync/globalconfig.h"
#include "libsync/theme.h"

#include <QValidator>

using namespace Qt::Literals::StringLiterals;

namespace {
class UrlValidator : public QValidator
{
    Q_OBJECT
public:
    using QValidator::QValidator;
    State validate(QString &input, int &) const override
    {
        if (input.isEmpty()) {
            return Intermediate;
        }
        const auto url = QUrl::fromUserInput(input);
        if (!url.isValid() || url.host().isEmpty()) {
            return Intermediate;
        }
        return Acceptable;
    }

    void fixup(QString &input) const override
    {
        auto url = QUrl::fromUserInput(input);
        if (url.scheme() == "http"_L1) {
            url.setScheme("https"_L1);
        }
        input = url.toString();
    }
};
}
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

    auto *validator = new UrlValidator(_ui->urlLineEdit);
    _ui->urlLineEdit->setValidator(validator);
    connect(_ui->urlLineEdit, &QLineEdit::textChanged, this, &AbstractSetupWizardPage::contentChanged);
}

QUrl ServerUrlSetupWizardPage::userProvidedUrl() const
{
    QString url = _ui->urlLineEdit->text();
    _ui->urlLineEdit->validator()->fixup(url);
    return QUrl(url);
}

ServerUrlSetupWizardPage::~ServerUrlSetupWizardPage()
{
    delete _ui;
}

bool ServerUrlSetupWizardPage::validateInput() const
{
    return _ui->urlLineEdit->hasAcceptableInput();
}
}

#include "serverurlsetupwizardpage.moc"

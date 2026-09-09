#include "gui/newwizard/pages/serverurlsetupwizardpage.h"
#include "gui/newwizard/ui_serverurlsetupwizardpage.h"

#include "gui/clientcertificatedialog.h"
#include "libsync/globalconfig.h"
#include "libsync/theme.h"

#include <QMessageBox>
#include <QPushButton>
#include <QValidator>

using namespace Qt::Literals::StringLiterals;

namespace {
QString fixupUrl(const QString &input)
{
    auto url = QUrl::fromUserInput(input);
    if (url.scheme() == "http"_L1) {
        url.setScheme("https"_L1);
    }
    return url.toString();
}

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

    void fixup(QString &input) const override { input = fixupUrl(input); }
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

    connect(_ui->clientCertButton, &QPushButton::clicked, this, &ServerUrlSetupWizardPage::slotConfigureClientCertificate);
    updateClientCertStatus();
}

QUrl ServerUrlSetupWizardPage::userProvidedUrl() const
{
    return QUrl::fromUserInput(fixupUrl(_ui->urlLineEdit->text()));
}

void ServerUrlSetupWizardPage::slotConfigureClientCertificate()
{
    // If a certificate is already selected, let the user replace or remove it.
    if (!_clientCertificate.isNull()) {
        QMessageBox box(QMessageBox::Question, tr("Client certificate (mTLS)"),
            tr("A client certificate is already selected:\n%1").arg(_clientCertificate.subjectDisplayName()),
            QMessageBox::NoButton, this);
        auto *replaceButton = box.addButton(tr("Replace..."), QMessageBox::AcceptRole);
        auto *removeButton = box.addButton(tr("Remove"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();

        if (box.clickedButton() == removeButton) {
            _clientCertificate.clear();
            _clientPrivateKey.clear();
            _clientCaCertificates.clear();
            updateClientCertStatus();
            return;
        }
        if (box.clickedButton() != replaceButton) {
            return;
        }
        // otherwise fall through and import a replacement
    }

    ClientCertificateUtils::Pkcs12Result result;
    if (!ClientCertificateDialog::promptImportCertificate(this, &result)) {
        return;
    }

    _clientCertificate = result.certificate;
    _clientPrivateKey = result.privateKey;
    _clientCaCertificates = result.caCertificates;
    updateClientCertStatus();
}

void ServerUrlSetupWizardPage::updateClientCertStatus()
{
    if (_clientCertificate.isNull()) {
        _ui->clientCertStatusLabel->clear();
    } else {
        _ui->clientCertStatusLabel->setText(tr("Selected: %1").arg(_clientCertificate.subjectDisplayName()));
    }
}

QSslCertificate ServerUrlSetupWizardPage::clientCertificate() const
{
    return _clientCertificate;
}

QSslKey ServerUrlSetupWizardPage::clientPrivateKey() const
{
    return _clientPrivateKey;
}

QList<QSslCertificate> ServerUrlSetupWizardPage::clientCaCertificates() const
{
    return _clientCaCertificates;
}

ServerUrlSetupWizardPage::~ServerUrlSetupWizardPage()
{
    delete _ui;
}

bool ServerUrlSetupWizardPage::validateInput() const
{
    return _ui->urlLineEdit->hasAcceptableInput();
}

void ServerUrlSetupWizardPage::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (validateInput()) {
            Q_EMIT requestNext();
        }
    }
    AbstractSetupWizardPage::keyPressEvent(event);
}
}

#include "serverurlsetupwizardpage.moc"

/*
 * Copyright (C) Fabian Müller <fmueller@owncloud.com>
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

#include "gui/newwizard/states/serverurlsetupwizardstate.h"

#include "gui/newwizard/jobs/discoverwebfingerservicejobfactory.h"
#include "gui/newwizard/jobs/resolveurljobfactory.h"
#include "libsync/theme.h"

namespace OCC::Wizard {

Q_LOGGING_CATEGORY(lcSetupWizardServerUrlState, "gui.setupwizard.states.serverurl");

ServerUrlSetupWizardState::ServerUrlSetupWizardState(SetupWizardContext *context)
    : AbstractSetupWizardState(context)
{
    _page = new ServerUrlSetupWizardPage(_context->accountBuilder().serverUrl());
}

SetupWizardState ServerUrlSetupWizardState::state() const
{
    return SetupWizardState::ServerUrlState;
}

void ServerUrlSetupWizardState::evaluatePage()
{
    // we don't want to store any unnecessary certificates for this account when the user returns to the first page
    // the easiest way is to just reset the account builder
    _context->resetAccountBuilder();

    auto serverUrlSetupWizardPage = qobject_cast<ServerUrlSetupWizardPage *>(_page);
    Q_ASSERT(serverUrlSetupWizardPage != nullptr);

    const QUrl serverUrl = [serverUrlSetupWizardPage]() {
        auto url = QUrl::fromUserInput(serverUrlSetupWizardPage->userProvidedUrl())
                       .adjusted(QUrl::RemoveUserInfo | QUrl::StripTrailingSlash | QUrl::RemoveQuery | QUrl::RemoveFragment);
        url.setScheme(QLatin1String("https"));
        return url;
    }();

    _context->accountBuilder().setServerUrl(serverUrl);

    // TODO: perform some better validation
    if (!serverUrl.isValid()) {
        Q_EMIT evaluationFailed(tr("Invalid server URL"));
        return;
    }

        // when moving back to this page (or retrying a failed credentials check), we need to make sure existing cookies
        // and certificates are deleted from the access manager
        _context->resetAccessManager();

        // first, we must resolve the actual server URL
        auto *resolveJob = Jobs::ResolveUrlJobFactory(_context->accessManager()).startJob(_context->accountBuilder().serverUrl(), this);

        connect(resolveJob, &CoreJob::finished, resolveJob, [this, resolveJob]() {
            if (!resolveJob->success()) {
                Q_EMIT evaluationFailed(resolveJob->errorMessage());
                return;
            }
            _context->accountBuilder().setServerUrl(resolveJob->result().toUrl());

            // check whether WebFinger is available
            // therefore, we run the corresponding discovery job
            auto *checkWebFingerAuthJob =
                Jobs::DiscoverWebFingerServiceJobFactory(_context->accessManager()).startJob(_context->accountBuilder().serverUrl(), this);
            connect(checkWebFingerAuthJob, &CoreJob::finished, this, [checkWebFingerAuthJob, this]() {
                // in case any kind of error occurs, we assume the WebFinger service is not available
                if (!checkWebFingerAuthJob->success()) {
                    Q_EMIT evaluationSuccessful();
                } else {
                    _context->accountBuilder().setWebFingerAuthenticationServerUrl(checkWebFingerAuthJob->result().toUrl());
                    Q_EMIT evaluationSuccessful();
                }
            });
        });

        connect(
            resolveJob, &CoreJob::caCertificateAccepted, this,
            [this](const QSslCertificate &caCertificate) {
                // future requests made through this access manager should accept the certificate
                _context->accessManager()->addCustomTrustedCaCertificates({caCertificate});

                // the account maintains a list, too, which is also saved in the config file
                _context->accountBuilder().addCustomTrustedCaCertificate(caCertificate);
            },
            Qt::DirectConnection);
}

} // OCC::Wizard

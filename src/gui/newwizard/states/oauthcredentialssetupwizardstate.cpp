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

#include "gui/newwizard/states/oauthcredentialssetupwizardstate.h"
#include "gui/newwizard/jobs/webfingeruserinfojobfactory.h"
#include "gui/newwizard/pages/oauthcredentialssetupwizardpage.h"

namespace OCC::Wizard {

OAuthCredentialsSetupWizardState::OAuthCredentialsSetupWizardState(SetupWizardContext *context, bool failed)
    : AbstractSetupWizardState(context)
{
    // the implementation moves to the next state automatically once ready, no user interaction needed
    _context->window()->disableNextButton();

    const auto authServerUrl = _context->accountBuilder().serverUrl();
    // pass a null pointer if failed to indicate an error state
    auto *oAuth = failed ? nullptr : new OAuth(authServerUrl, _context->accessManager(), {}, this);
    auto *page = new OAuthCredentialsSetupWizardPage(oAuth, authServerUrl);
    _page = page;
    connect(page, &OAuthCredentialsSetupWizardPage::requestAuthRestart, this, &OAuthCredentialsSetupWizardState::evaluationRetry);

    // bring window up top again, as the browser may have been raised in front of it
    _context->window()->raise();

    if (!failed) {
        connect(oAuth, &OAuth::result, this, [oAuth, this](OAuth::Result result, const QString &token, const QString &refreshToken) {
            _context->window()->slotStartTransition();
            switch (result) {
            case OAuth::Result::LoggedIn:
                break;
            case OAuth::Result::ErrorInsecureUrl:
                oAuth->deleteLater();
                Q_EMIT evaluationFailed(tr("Oauth2 authentication requires a secured connection."));
                return;
            case OAuth::Result::Error:
                oAuth->deleteLater();
                Q_EMIT evaluationFailed(tr("Error while trying to log in to OAuth2-enabled server."));
                return;
            }

            Q_ASSERT(result == OAuth::Result::LoggedIn);

            // This discovers which OpenCloud instance(s) the authenticated user has access to.
            // Uses the OAuth bearer token and resource="acct:me@{host}".
            // Looking for: rel="http://webfinger.opencloud/rel/server-instance"
            // Backend WebFinger docs: https://github.com/opencloud-eu/opencloud/blob/main/services/webfinger/README.md
            auto *job = Jobs::WebFingerInstanceLookupJobFactory(_context->accessManager(), token).startJob(_context->accountBuilder().serverUrl(), this);

            connect(job, &CoreJob::finished, this, [token, refreshToken, oAuth, job, this]() {
                oAuth->deleteLater();
                if (!job->success()) {
                    Q_EMIT evaluationFailed(tr("Failed to look up instances: %1").arg(job->errorMessage()));
                } else {
                    auto instanceUrls = qvariant_cast<QVector<QUrl>>(job->result());
                    if (instanceUrls.isEmpty()) {
                        _context->window()->showErrorMessage(tr("Server returned empty list of instances"));
                    } else {
                        _context->accountBuilder().setWebFingerInstances(instanceUrls);
                    }
                    _context->accountBuilder().setAuthenticationStrategy(
                        std::make_unique<OAuth2AuthenticationStrategy>(token, refreshToken, oAuth->dynamicRegistrationData(), oAuth->idToken()));
                    Q_EMIT evaluationSuccessful();
                }
            });
        });


        oAuth->startAuthentication();
    }
}

SetupWizardState OAuthCredentialsSetupWizardState::state() const
{
    return SetupWizardState::CredentialsState;
}

void OAuthCredentialsSetupWizardState::evaluatePage()
{
    // the next button is disabled anyway, since moving forward is controlled by the OAuth object signal handlers
    // therefore, this method should never ever be called
    Q_UNREACHABLE();
}

} // OCC::Wizard

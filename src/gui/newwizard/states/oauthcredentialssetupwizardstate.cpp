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

OAuthCredentialsSetupWizardState::OAuthCredentialsSetupWizardState(SetupWizardContext *context)
    : AbstractSetupWizardState(context)
{
    const auto authServerUrl = [this]() {
        auto authServerUrl = _context->accountBuilder().webFingerAuthenticationServerUrl();
        if (!authServerUrl.isEmpty()) {
            return authServerUrl;
        }
        return _context->accountBuilder().serverUrl();
    }();

    auto oAuth = new OAuth(authServerUrl, _context->accessManager(), {}, this);
    _page = new OAuthCredentialsSetupWizardPage(oAuth, authServerUrl);


    connect(oAuth, &OAuth::result, this, [oAuth, this](OAuth::Result result, const QString &token, const QString &refreshToken) {
        _context->window()->slotStartTransition();

        // bring window up top again, as the browser may have been raised in front of it
        _context->window()->raise();

        auto finish = [result, token, refreshToken, oAuth, this] {
            oAuth->deleteLater();
            switch (result) {
            case OAuth::Result::LoggedIn: {
                _context->accountBuilder().setAuthenticationStrategy(
                    std::make_unique<OAuth2AuthenticationStrategy>(token, refreshToken, oAuth->dynamicRegistrationData(), oAuth->idToken()));
                Q_EMIT evaluationSuccessful();
                break;
            }
            case OAuth::Result::Error: {
                Q_EMIT evaluationFailed(tr("Error while trying to log in to OAuth2-enabled server."));
                break;
            }
            case OAuth::Result::ErrorInsecureUrl: {
                Q_EMIT evaluationFailed(tr("Oauth2 authentication requires a secured connection."));
                break;
            }
            }
        };

        // SECOND WEBFINGER CALL (authenticated):
        // This discovers which OpenCloud instance(s) the authenticated user has access to.
        // Uses the OAuth bearer token and resource="acct:me@{host}".
        // Looking for: rel="http://webfinger.opencloud/rel/server-instance"
        // See issue #271 for why we perform WebFinger twice.
        // Backend WebFinger docs: https://github.com/opencloud-eu/opencloud/blob/main/services/webfinger/README.md
        if (!_context->accountBuilder().webFingerAuthenticationServerUrl().isEmpty()) {
            auto *job = Jobs::WebFingerInstanceLookupJobFactory(_context->accessManager(), token).startJob(_context->accountBuilder().serverUrl(), this);

            connect(job, &CoreJob::finished, this, [finish, job, this]() {
                if (!job->success()) {
                    Q_EMIT evaluationFailed(QStringLiteral("Failed to look up instances: %1").arg(job->errorMessage()));
                } else {
                    const auto instanceUrls = qvariant_cast<QVector<QUrl>>(job->result());

                    if (instanceUrls.isEmpty()) {
                        Q_EMIT evaluationFailed(QStringLiteral("Server returned empty list of instances"));
                    } else {
                        _context->accountBuilder().setWebFingerInstances(instanceUrls);
                    }
                }

                finish();
            });
        } else {
            finish();
        }
    });

    // the implementation moves to the next state automatically once ready, no user interaction needed
    _context->window()->disableNextButton();

    oAuth->startAuthentication();
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

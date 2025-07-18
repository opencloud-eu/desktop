/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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
#include "gui/connectionvalidator.h"
#include "gui/clientproxy.h"
#include "gui/fetchserversettings.h"
#include "gui/networkinformation.h"
#include "libsync/account.h"
#include "libsync/creds/abstractcredentials.h"
#include "libsync/networkjobs.h"
#include "libsync/networkjobs/checkserverjobfactory.h"
#include "libsync/networkjobs/jsonjob.h"
#include "libsync/theme.h"

#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkProxyFactory>
#include <QNetworkReply>

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;

namespace {

auto fetchSettingsTimeout()
{
    return std::min(20s, OCC::AbstractNetworkJob::httpTimeout);
}
}
namespace OCC {

Q_LOGGING_CATEGORY(lcConnectionValidator, "sync.connectionvalidator", QtInfoMsg)


ConnectionValidator::ConnectionValidator(AccountPtr account, QObject *parent)
    : QObject(parent)
    , _account(account)
{
    // TODO: 6.0 abort validator on 5min timeout
    auto timer = new QTimer(this);
    timer->setInterval(30s);
    connect(timer, &QTimer::timeout, this,
        [this] { qCInfo(lcConnectionValidator) << "ConnectionValidator" << _account->displayNameWithHost() << "still running after" << _duration; });
    timer->start();
}

void ConnectionValidator::setClearCookies(bool clearCookies)
{
    _clearCookies = clearCookies;
}

void ConnectionValidator::checkServer(ConnectionValidator::ValidationMode mode)
{
    _mode = mode;
    qCDebug(lcConnectionValidator) << "Checking server and authentication";

    // Lookup system proxy in a thread https://github.com/owncloud/client/issues/2993
    if (ClientProxy::isUsingSystemDefault()) {
        qCDebug(lcConnectionValidator) << "Trying to look up system proxy";
        ClientProxy::lookupSystemProxyAsync(_account->url(),
            this, SLOT(systemProxyLookupDone(QNetworkProxy)));
    } else {
        // We want to reset the QNAM proxy so that the global proxy settings are used (via ClientProxy settings)
        _account->accessManager()->setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
        // use a queued invocation so we're as asynchronous as with the other code path
        QMetaObject::invokeMethod(this, &ConnectionValidator::slotCheckServerAndAuth, Qt::QueuedConnection);
    }
}

void ConnectionValidator::systemProxyLookupDone(const QNetworkProxy &proxy)
{
    if (!_account) {
        qCWarning(lcConnectionValidator) << "Bailing out, Account had been deleted";
        return;
    }

    if (proxy.type() != QNetworkProxy::NoProxy) {
        qCInfo(lcConnectionValidator) << "Setting QNAM proxy to be system proxy" << ClientProxy::printQNetworkProxy(proxy);
    } else {
        qCInfo(lcConnectionValidator) << "No system proxy set by OS";
    }
    _account->accessManager()->setProxy(proxy);

    slotCheckServerAndAuth();
}

// The actual check
void ConnectionValidator::slotCheckServerAndAuth()
{
    auto checkServerFactory = CheckServerJobFactory::createFromAccount(_account, _clearCookies, this);
    auto checkServerJob = checkServerFactory.startJob(_account->url(), this);

    connect(checkServerJob->reply()->manager(), &AccessManager::sslErrors, this, [this](QNetworkReply *reply, const QList<QSslError> &errors) {
        Q_UNUSED(reply)
        Q_EMIT sslErrors(errors);
    });

    connect(checkServerJob, &CoreJob::finished, this, [checkServerJob, this]() {
        if (checkServerJob->success()) {
            const auto result = checkServerJob->result().value<CheckServerJobResult>();

            // adopt the new cookies
            _account->accessManager()->setCookieJar(checkServerJob->reply()->manager()->cookieJar());

            slotStatusFound(result.serverUrl(), result.statusObject());
        } else {
            switch (checkServerJob->reply()->error()) {
            case QNetworkReply::OperationCanceledError:
                [[fallthrough]];
            case QNetworkReply::TimeoutError:
                qCWarning(lcConnectionValidator) << checkServerJob;
                _errors.append(tr("timeout"));
                reportResult(Timeout);
                return;
            case QNetworkReply::SslHandshakeFailedError:
                reportResult(NetworkInformation::instance()->isBehindCaptivePortal() ? CaptivePortal : SslError);
                return;
            case QNetworkReply::TooManyRedirectsError:
                reportResult(MaintenanceMode);
                return;
            default:
                break;
            }

            _account->credentials()->checkCredentials(checkServerJob->reply());
            _errors.append(checkServerJob->errorMessage());
            reportResult(StatusNotFound);
        }
    });
}

void ConnectionValidator::slotStatusFound(const QUrl &url, const QJsonObject &info)
{
    // status.php was found.
    qCInfo(lcConnectionValidator) << "** Application: OpenCloud found: " << url << " with version " << info.value(QLatin1String("productversion")).toString();

    // Update server URL in case of redirection
    if (_account->url() != url) {
        if (Utility::urlEqual(_account->url(), url)) {
            qCInfo(lcConnectionValidator()) << "status.php was redirected to" << url.toString() << "updating the account url";
            _account->setUrl(url);
        } else {
            qCInfo(lcConnectionValidator()) << "status.php was redirected to" << url.toString() << "asking user to accept and abort for now";
            Q_EMIT _account->requestUrlUpdate(url);
            reportResult(StatusNotFound);
            return;
        }
    }

    // Check for maintenance mode: Servers send "true", so go through QVariant
    // to parse it correctly.
    if (info[QStringLiteral("maintenance")].toVariant().toBool()) {
        reportResult(MaintenanceMode);
        return;
    }

    AbstractCredentials *creds = _account->credentials();
    if (!creds->ready()) {
        reportResult(CredentialsNotReady);
        return;
    }
    // now check the authentication
    if (_mode != ConnectionValidator::ValidationMode::ValidateServer) {
        // the endpoint requires authentication
        auto *userJob = new JsonJob(_account, _account->url(), u"graph/v1.0/me"_s, "GET");
        userJob->setAuthenticationJob(true);
        userJob->setTimeout(fetchSettingsTimeout());
        connect(userJob, &JsonApiJob::finishedSignal, this, [userJob, this] {
            if (userJob->timedOut()) {
                reportResult(ConnectionValidator::Timeout);
            } else if (userJob->httpStatusCode() == 200) {
                reportResult(ConnectionValidator::Connected);
            } else if (userJob->httpStatusCode() == 401) {
                reportResult(ConnectionValidator::CredentialsWrong);
            } else if (userJob->httpStatusCode() == 503) {
                reportResult(ConnectionValidator::ServiceUnavailable);
            } else if (userJob->reply()->error() == QNetworkReply::SslHandshakeFailedError) {
                reportResult(NetworkInformation::instance()->isBehindCaptivePortal() ? ConnectionValidator::CaptivePortal : ConnectionValidator::SslError);
            } else {
                reportResult(ConnectionValidator::Undefined);
            }
        });
        userJob->start();
        return;
    } else {
        reportResult(Connected);
    }
}

void ConnectionValidator::reportResult(Status status)
{
    if (OC_ENSURE(!_finished)) {
        _finished = true;
        qCDebug(lcConnectionValidator) << status << _duration;
        Q_EMIT connectionResult(status, _errors);
        deleteLater();
    }
}

} // namespace OCC

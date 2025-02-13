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
#include "gui/tlserrordialog.h"
#include "libsync/account.h"
#include "libsync/creds/abstractcredentials.h"
#include "libsync/networkjobs.h"
#include "libsync/networkjobs/checkserverjobfactory.h"
#include "libsync/theme.h"

#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkProxyFactory>
#include <QNetworkReply>

using namespace std::chrono_literals;

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
        [this] { qCInfo(lcConnectionValidator) << "ConnectionValidator" << _account->displayNameWithHost() << "still running after" << _duration.duration(); });
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
            case QNetworkReply::ContentAccessDenied:
                reportResult(ClientUnsupported);
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
    qCInfo(lcConnectionValidator) << "** Application: OpenCloud found: " << url << " with version " << info.value(QLatin1String("versionstring")).toString();

    // Update server URL in case of redirection
    if (_account->url() != url) {
        qCInfo(lcConnectionValidator()) << "status.php was redirected to" << url.toString() << "asking user to accept and abort for now";
        Q_EMIT _account->requestUrlUpdate(url);
        reportResult(StatusNotFound);
        return;
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
        auto *fetchSetting = new FetchServerSettingsJob(_account, this);
        const auto unsupportedServerError = [this] {
            _errors.append({tr("The configured server for this client is too old."), tr("Please update to the latest server and restart the client.")});
        };
        connect(fetchSetting, &FetchServerSettingsJob::finishedSignal, this,
            [unsupportedServerError, this](ConnectionValidator::Status result, const QString &error) {
                if (!error.isEmpty()) {
                    _errors.append(error);
                }
                switch (result) {
                case ServerVersionMismatch:
                    unsupportedServerError();
                    reportResult(ServerVersionMismatch);
                    break;
                case Connected:
                    if (_account->serverSupportLevel() == Account::ServerSupportLevel::Unknown) {
                        unsupportedServerError();
                    }
                    [[fallthrough]];
                default:
                    reportResult(result);
                    break;
                }
            });

        fetchSetting->start();
        return;
    } else {
        reportResult(Connected);
    }
}

void ConnectionValidator::reportResult(Status status)
{
    if (OC_ENSURE(!_finished)) {
        _finished = true;
        qCDebug(lcConnectionValidator) << status << _duration.duration();
        Q_EMIT connectionResult(status, _errors);
        deleteLater();
    }
}

} // namespace OCC


/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "accountstate.h"
#include "account.h"
#include "application.h"
#include "configfile.h"
#include "fetchserversettings.h"
#include "fonticon.h"
#include "guiutility.h"

#include "libsync/creds/abstractcredentials.h"
#include "libsync/creds/httpcredentials.h"

#include "gui/folderman.h"
#include "gui/networkinformation.h"
#include "gui/settingsdialog.h"
#include "gui/tlserrordialog.h"

#include "logger.h"
#include "socketapi/socketapi.h"
#include "theme.h"

#include <QMessageBox>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

// How often we check the server for changed settings
auto fetchSettingsTimeout = 1h;

const QLatin1String userExplicitlySignedOutC()
{
    return QLatin1String("userExplicitlySignedOut");
}
} // anonymous namespace

namespace OCC {

Q_LOGGING_CATEGORY(lcAccountState, "gui.account.state", QtInfoMsg)


AccountState::AccountState(AccountPtr account)
    : QObject()
    , _account(account)
    , _queueGuard(_account->jobQueue())
    , _state(AccountState::Disconnected)
    , _connectionStatus(ConnectionValidator::Undefined)
    , _waitingForNewCredentials(false)
    , _maintenanceToConnectedDelay(1min + minutes(QRandomGenerator::global()->generate() % 4)) // 1-5min delay
{
    qRegisterMetaType<AccountState *>("AccountState*");

    connect(account.data(), &Account::invalidCredentials,
        this, &AccountState::slotInvalidCredentials);
    connect(account.data(), &Account::credentialsFetched,
        this, &AccountState::slotCredentialsFetched);
    connect(account.data(), &Account::credentialsAsked,
        this, &AccountState::slotCredentialsAsked);
    connect(account.data(), &Account::unknownConnectionState,
        this, [this] {
            checkConnectivity(true);
        });
    connect(account.data(), &Account::serverVersionChanged, this, [this] {
        if (_account->serverSupportLevel() == Account::ServerSupportLevel::Unsupported) {
            setState(ConfigurationError);
        }
    });

    connect(account.data(), &Account::capabilitiesChanged, this, [this] {
        if (_account->capabilities().checkForUpdates() && isOcApp()) {
            ocApp()->updateNotifier()->checkForUpdates(_account);
        }
    });


    connect(NetworkInformation::instance(), &NetworkInformation::reachabilityChanged, this, [this](NetworkInformation::Reachability reachability) {
        switch (reachability) {
        case NetworkInformation::Reachability::Online:
            [[fallthrough]];
        case NetworkInformation::Reachability::Site:
            [[fallthrough]];
        case NetworkInformation::Reachability::Unknown:
            // the connection might not yet be established
            QTimer::singleShot(0, this, [this] { checkConnectivity(false); });
            break;
        case NetworkInformation::Reachability::Disconnected:
            // explicitly set disconnected, this way a successful checkConnectivity call above will trigger a local discover
            if (state() != State::SignedOut) {
                setState(State::Disconnected);
            }
            [[fallthrough]];
        case NetworkInformation::Reachability::Local:
            break;
        }
    });

    connect(NetworkInformation::instance(), &NetworkInformation::isMeteredChanged, this, [this](bool isMetered) {
        if (ConfigFile().pauseSyncWhenMetered()) {
            if (state() == State::Connected && isMetered) {
                qCInfo(lcAccountState) << u"Network switched to a metered connection, setting account state to PausedDueToMetered";
                setState(State::Connecting);
            } else if (state() == State::Connecting && !isMetered) {
                qCInfo(lcAccountState) << u"Network switched to a NON-metered connection, setting account state to Connected";
                setState(State::Connected);
            }
        }
    });

    connect(NetworkInformation::instance(), &NetworkInformation::isBehindCaptivePortalChanged, this, [this](bool onoff) {
        if (onoff) {
            // Block jobs from starting: they will fail because of the captive portal.
            // Note: this includes the `Drives` jobs started periodically by the `SpacesManager`.
            _queueGuard.block();
        } else {
            // Empty the jobs queue before unblocking it. The client might have been behind a captive
            // portal for hours, so a whole bunch of jobs might have queued up. If we wouldn't
            // clear the queue, unleashing all those jobs might look like a DoS attack. Most of them
            // are also not very useful anymore (e.g. `Drives` jobs), and the important ones (PUT jobs)
            // will be rescheduled by a directory scan.
            _account->jobQueue()->clear();
            _queueGuard.unblock();
        }

        // A direct connect is not possible, because then the state parameter of `isBehindCaptivePortalChanged`
        // would become the `verifyServerState` argument to `checkConnectivity`.
        // The call is also made for when we "go behind" a captive portal. That ensures that not
        // only the status is set to `Connecting`, but also makes the UI show that syncing is paused.
        QTimer::singleShot(0, this, [this] { checkConnectivity(false); });
    });
    if (NetworkInformation::instance()->isBehindCaptivePortal()) {
        _queueGuard.block();
    }

    // as a fallback and to recover after server issues we also poll
    auto timer = new QTimer(this);
    timer->setInterval(ConnectionValidator::DefaultCallingInterval);
    connect(timer, &QTimer::timeout, this, [this] { checkConnectivity(false); });
    timer->start();

    connect(account->credentials(), &AbstractCredentials::requestLogout, this, [this] {
        setState(State::SignedOut);
    });

    if (FolderMan::instance()) {
        FolderMan::instance()->socketApi()->registerAccount(account);
    }

    connect(account.data(), &Account::appProviderErrorOccured, this, [](const QString &error) {
        QMessageBox *msgBox = new QMessageBox(QMessageBox::Information, Theme::instance()->appNameGUI(), error, {}, ocApp()->settingsDialog());
        msgBox->setAttribute(Qt::WA_DeleteOnClose);
        ocApp()->showSettings();
        msgBox->open();
    });
}

AccountState::~AccountState() { }

std::unique_ptr<AccountState> AccountState::loadFromSettings(AccountPtr account, const QSettings &settings)
{
    auto accountState = std::unique_ptr<AccountState>(new AccountState(account));
    const bool userExplicitlySignedOut = settings.value(userExplicitlySignedOutC(), false).toBool();
    if (userExplicitlySignedOut) {
        // see writeToSettings below
        accountState->setState(SignedOut);
    }
    return accountState;
}

std::unique_ptr<AccountState> AccountState::fromNewAccount(AccountPtr account)
{
    return std::unique_ptr<AccountState>(new AccountState(account));
}

void AccountState::writeToSettings(QSettings &settings) const
{
    // The SignedOut state is the only state where the client should *not* ask for credentials, nor
    // try to connect to the server. All other states should transition to Connected by either
    // (re-)trying to make a connection, or by authenticating (AskCredentials). So we save the
    // SignedOut state to indicate that the client should not try to re-connect the next time it
    // is started.
    settings.setValue(userExplicitlySignedOutC(), _state == SignedOut);
}

AccountPtr AccountState::account() const
{
    return _account;
}

AccountState::ConnectionStatus AccountState::connectionStatus() const
{
    return _connectionStatus;
}

QStringList AccountState::connectionErrors() const
{
    return _connectionErrors;
}

AccountState::State AccountState::state() const
{
    return _state;
}

void AccountState::setState(State state)
{
    const State oldState = _state;
    if (_state != state) {
        qCInfo(lcAccountState) << u"AccountState state change: " << _state << u"->" << state;
        _state = state;

        if (_state == SignedOut) {
            _connectionStatus = ConnectionValidator::Undefined;
            _connectionErrors.clear();
        } else if (oldState == SignedOut && _state == Disconnected) {
            // If we stop being voluntarily signed-out, try to connect and
            // auth right now!
            checkConnectivity();
        } else if (_state == ServiceUnavailable) {
            // Check if we are actually down for maintenance.
            // To do this we must clear the connection validator that just
            // produced the 503. It's finished anyway and will delete itself.
            _connectionValidator->deleteLater();
            _connectionValidator.clear();
            checkConnectivity();
        } else if (_state == Connected) {
            if ((NetworkInformation::instance()->isMetered() && ConfigFile().pauseSyncWhenMetered())
                || NetworkInformation::instance()->isBehindCaptivePortal()) {
                _state = Connecting;
            }
        }
    }

    // might not have changed but the underlying _connectionErrors might have
    if (_state == Connected) {
        QTimer::singleShot(0, this, [this, oldState] {
            // ensure the connection validator is done
            _queueGuard.unblock();

            // update capabilites and fetch relevant settings
            if (!_fetchServerSettingsJob && _fetchCapabilitiesElapsedTimer.duration() > fetchSettingsTimeout) {
                _fetchServerSettingsJob = new FetchServerSettingsJob(account(), this);
                connect(_fetchServerSettingsJob, &FetchServerSettingsJob::finishedSignal, this, [oldState, this] {
                    _fetchServerSettingsJob->deleteLater();
                    // clear the guard to make readyForSync return true
                    _fetchServerSettingsJob.clear();
                    _fetchCapabilitiesElapsedTimer.reset();
                    if (oldState == Connected || _state == Connected) {
                        Q_EMIT isConnectedChanged();
                    }
                });
                _fetchServerSettingsJob->start();
            }
        });
    }
    // don't anounce a state change from connected to connected
    // https://github.com/owncloud/client/commit/2c6c21d7532f0cbba4b768fde47810f6673ed931
    if (oldState != state || state != Connected) {
        Q_EMIT stateChanged(_state);
    }
}

bool AccountState::isSignedOut() const
{
    return _state == SignedOut;
}

void AccountState::signOutByUi()
{
    account()->credentials()->forgetSensitiveData();
    account()->clearCookieJar();
    setState(SignedOut);
    // persist that we are signed out
    Q_EMIT account()->wantsAccountSaved(account().data());
}

void AccountState::freshConnectionAttempt()
{
    if (isConnected())
        setState(Disconnected);
    checkConnectivity();
}

void AccountState::signIn()
{
    if (_state == SignedOut) {
        _waitingForNewCredentials = false;
        setState(Disconnected);
        // persist that we are no longer signed out
        Q_EMIT account()->wantsAccountSaved(account().data());
    }
}

bool AccountState::isConnected() const
{
    return _state == Connected;
}

void AccountState::tagLastSuccessfullETagRequest(const QDateTime &tp)
{
    _timeOfLastETagCheck = tp;
}

void AccountState::checkConnectivity(bool blockJobs)
{
    if (isSignedOut() || _waitingForNewCredentials) {
        return;
    }
    qCInfo(lcAccountState) << u"checkConnectivity blocking:" << blockJobs << account()->displayNameWithHost();
    if (_state != Connected) {
        setState(Connecting);
    }
    if (_tlsDialog) {
        qCDebug(lcAccountState) << u"Skip checkConnectivity, waiting for tls dialog";
        return;
    }


    if (_connectionValidator && blockJobs && !_queueGuard.queue()->isBlocked()) {
        // abort already running non blocking validator
        _connectionValidator->deleteLater();
        _connectionValidator.clear();
    }
    if (_connectionValidator) {
        qCWarning(lcAccountState) << u"ConnectionValidator already running, ignoring" << account()->displayNameWithHost() << u"Queue is blocked:"
                                  << _queueGuard.queue()->isBlocked();
        return;
    }

    // If we never fetched credentials, do that now - otherwise connection attempts
    // make little sense.
    if (!account()->credentials()->wasFetched()) {
        _waitingForNewCredentials = true;
        account()->credentials()->fetchFromKeychain();
    }
    if (account()->hasCapabilities()) {
        // IF the account is connected the connection check can be skipped
        // if the last successful etag check job is not so long ago.
        // TODO: https://github.com/owncloud/client/issues/10935
        const auto pta = account()->capabilities().remotePollInterval();
        const auto polltime = duration_cast<seconds>(ConfigFile().remotePollInterval(pta));
        const auto elapsed = _timeOfLastETagCheck.secsTo(QDateTime::currentDateTimeUtc());
        if (!blockJobs && isConnected() && _timeOfLastETagCheck.isValid()
            && elapsed <= polltime.count()) {
            qCDebug(lcAccountState) << account()->displayNameWithHost() << u"The last ETag check succeeded within the last " << polltime.count() << u"s ("
                                    << elapsed << u"s). No connection check needed!";
            return;
        }
    }

    if (blockJobs) {
        _queueGuard.block();
    }
    _connectionValidator = new ConnectionValidator(account());
    connect(_connectionValidator, &ConnectionValidator::connectionResult,
        this, &AccountState::slotConnectionValidatorResult);

    connect(_connectionValidator, &ConnectionValidator::sslErrors, this, [blockJobs, this](const QList<QSslError> &errors) {
        if (NetworkInformation::instance()->isBehindCaptivePortal()) {
            return;
        }
        if (!_tlsDialog) {
            // ignore errors for already accepted certificates
            auto filteredErrors = _account->accessManager()->filterSslErrors(errors);
            if (!filteredErrors.isEmpty()) {
                _tlsDialog = new TlsErrorDialog(filteredErrors, _account->url().host(), ocApp()->settingsDialog());
                _tlsDialog->setAttribute(Qt::WA_DeleteOnClose);
                QSet<QSslCertificate> certs;
                certs.reserve(filteredErrors.size());
                for (const auto &error : std::as_const(filteredErrors)) {
                    certs << error.certificate();
                }
                connect(_tlsDialog, &TlsErrorDialog::accepted, _tlsDialog, [certs, blockJobs, this]() {
                    _account->addApprovedCerts(certs);
                    _tlsDialog.clear();
                    // force a new _connectionValidator
                    if (_connectionValidator) {
                        _connectionValidator->deleteLater();
                        _connectionValidator.clear();
                    }
                    checkConnectivity(blockJobs);
                });
                connect(_tlsDialog, &TlsErrorDialog::rejected, this, [certs, this]() {
                    setState(SignedOut);
                });

                ocApp()->showSettings();
                _tlsDialog->open();
            }
        }
    });
    ConnectionValidator::ValidationMode mode = ConnectionValidator::ValidationMode::ValidateAuthAndUpdate;
    if (isConnected()) {
        // Use a small authed propfind as a minimal ping when we're
        // already connected.
        if (blockJobs) {
            _connectionValidator->setClearCookies(true);
        }
        mode = ConnectionValidator::ValidationMode::ValidateAuthAndUpdate;
    } else {
        // Check the server and then the auth.
        if (_waitingForNewCredentials) {
            mode = ConnectionValidator::ValidationMode::ValidateServer;
        } else {
            _connectionValidator->setClearCookies(true);
            mode = ConnectionValidator::ValidationMode::ValidateAuthAndUpdate;
        }
    }
    _connectionValidator->checkServer(mode);
}

void AccountState::slotConnectionValidatorResult(ConnectionValidator::Status status, const QStringList &errors)
{
    if (isSignedOut()) {
        qCWarning(lcAccountState) << u"Signed out, ignoring" << status << _account->url().toString();
        return;
    }


    if (status == ConnectionValidator::Connected && !_account->hasCapabilities()) {
        // this code should only be needed when upgrading from a < 3.0 release where capabilities where not cached
        // The last check was _waitingForNewCredentials = true so we only checked ValidateServer
        // now check again and fetch capabilities
        _connectionValidator->deleteLater();
        _connectionValidator.clear();
        checkConnectivity();
        return;
    }

    // Come online gradually from 503 or maintenance mode
    if (status == ConnectionValidator::Connected
        && (_connectionStatus == ConnectionValidator::ServiceUnavailable
               || _connectionStatus == ConnectionValidator::MaintenanceMode)) {
        if (!_timeSinceMaintenanceOver.isStarted()) {
            qCInfo(lcAccountState) << u"AccountState reconnection: delaying for" << _maintenanceToConnectedDelay.count() << u"ms";
            _timeSinceMaintenanceOver.reset();
            QTimer::singleShot(_maintenanceToConnectedDelay + 100ms, this, [this] { AccountState::checkConnectivity(false); });
            return;
        } else if (_timeSinceMaintenanceOver.duration() < _maintenanceToConnectedDelay) {
            qCInfo(lcAccountState) << u"AccountState reconnection: only" << _timeSinceMaintenanceOver;
            return;
        }
    }

    if (_connectionStatus != status) {
        qCInfo(lcAccountState) << u"AccountState connection status change: " << _connectionStatus << u"->" << status;
        _connectionStatus = status;
    }
    _connectionErrors = errors;
    switch (status) {
    case ConnectionValidator::Connected:
        setState(Connected);
        break;
    case ConnectionValidator::Undefined:
        [[fallthrough]];
    case ConnectionValidator::NotConfigured:
        setState(Disconnected);
        break;
    case ConnectionValidator::StatusNotFound:
        // This can happen either because the server does not exist
        // or because we are having network issues. The latter one is
        // much more likely, so keep trying to connect.
        setState(NetworkError);
        break;
    case ConnectionValidator::CredentialsWrong:
        [[fallthrough]];
    case ConnectionValidator::CredentialsNotReady:
        slotInvalidCredentials();
        break;
    case ConnectionValidator::SslError:
        // handled with the tlsDialog
        break;
    case ConnectionValidator::ServiceUnavailable:
        _timeSinceMaintenanceOver.stop();
        setState(ServiceUnavailable);
        break;
    case ConnectionValidator::MaintenanceMode:
        _timeSinceMaintenanceOver.stop();
        setState(MaintenanceMode);
        break;
    case ConnectionValidator::Timeout:
        setState(NetworkError);
        break;
    case ConnectionValidator::CaptivePortal:
        setState(Connecting);
        break;
    }
}

void AccountState::slotInvalidCredentials()
{
    if (!_waitingForNewCredentials) {
        qCInfo(lcAccountState) << u"Invalid credentials for" << _account->url().toString();

        _waitingForNewCredentials = true;
        if (account()->credentials()->ready()) {
            account()->credentials()->invalidateToken();
        }
        if (auto creds = qobject_cast<HttpCredentials *>(account()->credentials())) {
            qCInfo(lcAccountState) << u"refreshing oauth";
            if (creds->refreshAccessToken()) {
                return;
            }
            qCInfo(lcAccountState) << u"refreshing oauth failed";
        }
        qCInfo(lcAccountState) << u"restart oauth";
        account()->credentials()->restartOauth();
        setState(AskingCredentials);
    }
}

void AccountState::slotCredentialsFetched()
{
    // Make a connection attempt, no matter whether the credentials are
    // ready or not - we want to check whether we can get an SSL connection
    // going before bothering the user for a password.
    qCInfo(lcAccountState) << u"Fetched credentials for" << _account->url().toString() << u"attempting to connect";
    _waitingForNewCredentials = false;
    checkConnectivity();
}

void AccountState::slotCredentialsAsked()
{
    qCInfo(lcAccountState) << u"Credentials asked for" << _account->url().toString() << u"are they ready?" << _account->credentials()->ready();

    _waitingForNewCredentials = false;

    if (!_account->credentials()->ready()) {
        // User canceled the connection or did not give a password
        setState(SignedOut);
        return;
    }

    if (_connectionValidator) {
        // When new credentials become available we always want to restart the
        // connection validation, even if it's currently running.
        _connectionValidator->deleteLater();
        _connectionValidator.clear();
    }

    checkConnectivity();
}

Account *AccountState::accountForQml() const
{
    return _account.data();
}

bool AccountState::isSettingUp() const
{
    return _settingUp;
}

void AccountState::setSettingUp(bool settingUp)
{
    if (_settingUp != settingUp) {
        _settingUp = settingUp;
        Q_EMIT isSettingUpChanged();
    }
}
bool AccountState::readyForSync() const
{
    return !_fetchServerSettingsJob && isConnected();
}

} // namespace OCC

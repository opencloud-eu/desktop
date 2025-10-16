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


#pragma once

#include "gui/opencloudguilib.h"

#include "connectionvalidator.h"
#include "creds/abstractcredentials.h"
#include "jobqueue.h"

#include <QByteArray>
#include <QPointer>
#include <QtQmlIntegration/QtQmlIntegration>

#include <memory>

class QDialog;
class QMessageBox;
class QSettings;

namespace OCC {

class Account;
class TlsErrorDialog;
class FetchServerSettingsJob;

class OPENCLOUD_GUI_EXPORT AccountState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Account *account READ accountForQml CONSTANT)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(AccountState::State state READ state NOTIFY stateChanged)
    QML_ELEMENT
    QML_UNCREATABLE("Only created by AccountManager")

public:
    enum State : uint8_t {
        /// Not even attempting to connect, most likely because the
        /// user explicitly signed out or cancelled a credential dialog.
        SignedOut,

        /// Account would like to be connected but hasn't heard back yet.
        Disconnected,

        /// The account is successfully talking to the server.
        Connected,

        Connecting
    };
    Q_ENUM(State)


    ~AccountState() override;

    /** Creates an account state from settings and an Account object.
     *
     * Use from AccountManager with a prepared QSettings object only.
     */
    static std::unique_ptr<AccountState> loadFromSettings(AccountPtr account, const QSettings &settings);

    static std::unique_ptr<AccountState> fromNewAccount(AccountPtr account);

    /** Writes account state information to settings.
     *
     * It does not write the Account data.
     */
    void writeToSettings(QSettings &settings) const;

    AccountPtr account() const;

    ConnectionValidator::Status connectionStatus() const;
    QStringList connectionErrors() const;

    State state() const;

    bool isSignedOut() const;

    [[nodiscard]] bool readyForSync() const;

    /** A user-triggered sign out which disconnects, stops syncs
     * for the account and forgets the password. */
    void signOutByUi();

    /// Move from SignedOut state to Disconnected (attempting to connect)
    void signIn();

    bool isConnected() const;

    /** Mark the timestamp when the last successful ETag check happened for
     *  this account.
     *  The checkConnectivity() method uses the timestamp to save a call to
     *  the server to validate the connection if the last successful etag job
     *  was not so long ago.
     */
    void tagLastSuccessfullETagRequest(const QDateTime &tp);

    /***
     * The account is setup for the first time, this may take some time
     */
    bool isSettingUp() const;
    void setSettingUp(bool settingUp);

public Q_SLOTS:
    /// Triggers a ping to the server to update state and
    /// connection status and errors.
    /// verifyServerState indicates that we must check the server
    void checkConnectivity(bool verifyServerState = false);

private:
    /// Use the account as parent
    explicit AccountState(AccountPtr account);

    void setState(State state);

Q_SIGNALS:
    void stateChanged(State state);
    void isConnectedChanged();
    void isSettingUpChanged();

protected Q_SLOTS:
    void slotConnectionValidatorResult(ConnectionValidator::Status status, const QStringList &errors);
    void slotInvalidCredentials();
    void slotCredentialsFetched();
    void slotCredentialsAsked();

private:
    Account *accountForQml() const;
    AccountPtr _account;
    JobQueueGuard _queueGuard;
    State _state;
    ConnectionValidator::Status _connectionStatus;
    QStringList _connectionErrors;
    bool _waitingForNewCredentials;
    QDateTime _timeOfLastETagCheck;
    QPointer<ConnectionValidator> _connectionValidator;
    QPointer<TlsErrorDialog> _tlsDialog;

    bool _settingUp = false;

    Utility::ChronoElapsedTimer _fetchCapabilitiesElapsedTimer = {false};
    // guard against multiple fetches
    QPointer<FetchServerSettingsJob> _fetchServerSettingsJob;
};
}

Q_DECLARE_METATYPE(OCC::AccountState *)
Q_DECLARE_METATYPE(OCC::AccountStatePtr)

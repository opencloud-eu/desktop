/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
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

#include "fetchserversettings.h"

#include "gui/accountstate.h"
#include "gui/connectionvalidator.h"
#include "gui/networkinformation.h"

#include "libsync/account.h"
#include "libsync/networkjobs/jsonjob.h"

#include <OAIUser.h>

#include <QImageReader>

using namespace std::chrono_literals;

using namespace Qt::Literals::StringLiterals;

using namespace OCC;


Q_LOGGING_CATEGORY(lcfetchserversettings, "sync.fetchserversettings", QtInfoMsg)

namespace {
auto fetchSettingsTimeout()
{
    return std::min(20s, AbstractNetworkJob::httpTimeout);
}
}

// TODO: move to libsync?
FetchServerSettingsJob::FetchServerSettingsJob(const OCC::AccountPtr &account, QObject *parent)
    : QObject(parent)
    , _account(account)
{
}


void FetchServerSettingsJob::start()
{
    // The main flow now needs the capabilities
    auto *job = new JsonApiJob(_account, QStringLiteral("ocs/v2.php/cloud/capabilities"), {}, {}, this);
    job->setTimeout(fetchSettingsTimeout());

    connect(job, &JsonApiJob::finishedSignal, this, [job, this] {
        auto caps =
            job->data().value(QStringLiteral("ocs")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("capabilities")).toObject();
        qCInfo(lcfetchserversettings) << "Server capabilities" << caps;
        if (job->ocsSuccess()) {
            // Record that the server supports HTTP/2
            // Actual decision if we should use HTTP/2 is done in AccessManager::createRequest
            if (auto reply = job->reply()) {
                _account->setHttp2Supported(reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool());
            }
            _account->setCapabilities({_account->url(), caps.toVariantMap()});
            runAsyncUpdates();

            Q_EMIT finishedSignal();
        }
    });
    job->start();
}

void FetchServerSettingsJob::runAsyncUpdates()
{
    // those jobs are:
    // - never auth jobs
    // - might get queued
    // - have the default timeout
    // - must not be parented by this object

    // ideally we would parent them to the account, but as things are messed up by the shared pointer stuff we can't at the moment
    // so we just set them free

    // this must not be passed to the lambda
    [account = _account] {
        auto *userJob = new JsonJob(account, account->url(), u"graph/v1.0/me"_s, "GET");
        userJob->setTimeout(fetchSettingsTimeout());
        connect(userJob, &JsonApiJob::finishedSignal, account.data(), [userJob, account] {
            if (userJob->httpStatusCode() == 200) {
                OpenAPI::OAIUser me;
                me.fromJsonObject(userJob->data());
                account->setDavDisplayName(me.getDisplayName());
            }
        });
        userJob->start();

        if (account->capabilities().appProviders().enabled) {
            auto *jsonJob = new JsonJob(account, account->capabilities().appProviders().appsUrl, {}, "GET");
            connect(jsonJob, &JsonJob::finishedSignal, account.data(), [jsonJob, account] { account->setAppProvider(AppProvider{jsonJob->data()}); });
            jsonJob->start();
        }

        auto *avatarJob = new SimpleNetworkJob(account, account->url(), u"graph/v1.0/me/photo/$value"_s, "GET");
        connect(avatarJob, &SimpleNetworkJob::finishedSignal, account.data(), [avatarJob, account] {
            if (avatarJob->httpStatusCode() == 200) {
                QImageReader reader(avatarJob->reply());
                const auto image = reader.read();
                if (!image.isNull()) {
                    account->setAvatar(QPixmap::fromImage(image));
                } else {
                    qCWarning(lcfetchserversettings) << "Failed to read avatar image:" << reader.errorString();
                }
            }
        });
        avatarJob->start();
    }();
}

// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/updatenotifier.h"

#include "gui/application.h"
#include "gui/notifications/systemnotificationmanager.h"

#include "resources/fonticon.h"

#include "libsync/account.h"
#include "libsync/common/version.h"
#include "libsync/networkjobs/jsonjob.h"

#include <QDesktopServices>

using namespace Qt::Literals::StringLiterals;
using namespace OCC;

Q_LOGGING_CATEGORY(lcUpdateNotifier, "gui.updatenotifier", QtInfoMsg)

UpdateNotifier::UpdateNotifier(QObject *parent)
    : QObject(parent)
{
}

void UpdateNotifier::checkForUpdates(const AccountPtr &account)
{
    if (Version::withUpdateNotification()) {
        if (!_checkedForUpdate) {
            _checkedForUpdate = true;
            auto *job = new JsonJob(account, QUrl(u"https://update.opencloud.eu/desktop.json"_s), {}, "GET"_ba,
                SimpleNetworkJob::UrlQuery{
                    {u"version"_s, Version::versionWithBuildNumber().toString()}, {u"edition"_s, channel()}, {u"server"_s, account->url().toString()}},
                {}, this);
            connect(job, &JsonJob::finishedSignal, this, [job, this] {
                if (job->httpStatusCode() == 200) {
                    const auto data = job->data().value("channels"_L1).toObject().value(channel()).toObject();
                    if (!data.isEmpty()) {
                        const auto currentVersion = data.value("current_version"_L1).toString();
                        const auto url = QUrl(data.value("url"_L1).toString());
                        if (version() < QVersionNumber::fromString(currentVersion)) {
                            auto notification = SystemNotificationRequest{tr("Update available"),
                                tr("A new version %1 is available. You are using version %2.",
                                    "The first placeholder is the new version, the second one the current version")
                                    .arg(currentVersion, version().toString()),
                                Resources::FontIcon(u'')};

                            const QString buttonText = tr("Open Download Page");
                            notification.setButtons({buttonText});
                            auto *sysNotification = ocApp()->systemNotificationManager()->notify(std::move(notification));
                            connect(sysNotification, &SystemNotification::buttonClicked, this, [buttonText, url](const QString &button) {
                                if (button == buttonText) {
                                    QDesktopServices::openUrl(url);
                                }
                            });
                        }
                    }
                } else {
                    qCWarning(lcUpdateNotifier) << u"Update check failed with HTTP code" << job->httpStatusCode();
                }
            });
            job->start();
        }
    }
}

QString UpdateNotifier::channel() const
{
    return Version::isBeta() ? u"beta"_s : u"stable"_s;
}

QVersionNumber UpdateNotifier::version() const
{
    return Version::isBeta() ? Version::versionWithBuildNumber() : Version::version();
}

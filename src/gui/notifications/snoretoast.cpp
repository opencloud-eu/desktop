// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>


#include "snoretoast.h"

#include "gui/application.h"
#include "gui/notifications/systemnotification.h"
#include "libsync/theme.h"
#include "systemnotificationmanager.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <snoretoastactions.h>

Q_LOGGING_CATEGORY(lcSnoreToast, "gui.notifications.snoretoast", QtInfoMsg)

using namespace OCC;

namespace {

QString searchSnore()
{
    const auto exe = QStringLiteral("SnoreToast.exe");
    // look for snoretoast close to our executable, if the file does not exist use one from the path
    const QString path = QStandardPaths::findExecutable(exe, {qApp->applicationDirPath()});
    if (!path.isEmpty()) {
        return path;
    }
    return QStandardPaths::findExecutable(exe);
}

}


SnoreToast::SnoreToast(SystemNotificationManager *parent)
    : SystemNotificationBackend(parent)
    , _snoreSystemPath(searchSnore())
    , _server(new QLocalServer(this))
{
    if (!_snoreSystemPath.isEmpty()) {
        qCDebug(lcSnoreToast) << u"Located SnoreToast.exe in" << _snoreSystemPath;
        connect(_server, &QLocalServer::newConnection, _server, [this]() {
            auto *sock = _server->nextPendingConnection();
            connect(sock, &QLocalSocket::readyRead, sock, [sock, this]() {
                sock->deleteLater();
                const QByteArray rawData = sock->readAll();
                const QStringView data(reinterpret_cast<const wchar_t *>(rawData.constData()), rawData.size() / sizeof(wchar_t));
                qCDebug(lcSnoreToast) << data;

                QMap<QAnyStringView, QStringView> notificationResponseMap;
                for (const auto str : data.split(QLatin1Char(';'))) {
                    const int equalIndex = str.indexOf(QLatin1Char('='));
                    notificationResponseMap.insert(str.mid(0, equalIndex), str.mid(equalIndex + 1));
                }
                qDebug() << notificationResponseMap;
                const QStringView action = notificationResponseMap["action"];

                const auto snoreAction = SnoreToastActions::getAction(action.toString().toStdWString());

                qCInfo(lcSnoreToast) << u"Notification" << notificationResponseMap["notificationId"] << u"Closed with action:"
                                     << SnoreToastActions::getActionString(snoreAction);

                SystemNotification *notification = activeNotification(notificationResponseMap["notificationId"].toULongLong());
                SystemNotification::Result result = SystemNotification::Result::Clicked;
                if (notification) {
                    switch (snoreAction) {
                    case SnoreToastActions::Actions::Clicked:
                        result = SystemNotification::Result::Clicked;
                        break;
                    case SnoreToastActions::Actions::Hidden:
                        result = SystemNotification::Result::Hidden;
                        break;
                    case SnoreToastActions::Actions::Dismissed:
                        result = SystemNotification::Result::Dismissed;
                        break;
                    case SnoreToastActions::Actions::Timedout:
                        result = SystemNotification::Result::TimedOut;
                        break;
                    case SnoreToastActions::Actions::ButtonClicked:
                        result = SystemNotification::Result::ButtonClicked;
                        Q_EMIT notification->buttonClicked(notificationResponseMap["button"].toString());
                        break;
                    case SnoreToastActions::Actions::TextEntered:
                        Q_UNREACHABLE();
                        break;
                    case SnoreToastActions::Actions::Error:
                        qCWarning(lcSnoreToast) << u"Error:" << data;
                        break;
                    }
                    finishNotification(notification, result);
                } else {
                    qCWarning(lcSnoreToast) << u"Received notification response for unknown notification with the id:"
                                            << notificationResponseMap["notificationId"];
                    Q_EMIT systemNotificationManager() -> unknownNotificationClicked();
                }
            });
        });
        if (!_server->listen(QStringLiteral("%1.SnoreToast").arg(Theme::instance()->orgDomainName()))) {
            qCWarning(lcSnoreToast) << u"Failed to listen on the server";
        }
    } else {
        qCWarning(lcSnoreToast) << u"Failed to locate SnoreToast.exe";
    }
}

bool SnoreToast::isReady() const
{
    return !_snoreSystemPath.isEmpty();
}

void SnoreToast::notify(const SystemNotificationRequest &notificationRequest)
{
    auto proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->connect(proc, &QProcess::finished, proc, [proc, this] {
        proc->deleteLater();
        qCDebug(lcSnoreToast) << proc->readAll();
    });
    QStringList args = {
        QStringLiteral("-t"), notificationRequest.title(), //
        QStringLiteral("-m"), notificationRequest.text(), //
        QStringLiteral("-pipename"), _server->fullServerName(), //
        QStringLiteral("-id"), QString::number(notificationRequest.id()), //
        QStringLiteral("-appId"), Theme::instance()->orgDomainName(), //
        QStringLiteral("-pid"), QString::number(qApp->applicationPid()), //
        QStringLiteral("-application"), qApp->applicationFilePath(), //
        QStringLiteral("-p"), Resources::iconToFileSystemUrl(notificationRequest.icon()).toLocalFile() //
    };
    if (!notificationRequest.buttons().isEmpty()) {
        args.append({QStringLiteral("-b"), notificationRequest.buttons().join(QLatin1Char(';'))});
    }
    proc->start(_snoreSystemPath, args);
}

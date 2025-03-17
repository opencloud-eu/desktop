// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>


#include "snoretoast.h"
#include "gui/application.h"
#include "libsync/theme.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <snoretoastactions.h>

Q_LOGGING_CATEGORY(lcSnoreToast, "gui.snoretoast", QtInfoMsg)

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


SnoreToast::SnoreToast(QObject *parent)
    : QObject(parent)
    , _snoreSystemPath(searchSnore())
    , _server(new QLocalServer(this))
    , _temp(new QTemporaryDir)
{
    if (!_snoreSystemPath.isEmpty()) {
        qCDebug(lcSnoreToast) << "Located SnoreToast.exe in" << _snoreSystemPath;
        connect(_server, &QLocalServer::newConnection, _server, [this]() {
            auto *sock = _server->nextPendingConnection();
            sock->waitForReadyRead();
            const QByteArray rawData = sock->readAll();
            sock->deleteLater();

            const QString data =
                QString::fromWCharArray(reinterpret_cast<const wchar_t *>(rawData.constData()), rawData.size() / static_cast<int>(sizeof(wchar_t)));
            qCDebug(lcSnoreToast) << data;

            QMap<QByteArray, QStringView> notificationResponseMap;
            for (const auto str : QStringView(data).split(QLatin1Char(';'))) {
                const int equalIndex = str.indexOf(QLatin1Char('='));
                notificationResponseMap.insert(str.mid(0, equalIndex).toUtf8(), str.mid(equalIndex + 1));
            }
            const QStringView action = notificationResponseMap["action"];

            const auto snoreAction = SnoreToastActions::getAction(action.toString().toStdWString());

            qCInfo(lcSnoreToast) << "Notification" << notificationResponseMap["id"] << "Closed with action:" << SnoreToastActions::getActionString(snoreAction);

            switch (snoreAction) {
            case SnoreToastActions::Actions::Clicked:
                ocApp()->showSettings();
                break;
            case SnoreToastActions::Actions::Hidden:
                break;
            case SnoreToastActions::Actions::Dismissed:
                break;
            case SnoreToastActions::Actions::Timedout:
                break;
            case SnoreToastActions::Actions::ButtonClicked:
                break;
            case SnoreToastActions::Actions::TextEntered:
                break;
            case SnoreToastActions::Actions::Error:
                break;
            }
        });
        if (!_server->listen(QStringLiteral("%1.SnoreToast").arg(Theme::instance()->orgDomainName()))) {
            qCWarning(lcSnoreToast) << "Failed to listen on the server";
        }
    } else {
        qCWarning(lcSnoreToast) << "Failed to locate SnoreToast.exe";
    }
}

SnoreToast::~SnoreToast()
{
    delete _temp;
}

bool SnoreToast::isReady() const
{
    return !_snoreSystemPath.isEmpty();
}

void SnoreToast::notify(const QIcon &icon, const QString &title, const QString &msg)
{
    auto proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->connect(proc, &QProcess::finished, proc, [proc, this] {
        proc->deleteLater();
        qCDebug(lcSnoreToast) << proc->readAll();
    });
    proc->start(_snoreSystemPath,
        {
            QStringLiteral("-t"), title, //
            QStringLiteral("-m"), msg, //
            QStringLiteral("-pipename"), _server->fullServerName(), //
            QStringLiteral("-id"), QString::number(_notificationId++), //
            QStringLiteral("-appId"), Theme::instance()->orgDomainName(), //
            QStringLiteral("-pid"), QString::number(qApp->applicationPid()), //
            QStringLiteral("-application"), qApp->applicationFilePath(), //
            QStringLiteral("-p"), iconToPath(icon) //
        });
}

QString SnoreToast::iconToPath(const QIcon &icon)
{
    QFileInfo info(QStringLiteral("%1/%2.png").arg(_temp->path(), QString::number(icon.cacheKey())));
    if (!info.exists()) {
        if (!icon.pixmap(icon.actualSize({512, 512})).save(info.absoluteFilePath(), "PNG")) {
            qCWarning(lcSnoreToast) << "Failed to save icon to " << info.absoluteFilePath() << info.size();
        }
    }
    return info.absoluteFilePath();
}

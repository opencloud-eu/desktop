// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>


#pragma once
#include <QObject>

class QLocalServer;
class QTemporaryDir;

namespace OCC {
class SnoreToast : public QObject
{
    Q_OBJECT
public:
    explicit SnoreToast(QObject *parent = nullptr);

    ~SnoreToast();

    bool isReady() const;

    void notify(const QIcon &icon, const QString &title, const QString &msg);

private:
    QString iconToPath(const QIcon &icon);


    QString _snoreSystemPath;
    QLocalServer *_server;
    uint _notificationId = 0;
    QTemporaryDir *_temp;
};

}

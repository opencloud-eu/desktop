// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once
#include <QFileInfo>
#include <QIcon>
#include <QObject>
#include <QVector>


namespace OCC {
class SystemNotificationRequest
{
public:
    SystemNotificationRequest(const QString &title, const QString &text, const QIcon &icon);
    QString title() const;
    QString text() const;
    QIcon icon() const;

    quint64 id() const;

    /**
     * Add dynamic buttons to the Notification
     */
    void setButtons(const QStringList &buttons);
    const QStringList &buttons() const;

private:
    QString _title;
    QString _text;
    QIcon _icon;
    QStringList _buttons;
    const quint64 _id = 0;
};

class SystemNotification : public QObject
{
    Q_OBJECT
public:
    enum class Result {
        Clicked,
        Hidden,
        Dismissed,
        TimedOut,
        ButtonClicked,

        Error = -1
    };
    Q_ENUM(Result);

    SystemNotification(SystemNotificationRequest &&notificationRequest, QObject *parent = nullptr);

    const SystemNotificationRequest &request() const;


Q_SIGNALS:
    void finished(Result r);
    void buttonClicked(const QString &button);

private:
    const SystemNotificationRequest _notificationRequest;
};
}

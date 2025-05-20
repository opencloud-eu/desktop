// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/opencloudsynclib.h"

#include <QVariant>
#include <QtQmlMeta>

namespace OCC {
class OPENCLOUD_SYNC_EXPORT GlobalConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;

    static QVariant getValue(QAnyStringView param, const QVariant &defaultValue = {});

#ifdef Q_OS_WIN
    static QVariant getPolicySetting(QAnyStringView policy, const QVariant &defaultValue = {});
#endif
};
}

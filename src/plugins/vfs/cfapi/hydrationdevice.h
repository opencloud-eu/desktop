// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "cfapiwrapper.h"

#include <QFile>

namespace OCC {
class CfApiHydrationJob;

namespace CfApiWrapper {

    class HydrationDevice : public QIODevice
    {
        Q_OBJECT
    public:
        static CfApiHydrationJob *requestHydration(const CfApiWrapper::CallBackContext &context, qint64 totalSize, QObject *parent = nullptr);

        HydrationDevice(const CfApiWrapper::CallBackContext &context, qint64 totalSize, QObject *parent = nullptr);

        qint64 readData(char *data, qint64 maxlen) override;
        qint64 writeData(const char *data, qint64 len) override;

    private:
        CfApiWrapper::CallBackContext _context;
        // expected total size
        qint64 _totalSize;
        // current offset
        qint64 _offset = 0;

        QByteArray _buffer;
    };

}
}

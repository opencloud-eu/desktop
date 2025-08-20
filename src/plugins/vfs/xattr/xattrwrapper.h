/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QString>

#include "config.h"
#include "common/result.h"

namespace XAttrWrapper
{
struct PlaceHolderAttribs {
public:
    qint64 size() const { return _size; }
    QByteArray fileId() const { return _fileId; }
    time_t modTime() const {return _modtime; }
    QString eTag() const { return _etag; }
    QByteArray pinState() const { return _pinState; }

    bool itsMe() const { return !_executor.isEmpty() && _executor == QByteArrayLiteral(APPLICATION_EXECUTABLE);}

    qint64 _size;
    QByteArray _fileId;
    time_t _modtime;
    QString _etag;
    QByteArray _executor;
    QByteArray _pinState;

};

PlaceHolderAttribs placeHolderAttributes(const QString& path);
bool hasPlaceholderAttributes(const QString &path);

OCC::Result<void, QString> addPlaceholderAttribute(const QString &path, const QByteArray &name = {}, const QByteArray &val = {});

}

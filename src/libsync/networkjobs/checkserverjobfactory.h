/*
 * Copyright (C) Fabian Müller <fmueller@owncloud.com>
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

#include "abstractcorejob.h"

#include <QJsonObject>

namespace OCC {

class OPENCLOUD_SYNC_EXPORT CheckServerJobResult
{

public:
    CheckServerJobResult();
    CheckServerJobResult(const QJsonObject &statusObject, const QUrl &serverUrl);

    QJsonObject statusObject() const;
    QUrl serverUrl() const;

private:
    const QJsonObject _statusObject;
    const QUrl _serverUrl;
};


class OPENCLOUD_SYNC_EXPORT CheckServerJobFactory : public AbstractCoreJobFactory
{
public:
    using AbstractCoreJobFactory::AbstractCoreJobFactory;

    /**
     * clearCookies: Whether to clear the cookies before we start the CheckServerJob job
     * This option also depends on Theme::instance()->connectionValidatorClearCookies()
     */
    static CheckServerJobFactory createFromAccount(const AccountPtr &account, bool clearCookies, QObject *parent);

    CoreJob *startJob(const QUrl &url, QObject *parent) override;

private:
    int _maxRedirectsAllowed = 5;
};

} // OCC

Q_DECLARE_METATYPE(OCC::CheckServerJobResult)

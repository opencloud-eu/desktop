/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "xattrwrapper.h"
#include "common/result.h"
#include "config.h"

#include <QLoggingCategory>
#include <sys/xattr.h>



Q_LOGGING_CATEGORY(lcXAttrWrapper, "sync.vfs.xattr.wrapper", QtInfoMsg)

namespace {
constexpr auto hydrateExecAttributeName = "user.openvfs.hydrate_exec";

OCC::Optional<QByteArray> xattrGet(const QByteArray &path, const QByteArray &name)
{
    QByteArray result(512, Qt::Initialization::Uninitialized);
    auto count = getxattr(path.constData(), name.constData(), result.data(), result.size());
    if (count > 0) {
        // xattr is special. It does not store C-Strings, but blobs.
        // So it needs to be checked, if a trailing \0 was added when writing
        // (as this software does) or not as the standard setfattr-tool
        // the following will handle both cases correctly.
        if (result[count-1] == '\0') {
            count--;
        }
        result.truncate(count);
        return result;
    } else {
        return {};
    }
}

bool xattrSet(const QByteArray &path, const QByteArray &name, const QByteArray &value)
{
    const auto returnCode = setxattr(path.constData(), name.constData(), value.constData(), value.size()+1, 0);
    return returnCode == 0;
}

}

namespace XAttrWrapper {

PlaceHolderAttribs placeHolderAttributes(const QString& path)
{
    PlaceHolderAttribs attribs;

    // lambda to handle the Optional return val of xattrGet
    auto xattr = [](const QByteArray& p, const QByteArray& name) {
        const auto value = xattrGet(p, name);
        if (value) {
            return *value;
        } else {
            return QByteArray();
        }
    };

    const auto p = path.toUtf8();

    attribs._executor = xattr(p, hydrateExecAttributeName);
    attribs._etag = QString::fromUtf8(xattr(p, "user.openvfs.etag"));
    attribs._fileId = xattr(p, "user.openvfs.fileid");

    const QByteArray& tt = xattr(p, "user.openvfs.modtime");
    attribs._modtime = tt.toLongLong();

    attribs._action = xattr(p, "user.openvfs.action");
    attribs._size = xattr(p, "user.openvfs.fsize").toLongLong();
    attribs._pinState = xattr(p, "user.openvfs.pinstate");

    return attribs;
}


bool hasPlaceholderAttributes(const QString &path)
{
    const PlaceHolderAttribs attribs = placeHolderAttributes(path);

    // Only pretend to have attribs if they are from us...
    return attribs.itsMe();
}

OCC::Result<void, QString> addPlaceholderAttribute(const QString &path, const QByteArray& name, const QByteArray& value)
{
    auto success = xattrSet(path.toUtf8(), hydrateExecAttributeName, APPLICATION_EXECUTABLE);
    if (!success) {
        return QStringLiteral("Failed to set the extended attribute hydrateExec");
    }

    if (!name.isEmpty()) {
        success = xattrSet(path.toUtf8(), name, value);
        if (!success) {
            return QStringLiteral("Failed to set the extended attribute");
        }
    }

    return {};
}
}

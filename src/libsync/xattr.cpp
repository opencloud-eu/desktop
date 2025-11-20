// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "xattr.h"

#include <sys/xattr.h>

#include "common/filesystembase.h"

namespace OCC {
namespace FileSystem {
    std::optional<QString> Xattr::getxattr(const std::filesystem::path &path, const QString &name)
    {
        QByteArray value;
        ssize_t res = 0;
        do {
            value.resize(value.size() + 255);
#ifdef Q_OS_MAC
            res = ::getxattr(path.c_str(), name.toUtf8().constData(), value.data(), value.size(), 0, XATTR_NOFOLLOW);
#else
            res = ::lgetxattr(path.c_str(), name.toUtf8().constData(), value.data(), value.size());
#endif
        } while (res == -1 && errno == ERANGE);
        if (res > 0) {
            value.resize(res);
            return QString::fromUtf8(value);
        } else {
            return {};
        }
    }

    Result<void, QString> Xattr::setxattr(const std::filesystem::path &path, const QString &name, const QString &value)
    {
        const auto data = value.toUtf8();
#ifdef Q_OS_MAC
        const auto result = ::setxattr(path.c_str(), name.toUtf8().constData(), data.constData(), data.size(), 0, XATTR_NOFOLLOW);
#else
        const auto result = ::lsetxattr(path.c_str(), name.toUtf8().constData(), data.constData(), data.size(), 0);
#endif
        if (result != 0) {
            return QString::fromUtf8(strerror(errno));
        }
        return {};
    }

    Result<void, QString> Xattr::removexattr(const std::filesystem::path &path, const QString &name)
    {
#ifdef Q_OS_MAC
        const auto result = ::removexattr(path.c_str(), name.toUtf8().constData(), 0);
#else
        const auto result = ::lremovexattr(path.c_str(), name.toUtf8().constData());
#endif

#ifdef Q_OS_MAC
        if (errno == ENOATTR) {
#else
        if (errno == ENODATA) {
#endif
            qCWarning(lcFileSystem) << u"Failed to remove tag" << name << u"from" << path.native() << u"tag doesn't exist";
            return {};
        }
        if (result != 0) {
            return QString::fromUtf8(strerror(errno));
        }
        return {};
    }
}
}

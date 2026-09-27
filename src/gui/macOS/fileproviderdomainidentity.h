#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <optional>

namespace OCC::Mac {

// Keep the original account UUID for the personal space so existing Finder
// locations and downloaded files survive upgrading to multiple-space support.
inline QString fileProviderDomainIdentifier(const QUuid &account, const QString &spaceId, bool personal)
{
    const auto prefix = account.toString(QUuid::WithoutBraces);
    return personal
        ? prefix
        // NSFileProviderDomain forbids both '/' and ':' in identifiers.
        : prefix + QStringLiteral(".space.") + QString::fromLatin1(spaceId.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

struct FileProviderDomainIdentity
{
    QUuid account;
    QString spaceId;
};

inline std::optional<FileProviderDomainIdentity> fileProviderDomainIdentity(const QString &identifier)
{
    const auto accountId = identifier.left(36);
    const QUuid account(accountId);
    if (account.isNull() || accountId != account.toString(QUuid::WithoutBraces)) {
        return std::nullopt;
    }
    if (identifier.size() == 36) {
        return FileProviderDomainIdentity{account, {}};
    }
    const auto suffix = identifier.mid(36);
    // Recognize the rejected prerelease format for account association and
    // cleanup, while always generating the native-compatible format above.
    if ((!suffix.startsWith(QLatin1String(".space.")) && !suffix.startsWith(QLatin1String(":space:"))) || suffix.size() == 7) {
        return std::nullopt;
    }
    const auto encoded = suffix.mid(7);
    const auto decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || QString::fromUtf8(decoded.decoded).toUtf8() != decoded.decoded) {
        return std::nullopt;
    }
    const auto spaceId = QString::fromUtf8(decoded.decoded);
    if (QString::fromLatin1(decoded.decoded.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) != encoded) {
        return std::nullopt;
    }
    return FileProviderDomainIdentity{account, spaceId};
}

}

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <cmath>
#include <optional>

namespace OCC::Mac {

// A status snapshot is evidence from the provider, not an authentication result.
struct FileProviderSyncStatus
{
    QJsonObject values;
    QDateTime receivedAt;

    static std::optional<FileProviderSyncStatus> parse(const QString &domain, const QJsonObject &object, const QDateTime &now)
    {
        if (object.value(QStringLiteral("schemaVersion")).toInt() != 1 || object.value(QStringLiteral("domainIdentifier")).toString() != domain) {
            return std::nullopt;
        }
        for (const auto &key : {"activeUploads", "activeDownloads", "activeMetadata", "pendingItems", "errorCount", "uploadedBytes", "uploadTotalBytes",
                 "downloadedBytes", "downloadTotalBytes", "lastCheckedAt", "lastSyncedAt", "sampledAt"}) {
            const auto value = object.value(QLatin1String(key));
            const auto number = value.toDouble(-1);
            if (!value.isDouble() || !std::isfinite(number) || number < 0 || number > 9007199254740991.0) {
                return std::nullopt;
            }
        }
        for (const auto &key : {"isAuthenticated", "isSynced", "pendingKnown", "pendingTruncated"}) {
            if (!object.value(QLatin1String(key)).isBool()) {
                return std::nullopt;
            }
        }
        for (const auto &key : {"activeUploads", "activeDownloads", "activeMetadata", "pendingItems", "errorCount", "uploadedBytes", "uploadTotalBytes",
                 "downloadedBytes", "downloadTotalBytes"}) {
            const auto value = object.value(QLatin1String(key)).toDouble();
            if (std::floor(value) != value) {
                return std::nullopt;
            }
        }
        for (const auto &key : {"lastCheckedAt", "lastSyncedAt"}) {
            if (object.value(QLatin1String(key)).toDouble() > now.toSecsSinceEpoch() + 60) {
                return std::nullopt;
            }
        }
        const auto sampledAt = object.value(QStringLiteral("sampledAt")).toDouble();
        if (sampledAt < now.toSecsSinceEpoch() - 10 || sampledAt > now.toSecsSinceEpoch() + 60) {
            return std::nullopt;
        }
        return FileProviderSyncStatus{object, now};
    }

    qint64 count(const char *key) const { return qint64(values.value(QLatin1String(key)).toDouble()); }
    bool fresh(const QDateTime &now) const { return receivedAt.isValid() && receivedAt.msecsTo(now) >= 0 && receivedAt.msecsTo(now) <= 10000; }
    bool active() const { return count("activeUploads") || count("activeDownloads") || count("activeMetadata") || count("pendingItems"); }
    bool idle(const QDateTime &now) const
    {
        return fresh(now) && values.value(QStringLiteral("isAuthenticated")).toBool() && values.value(QStringLiteral("pendingKnown")).toBool()
            && values.value(QStringLiteral("isSynced")).toBool() && !values.value(QStringLiteral("pendingTruncated")).toBool() && !active()
            && !count("errorCount") && count("lastCheckedAt") > 0 && count("lastSyncedAt") > 0;
    }
};

}

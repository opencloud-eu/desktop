// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/discoveryremoteinfo.h"

#include "libsync/common/checksums.h"

using namespace OCC;

class OCC::RemoteInfoData : public QSharedData
{
public:
    RemoteInfoData() = default;
    RemoteInfoData(const QString &fileName, const QMap<QString, QString> &map)
    {
        _name = fileName.mid(fileName.lastIndexOf(QLatin1Char('/')) + 1);
        _directDownloadUrl = map.value(QStringLiteral("downloadURL"));
        _directDownloadCookies = map.value(QStringLiteral("dDC"));

        if (auto it = Utility::optionalFind(map, QStringLiteral("resourcetype"))) {
            _isDirectory = it->value().contains(QStringLiteral("collection"));
        }
        if (auto it = Utility::optionalFind(map, QStringLiteral("getlastmodified"))) {
            const auto date = Utility::parseRFC1123Date(**it);
            Q_ASSERT(date.isValid());
            _modtime = date.toSecsSinceEpoch();
        }
        if (_isDirectory) {
            _size = 0;
        } else {
            if (auto it = Utility::optionalFind(map, QStringLiteral("getcontentlength"))) {
                // See #4573, sometimes negative size values are returned
                _size = std::max<int64_t>(0, it->value().toLongLong());
            }
        }
        if (auto it = Utility::optionalFind(map, QStringLiteral("getetag"))) {
            _etag = Utility::normalizeEtag(it->value());
        }
        if (auto it = Utility::optionalFind(map, QStringLiteral("id"))) {
            _fileId = it->value().toUtf8();
        }
        if (auto it = Utility::optionalFind(map, QStringLiteral("checksums"))) {
            _checksumHeader = findBestChecksum(it->value().toUtf8());
        }
        if (auto it = Utility::optionalFind(map, QStringLiteral("permissions"))) {
            _remotePerm = RemotePermissions::fromServerString(it->value());
        }
        if (auto it = Utility::optionalFind(map, QStringLiteral("share-types"))) {
            const QString &value = it->value();
            if (!value.isEmpty()) {
                if (!map.contains(QStringLiteral("permissions"))) {
                    qWarning() << "Server returned a share type, but no permissions?";
                    // Empty permissions will cause a sync failure
                } else {
                    // S means shared with me.
                    // But for our purpose, we want to know if the file is shared. It does not matter
                    // if we are the owner or not.
                    // Piggy back on the persmission field
                    _remotePerm.setPermission(RemotePermissions::IsShared);
                }
            }
        }
    }

    QString _name;
    QString _etag;
    QByteArray _fileId;
    QByteArray _checksumHeader;
    RemotePermissions _remotePerm;
    time_t _modtime = 0;
    int64_t _size = -1;
    bool _isDirectory = false;

    QString _directDownloadUrl;
    QString _directDownloadCookies;
};

RemoteInfo::RemoteInfo()
    : d([] {
        static QExplicitlySharedDataPointer<RemoteInfoData> nullData{new RemoteInfoData{}};
        return nullData;
    }())
{
}

RemoteInfo::RemoteInfo(const QString &fileName, const QMap<QString, QString> &map)
    : d(new RemoteInfoData(fileName, map))
{
}

RemoteInfo::~RemoteInfo() = default;

RemoteInfo::RemoteInfo(const RemoteInfo &other) = default;

RemoteInfo &RemoteInfo::operator=(const RemoteInfo &other) = default;

QString OCC::RemoteInfo::name() const
{
    return d->_name;
}

bool OCC::RemoteInfo::isDirectory() const
{
    return d->_isDirectory;
}

QString OCC::RemoteInfo::etag() const
{
    return d->_etag;
}

QByteArray OCC::RemoteInfo::fileId() const
{
    return d->_fileId;
}

QByteArray OCC::RemoteInfo::checksumHeader() const
{
    return d->_checksumHeader;
}

RemotePermissions OCC::RemoteInfo::remotePerm() const
{
    return d->_remotePerm;
}

time_t OCC::RemoteInfo::modtime() const
{
    return d->_modtime;
}

int64_t OCC::RemoteInfo::size() const
{
    return d->_size;
}

QString OCC::RemoteInfo::directDownloadUrl() const
{
    return d->_directDownloadUrl;
}

QString OCC::RemoteInfo::directDownloadCookies() const
{
    return d->_directDownloadCookies;
}

bool OCC::RemoteInfo::isValid() const
{
    return !d->_name.isNull();
}

// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/discoveryremoteinfo.h"

#include "libsync/common/checksums.h"

#include <QApplication>

using namespace OCC;
using namespace Qt::Literals::StringLiterals;

class OCC::RemoteInfoData : public QSharedData
{
public:
    RemoteInfoData() = default;
    RemoteInfoData(const QString &fileName, const QMap<QString, QString> &map)
    {
        QStringList errors;
        _name = fileName.mid(fileName.lastIndexOf('/'_L1) + 1);

        if (auto it = Utility::optionalFind(map, "resourcetype"_L1)) {
            _isDirectory = it->value().contains(QStringLiteral("collection"));
        }
        if (auto it = Utility::optionalFind(map, "getlastmodified"_L1)) {
            const auto date = Utility::parseRFC1123Date(**it);
            Q_ASSERT(date.isValid());
            _modtime = date.toSecsSinceEpoch();
        }
        if (_isDirectory) {
            _size = 0;
        } else {
            if (auto it = Utility::optionalFind(map, "getcontentlength"_L1)) {
                // See #4573, sometimes negative size values are returned
                _size = std::max<int64_t>(0, it->value().toLongLong());
            } else {
                errors.append(u"size"_s);
            }
        }
        if (auto it = Utility::optionalFind(map, "getetag"_L1)) {
            _etag = Utility::normalizeEtag(it->value());
        }
        if (auto it = Utility::optionalFind(map, "id"_L1)) {
            _fileId = it->value().toUtf8();
        }
        if (auto it = Utility::optionalFind(map, "checksums"_L1)) {
            _checksumHeader = findBestChecksum(it->value().toUtf8());
        }
        if (auto it = Utility::optionalFind(map, "permissions"_L1)) {
            _remotePerm = RemotePermissions::fromServerString(it->value());
        }


        if (_etag.isEmpty()) {
            errors.append(u"etag"_s);
        }
        if (_fileId.isEmpty()) {
            errors.append(u"id"_s);
        }
        if (_checksumHeader.isEmpty()) {
            errors.append(u"checksum"_s);
        }
        if (_remotePerm.isNull()) {
            errors.append(u"permissions"_s);
        }
        if (!errors.empty()) {
            _error = QApplication::translate("RemoteInfo", "server reported no %1").arg(errors.join(u", "_s));
        }
    }

    QString _name;
    QString _etag;
    QByteArray _fileId;
    QByteArray _checksumHeader;
    RemotePermissions _remotePerm;
    time_t _modtime = 0;
    int64_t _size = 0;
    bool _isDirectory = false;

    QString _error;
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

QString RemoteInfo::name() const
{
    return d->_name;
}

bool RemoteInfo::isDirectory() const
{
    return d->_isDirectory;
}

QString RemoteInfo::etag() const
{
    return d->_etag;
}

QByteArray RemoteInfo::fileId() const
{
    return d->_fileId;
}

QByteArray RemoteInfo::checksumHeader() const
{
    return d->_checksumHeader;
}

RemotePermissions RemoteInfo::remotePerm() const
{
    return d->_remotePerm;
}

time_t RemoteInfo::modtime() const
{
    return d->_modtime;
}

int64_t RemoteInfo::size() const
{
    return d->_size;
}

QString RemoteInfo::error() const
{
    return d->_error;
}

bool RemoteInfo::isValid() const
{
    return !d->_name.isNull();
}

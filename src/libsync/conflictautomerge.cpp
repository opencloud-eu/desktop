/*
 * Copyright (C) by OpenCloud GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "libsync/conflictautomerge.h"

#include "libsync/account.h"
#include "libsync/configfile.h"
#include "libsync/filesystem.h"

#include "common/utility.h"

#include <QDateTime>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QNetworkRequest>
#include <QXmlStreamReader>

#include <git2.h>

namespace OCC {

Q_LOGGING_CATEGORY(lcConflictAutoMerge, "sync.conflictautomerge", QtInfoMsg)

namespace {
constexpr qint64 MaxAutoMergeFileSize = 1024 * 1024;

QString versionNameFromHref(const QString &href)
{
    const auto path = href.endsWith(QLatin1Char('/')) ? href.left(href.size() - 1) : href;
    const auto slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QByteArray readAll(const QString &fileName, QIODevice::OpenMode mode = {})
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | mode)) {
        return {};
    }
    return file.readAll();
}

bool writeAll(const QString &fileName, const QByteArray &data, QIODevice::OpenMode mode = {})
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | mode)) {
        return false;
    }
    return file.write(data) == data.size();
}

bool containsNulByte(const QByteArray &data)
{
    return data.contains('\0');
}

bool mergeFiles(const QString &baseFileName, const QString &localFileName, const QString &remoteFileName, const QString &mergedFileName)
{
    const auto base = readAll(baseFileName, QIODevice::Text);
    const auto local = readAll(localFileName, QIODevice::Text);
    const auto remote = readAll(remoteFileName, QIODevice::Text);
    if (base.isEmpty() && QFileInfo(baseFileName).size() > 0) {
        return false;
    }
    if (local.isEmpty() && QFileInfo(localFileName).size() > 0) {
        return false;
    }
    if (remote.isEmpty() && QFileInfo(remoteFileName).size() > 0) {
        return false;
    }

    git_libgit2_init();

    git_merge_file_input ancestor = GIT_MERGE_FILE_INPUT_INIT;
    ancestor.ptr = base.constData();
    ancestor.size = static_cast<size_t>(base.size());
    ancestor.path = "base";

    git_merge_file_input ours = GIT_MERGE_FILE_INPUT_INIT;
    ours.ptr = local.constData();
    ours.size = static_cast<size_t>(local.size());
    ours.path = "local";

    git_merge_file_input theirs = GIT_MERGE_FILE_INPUT_INIT;
    theirs.ptr = remote.constData();
    theirs.size = static_cast<size_t>(remote.size());
    theirs.path = "remote";

    git_merge_file_options options = GIT_MERGE_FILE_OPTIONS_INIT;
    options.favor = GIT_MERGE_FILE_FAVOR_NORMAL;
    options.flags = GIT_MERGE_FILE_STYLE_MERGE;

    git_merge_file_result result = {};
    const int rc = git_merge_file(&result, &ancestor, &ours, &theirs, &options);
    const bool hasResult = result.ptr || result.len == 0;
    const bool merged = rc == 0 && result.automergeable && hasResult
        && writeAll(mergedFileName, QByteArray(result.ptr, static_cast<qsizetype>(result.len)), QIODevice::Text);
    if (!merged) {
        qCInfo(lcConflictAutoMerge) << "libgit2 merge failed"
                                    << "rc" << rc
                                    << "automergeable" << result.automergeable
                                    << "hasResult" << hasResult;
    }
    git_merge_file_result_free(&result);
    git_libgit2_shutdown();
    return merged;
}

SyncJournalFileRecord baseRecordFor(OwncloudPropagator *propagator, const SyncFileItemPtr &item)
{
    auto baseRecord = propagator->_journal->getFileRecord(item->_originalFile);
    if (!baseRecord.isValid() && item->_originalFile != item->localName()) {
        baseRecord = propagator->_journal->getFileRecord(item->localName());
    }
    return baseRecord;
}
}

ConflictAutoMerge::ConflictAutoMerge(OwncloudPropagator *propagator, const SyncFileItemPtr &item, const QString &localFile, const QString &remoteFile,
    QObject *parent)
    : QObject(parent)
    , _propagator(propagator)
    , _item(item)
    , _localFile(localFile)
    , _remoteFile(remoteFile)
    , _baseRecord(baseRecordFor(propagator, item))
{
}

bool ConflictAutoMerge::canStart() const
{
    const auto enabled = ConfigFile().autoMergeTextConflicts();
    const auto conflict = _item->instruction() == CSYNC_INSTRUCTION_CONFLICT;
    const auto file = !_item->isDirectory();
    const auto versioning = _propagator->account()->capabilities().versioningEnabled();
    const auto baseValid = _baseRecord.isValid();
    const auto hasBaseFileId = !_baseRecord.fileId().isEmpty();
    const auto hasBaseEtag = !_baseRecord.etag().isEmpty();
    const auto localText = isTextMergeCandidate(_localFile);
    const auto remoteText = isTextMergeCandidate(_remoteFile);

    const auto canStart = enabled && conflict && file && versioning && baseValid && hasBaseFileId && hasBaseEtag && localText && remoteText;
    if (conflict && !canStart) {
        qCInfo(lcConflictAutoMerge) << "Text conflict automerge not started"
                                    << _item->localName()
                                    << "enabled" << enabled
                                    << "file" << file
                                    << "versioning" << versioning
                                    << "baseValid" << baseValid
                                    << "hasBaseFileId" << hasBaseFileId
                                    << "hasBaseEtag" << hasBaseEtag
                                    << "localText" << localText
                                    << "remoteText" << remoteText
                                    << "originalFile" << _item->_originalFile
                                    << "localFile" << _localFile
                                    << "remoteFile" << _remoteFile
                                    << "basePath" << _baseRecord.path();
    }
    return canStart;
}

void ConflictAutoMerge::start()
{
    if (!canStart()) {
        emitNotMerged();
        return;
    }

    QNetworkRequest request;
    request.setRawHeader("Depth", "1");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=utf-8"));

    QByteArray body = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/><d:getlastmodified/><d:getcontentlength/></d:prop></d:propfind>\n");
    _versionsJob = new SimpleNetworkJob(_propagator->account(), _propagator->account()->url(),
        QStringLiteral("dav/meta/%1/v/").arg(QString::fromUtf8(_baseRecord.fileId())), QByteArrayLiteral("PROPFIND"), std::move(body), request, this);

    connect(_versionsJob, &SimpleNetworkJob::finishedSignal, this, [this] {
        if (!_versionsJob || _versionsJob->reply()->error() != QNetworkReply::NoError || _versionsJob->httpStatusCode() / 100 != 2) {
            qCInfo(lcConflictAutoMerge) << "Could not list base versions" << _item->localName() << _versionsJob->errorString();
            emitNotMerged();
            return;
        }

        const auto xml = _versionsJob->reply()->readAll();
        QXmlStreamReader reader(xml);
        VersionInfo version;
        bool inResponse = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QLatin1String("response")) {
                version = {};
                inResponse = true;
            } else if (reader.isEndElement() && reader.name() == QLatin1String("response")) {
                if (!version.name.isEmpty()) {
                    _versions.append(version);
                }
                inResponse = false;
            } else if (inResponse && reader.isStartElement() && reader.name() == QLatin1String("href")) {
                const auto href = reader.readElementText();
                if (href.contains(QLatin1String("/v/"))) {
                    version.name = versionNameFromHref(href);
                }
            } else if (inResponse && reader.isStartElement() && reader.name() == QLatin1String("getetag")) {
                version.etag = Utility::normalizeEtag(reader.readElementText());
            } else if (inResponse && reader.isStartElement() && reader.name() == QLatin1String("getlastmodified")) {
                const auto lastModified = reader.readElementText();
                if (!lastModified.isEmpty()) {
                    version.mtime = Utility::qDateTimeToTime_t(Utility::parseRFC1123Date(lastModified));
                }
            }
        }
        if (reader.hasError()) {
            qCInfo(lcConflictAutoMerge) << "Could not parse base versions" << _item->localName() << reader.errorString();
            emitNotMerged();
            return;
        }
        versionsListed();
    });
    _versionsJob->start();
}

bool ConflictAutoMerge::isTextMergeCandidate(const QString &fileName) const
{
    const QFileInfo info(fileName);
    if (!info.isFile() || info.size() > MaxAutoMergeFileSize) {
        return false;
    }
    if (info.size() == 0) {
        return true;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const auto data = file.read(MaxAutoMergeFileSize + 1);
    if (data.size() > MaxAutoMergeFileSize || containsNulByte(data)) {
        return false;
    }

    QMimeDatabase mimeDb;
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    const auto mimeType = mimeDb.mimeTypeForFileNameAndData(fileName, &buffer);
    return mimeType.inherits(QStringLiteral("text/plain"))
        || mimeType.name().startsWith(QStringLiteral("text/"))
        || mimeType.name() == QLatin1String("application/json")
        || mimeType.name() == QLatin1String("application/xml")
        || mimeType.name() == QLatin1String("application/x-yaml")
        || mimeType.name() == QLatin1String("application/javascript");
}

void ConflictAutoMerge::versionsListed()
{
    const auto versionName = matchingVersionName();
    if (versionName.isEmpty()) {
        qCInfo(lcConflictAutoMerge) << u"No matching base version for" << _item->localName();
        emitNotMerged();
        return;
    }

    _baseJob = new SimpleNetworkJob(_propagator->account(), _propagator->account()->url(),
        QStringLiteral("dav/meta/%1/v/%2").arg(QString::fromUtf8(_baseRecord.fileId()), versionName), QByteArrayLiteral("GET"), this);
    connect(_baseJob, &SimpleNetworkJob::finishedSignal, this, &ConflictAutoMerge::baseDownloaded);
    _baseJob->start();
}

void ConflictAutoMerge::baseDownloaded()
{
    if (!_baseJob || _baseJob->reply()->error() != QNetworkReply::NoError || _baseJob->httpStatusCode() / 100 != 2) {
        emitNotMerged();
        return;
    }

    const auto baseData = _baseJob->reply()->readAll();
    if (baseData.size() > MaxAutoMergeFileSize || containsNulByte(baseData) || !_baseFile.open()) {
        qCInfo(lcConflictAutoMerge) << "Downloaded base version is not mergeable" << _item->localName();
        emitNotMerged();
        return;
    }
    if (_baseFile.write(baseData) != baseData.size()) {
        qCInfo(lcConflictAutoMerge) << "Could not write base version for automerge" << _item->localName();
        emitNotMerged();
        return;
    }
    _baseFile.close();

    runMerge();
}

void ConflictAutoMerge::emitNotMerged()
{
    Q_EMIT finished(false, {});
}

void ConflictAutoMerge::runMerge()
{
    QString mergedFileName;
    {
        QTemporaryFile mergedFile(QDir::tempPath() + QStringLiteral("/opencloud-automerge-XXXXXX"));
        mergedFile.setAutoRemove(false);
        if (!mergedFile.open()) {
            emitNotMerged();
            return;
        }
        mergedFileName = mergedFile.fileName();
        mergedFile.close();
    }

    if (!mergeFiles(_baseFile.fileName(), _localFile, _remoteFile, mergedFileName)) {
        qCInfo(lcConflictAutoMerge) << "Could not automatically merge text conflict" << _item->localName();
        QFile::remove(mergedFileName);
        emitNotMerged();
        return;
    }

    qCInfo(lcConflictAutoMerge) << u"Automatically merged text conflict" << _item->localName();
    Q_EMIT finished(true, mergedFileName);
}

QString ConflictAutoMerge::matchingVersionName() const
{
    const auto baseEtag = Utility::normalizeEtag(_baseRecord.etag());
    for (const auto &version : _versions) {
        if (!version.etag.isEmpty() && version.etag == baseEtag) {
            return version.name;
        }
    }

    if (_baseRecord.modtime() <= 0) {
        return {};
    }
    for (const auto &version : _versions) {
        if (version.mtime == _baseRecord.modtime()) {
            return version.name;
        }
    }

    return {};
}

}

/*
 * Copyright (C) by Dominik Schmidt <dschmidt@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "vfs.h"

#include "libsync/common/filesystembase.h"
#include "libsync/common/plugin.h"
#include "libsync/common/syncjournaldb.h"
#include "libsync/common/version.h"
#include "libsync/filesystem.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPluginLoader>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace OCC;
using namespace Qt::Literals::StringLiterals;

Q_LOGGING_CATEGORY(lcVfs, "sync.vfs", QtInfoMsg)


Vfs::Vfs(QObject *parent)
    : QObject(parent)
{
}

Vfs::~Vfs() = default;

Optional<Vfs::Mode> Vfs::modeFromString(const QString &str)
{
    // Note: Strings are used for config and must be stable
    // keep in sync with: QString Utility::enumToString(Vfs::Mode mode)
    if (str == QLatin1String("off")) {
        return Mode::Off;
    } else if (str == QLatin1String("cfapi")) {
        return Mode::WindowsCfApi;
    } else if (str == QLatin1String("xattr")) {
        return Mode::XAttr;
    }
    return {};
}

template <>
QString Utility::enumToString(Vfs::Mode mode)
{
    // Note: Strings are used for config and must be stable
    // keep in sync with: Optional<Vfs::Mode> Vfs::modeFromString(const QString &str)
    switch (mode) {
    case Vfs::Mode::WindowsCfApi:
        return QStringLiteral("cfapi");
    case Vfs::Mode::Off:
        return QStringLiteral("off");
    case Vfs::Mode::XAttr:
        return QStringLiteral("xattr");
    }
    Q_UNREACHABLE();
}

void Vfs::start(const VfsSetupParams &params)
{
    _setupParams = std::make_unique<VfsSetupParams>(params);
    startImpl(this->params());
}


void Vfs::wipeDehydratedVirtualFiles()
{
    if (mode() == Vfs::Mode::Off) {
        // there are no placeholders
        return;
    }
    _setupParams->journal->getFilesBelowPath(QString(), [&](const SyncJournalFileRecord &rec) {
        // only handle dehydrated files
        if (rec.type() != ItemTypeVirtualFile && rec.type() != ItemTypeVirtualFileDownload) {
            return;
        }
        const QString relativePath = rec.path();
        qCDebug(lcVfs) << u"Removing db record for dehydrated file" << relativePath;
        _setupParams->journal->deleteFileRecord(relativePath);

        // If the local file is a dehydrated placeholder, wipe it too.
        // Otherwise leave it to allow the next sync to have a new-new conflict.
        const QString absolutePath = _setupParams->filesystemPath + relativePath;
        if (QFile::exists(absolutePath)) {
            // according to our db this is a dehydrated file, check it  to be sure
            if (isDehydratedPlaceholder(absolutePath)) {
                qCDebug(lcVfs) << u"Removing local dehydrated placeholder" << relativePath;
                FileSystem::remove(absolutePath);
            }
        }
    });

    _setupParams->journal->forceRemoteDiscoveryNextSync();

    // Postcondition: No ItemTypeVirtualFile / ItemTypeVirtualFileDownload left in the db.
    // But hydrated placeholders may still be around.
}

HydrationJob* Vfs::hydrateFile(const QByteArray&, const QString&)
{
    // nothing to do
    return nullptr;
}

Q_LOGGING_CATEGORY(lcPlugin, "sync.plugins", QtInfoMsg)

OCC::VfsPluginManager *OCC::VfsPluginManager::_instance = nullptr;

bool OCC::VfsPluginManager::isVfsPluginAvailable(Vfs::Mode mode) const
{
    {
        auto result = _pluginCache.constFind(mode);
        if (result != _pluginCache.cend()) {
            return *result;
        }
    }
    const bool out = [mode] {
        const QString name = Utility::enumToString(mode);
        if (!OC_ENSURE_NOT(name.isEmpty())) {
            return false;
        }
        auto pluginPath = pluginFileName(QStringLiteral("vfs"), name);
        QPluginLoader loader(pluginPath);

        auto basemeta = loader.metaData();
        if (basemeta.isEmpty() || !basemeta.contains(QStringLiteral("IID"))) {
            qCDebug(lcPlugin) << u"Plugin doesn't exist:" << loader.fileName() << u"LibraryPath:" << QCoreApplication::libraryPaths();
            return false;
        }
        if (basemeta[QStringLiteral("IID")].toString() != QLatin1String("eu.opencloud.PluginFactory")) {
            qCWarning(lcPlugin) << u"Plugin has wrong IID" << loader.fileName() << basemeta[QStringLiteral("IID")];
            return false;
        }

        auto metadata = basemeta[QStringLiteral("MetaData")].toObject();
        if (metadata[QStringLiteral("type")].toString() != QLatin1String("vfs")) {
            qCWarning(lcPlugin) << u"Plugin has wrong type" << loader.fileName() << metadata[QStringLiteral("type")];
            return false;
        }
        if (metadata[QStringLiteral("version")].toString() != OCC::Version::version().toString()) {
            qCWarning(lcPlugin) << u"Plugin has wrong version" << loader.fileName() << metadata[QStringLiteral("version")];
            return false;
        }

        // Attempting to load the plugin is essential as it could have dependencies that
        // can't be resolved and thus not be available after all.
        if (!loader.load()) {
            qCWarning(lcPlugin) << u"Plugin failed to load:" << loader.errorString();
            return false;
        }

        return true;
    }();
    _pluginCache[mode] = out;
    return out;
}

Vfs::Mode OCC::VfsPluginManager::bestAvailableVfsMode() const
{
    if (isVfsPluginAvailable(Vfs::Mode::WindowsCfApi)) {
        return Vfs::Mode::WindowsCfApi;
    } else if (isVfsPluginAvailable(Vfs::Mode::XAttr)) {
        return Vfs::Mode::XAttr;
    } else if (isVfsPluginAvailable(Vfs::Mode::Off)) {
        return Vfs::Mode::Off;
    }
    Q_UNREACHABLE();
}


std::pair<QString, PluginFactory *> OCC::VfsPluginManager::createVfsPluginFactory(Vfs::Mode mode) const
{
    auto name = Utility::enumToString(mode);
    if (name.isEmpty())
        return {};
    auto pluginPath = pluginFileName(QStringLiteral("vfs"), name);

    if (!isVfsPluginAvailable(mode)) {
        qCCritical(lcPlugin) << u"Could not load plugin: not existant or bad metadata" << pluginPath;
        return {pluginPath, nullptr};
    }

    QPluginLoader loader(pluginPath);
    auto plugin = loader.instance();
    if (!plugin) {
        qCCritical(lcPlugin) << u"Could not load plugin" << pluginPath << loader.errorString();
        return {pluginPath, nullptr};
    }

    auto factory = qobject_cast<PluginFactory *>(plugin);
    if (!factory) {
        qCCritical(lcPlugin) << u"Plugin" << loader.fileName() << u"does not implement PluginFactory";
        return {pluginPath, nullptr};
    }
    return {pluginPath, factory};
}

std::unique_ptr<Vfs> OCC::VfsPluginManager::createVfsFromPlugin(Vfs::Mode mode) const
{
    const auto [pluginPath, factory] = createVfsPluginFactory(mode);
    if (factory) {
        auto vfs = std::unique_ptr<Vfs>(qobject_cast<Vfs *>(factory->create(nullptr)));
        if (!vfs) {
            qCCritical(lcPlugin) << u"Plugin" << pluginPath << u"does not create a Vfs instance";
            return nullptr;
        }

        qCInfo(lcPlugin) << u"Created VFS instance from plugin" << pluginPath;
        return vfs;
    }
    return nullptr;
}

Result<void, QString> VfsPluginManager::prepare(const QString &path, const QUuid &accountUuid, Vfs::Mode mode) const
{
    const auto canonicalPath = FileSystem::canonicalPath(path);
#ifdef Q_OS_WIN
    if (FileSystem::fileSystemForPath(canonicalPath).startsWith("ReFS"_L1, Qt::CaseInsensitive)) {
        return QApplication::translate("VfsPluginManager", "ReFS is currently not supported.");
    }
#endif
    const auto [pluginPath, factory] = createVfsPluginFactory(mode);
    if (factory) {
        return factory->prepare(canonicalPath, accountUuid);
    }

    return QApplication::translate("VfsPluginManager", "The Virtual filesystem %1 is not supported on this platform").arg(Utility::enumToString(mode));
}

const VfsPluginManager &VfsPluginManager::instance()
{
    if (!_instance) {
        _instance = new VfsPluginManager();
    }
    return *_instance;
}

VfsSetupParams::VfsSetupParams(const AccountPtr &account, const QUrl &baseUrl, const QString &spaceId, const QString &folderDisplayName, SyncEngine *syncEngine)
    : account(account)
    , _baseUrl(baseUrl)
    , _syncEngine(syncEngine)
    , _spaceId(spaceId)
    , _folderDisplayName(folderDisplayName)
{
}

QString VfsSetupParams::folderDisplayName() const
{
    return _folderDisplayName;
}

SyncEngine *VfsSetupParams::syncEngine() const
{
    return _syncEngine;
}

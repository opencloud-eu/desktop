/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#include "configfile.h"
#include "common/asserts.h"
#include "common/utility.h"
#include "common/version.h"
#include "libsync/globalconfig.h"
#include "logger.h"
#include "theme.h"

#include "creds/abstractcredentials.h"

#include "csync_exclude.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLoggingCategory>
#include <QNetworkProxy>
#include <QOperatingSystemVersion>
#include <QSettings>
#include <QStandardPaths>

#include <chrono>
using namespace std::chrono_literals;

namespace OCC {

namespace chrono = std::chrono;

Q_LOGGING_CATEGORY(lcConfigFile, "sync.configfile", QtInfoMsg)
namespace  {
const QString logHttpC() { return QStringLiteral("logHttp"); }
const QString remotePollIntervalC()
{
    return QStringLiteral("remotePollInterval");
}

const QString fullLocalDiscoveryIntervalC()
{
    return QStringLiteral("fullLocalDiscoveryInterval");
}

const QString crashReporterC()
{
    return QStringLiteral("crashReporter");
}
const QString skipUpdateCheckC() { return QStringLiteral("skipUpdateCheck"); }
const QString updateCheckIntervalC() { return QStringLiteral("updateCheckInterval"); }
const QString updateChannelC() { return QStringLiteral("updateChannel"); }
const QString uiLanguageC() { return QStringLiteral("uiLanguage"); }
const QString geometryC() { return QStringLiteral("geometry"); }
const QString timeoutC()
{
    return QStringLiteral("timeout");
}
const QString automaticLogDirC() { return QStringLiteral("logToTemporaryLogDir"); }
const QString numberOfLogsToKeepC()
{
    return QStringLiteral("numberOfLogsToKeep");
}

// The key `clientVersion` stores the version *with* build number of the config file. It is named
// this way, because before 5.0, only the version *without* build number was stored.
const QString clientVersionC() { return QStringLiteral("clientVersion"); }

const QString proxyHostC() { return QStringLiteral("Proxy/host"); }
const QString proxyTypeC() { return QStringLiteral("Proxy/type"); }
const QString proxyPortC() { return QStringLiteral("Proxy/port"); }
const QString proxyUserC()
{
    return QStringLiteral("Proxy/user");
}
const QString proxyNeedsAuthC() { return QStringLiteral("Proxy/needsAuth"); }

const QString useUploadLimitC() { return QStringLiteral("BWLimit/useUploadLimit"); }
const QString useDownloadLimitC() { return QStringLiteral("BWLimit/useDownloadLimit"); }
const QString uploadLimitC() { return QStringLiteral("BWLimit/uploadLimit"); }
const QString downloadLimitC() { return QStringLiteral("BWLimit/downloadLimit"); }

const QString pauseSyncWhenMeteredC()
{
    return QStringLiteral("pauseWhenMetered");
}
const QString moveToTrashC() { return QStringLiteral("moveToTrash"); }

const QString issuesWidgetFilterC()
{
    return QStringLiteral("issuesWidgetFilter");
}

QString excludeFileNameC()
{
    return QStringLiteral("sync-exclude.lst");
}

} // anonymous namespace

QString ConfigFile::_confDir = QString();
const std::chrono::seconds DefaultRemotePollInterval { 30 };

static chrono::milliseconds millisecondsValue(const QSettings &setting, const QString &key,
    chrono::milliseconds defaultValue)
{
    return chrono::milliseconds(setting.value(key, qlonglong(defaultValue.count())).toLongLong());
}

ConfigFile::ConfigFile()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

bool ConfigFile::setConfDir(const QString &value)
{
    QString dirPath = value;
    if (dirPath.isEmpty())
        return false;

    QFileInfo fi(dirPath);
    if (!fi.exists()) {
        QDir().mkpath(dirPath);
        fi.setFile(dirPath);
    }
    if (fi.exists() && fi.isDir()) {
        dirPath = fi.absoluteFilePath();
        qCInfo(lcConfigFile) << u"Using custom config dir " << dirPath;
        _confDir = dirPath;
        return true;
    }
    return false;
}

std::optional<QStringList> ConfigFile::issuesWidgetFilter() const
{
    auto settings = makeQSettings();
    if (settings.contains(issuesWidgetFilterC())) {
        return settings.value(issuesWidgetFilterC()).toStringList();
    }

    return {};
}

void ConfigFile::setIssuesWidgetFilter(const QStringList &checked)
{
    auto settings = makeQSettings();
    settings.setValue(issuesWidgetFilterC(), checked);
    settings.sync();
}

std::chrono::seconds ConfigFile::timeout() const
{
    auto settings = makeQSettings();
    const auto val = settings.value(timeoutC()).toInt(); // default to 5 min
    return val ? std::chrono::seconds(val) : 5min;
}

void ConfigFile::saveGeometry(QWidget *w)
{
    OC_ASSERT(!w->objectName().isNull());
    auto settings = makeQSettings();
    settings.beginGroup(w->objectName());
    settings.setValue(geometryC(), w->saveGeometry());
    settings.sync();
}

void ConfigFile::restoreGeometry(QWidget *w)
{
    w->restoreGeometry(getValue(QStringLiteral("%1/%2").arg(geometryC(), w->objectName())).toByteArray());
}

void ConfigFile::saveGeometryHeader(QHeaderView *header)
{
    if (!header)
        return;
    OC_ASSERT(!header->objectName().isEmpty());

    auto settings = makeQSettings();
    settings.beginGroup(header->objectName());
    settings.setValue(geometryC(), header->saveState());
    settings.sync();
}

bool ConfigFile::restoreGeometryHeader(QHeaderView *header)
{
    Q_ASSERT(header && !header->objectName().isNull());

    auto settings = makeQSettings();
    settings.beginGroup(header->objectName());
    if (settings.contains(geometryC())) {
        header->restoreState(settings.value(geometryC()).toByteArray());
        return true;
    }
    return false;
}

QString ConfigFile::configPath()
{
    if (_confDir.isEmpty()) {
        // On Unix, use the AppConfigLocation for the settings, that's configurable with the XDG_CONFIG_HOME env variable.
        // On Windows, use AppDataLocation, that's where the roaming data is and where we should store the config file
        _confDir = QStandardPaths::writableLocation(Utility::isWindows() ? QStandardPaths::AppDataLocation : QStandardPaths::AppConfigLocation);
    }
    QString dir = _confDir;

    if (!dir.endsWith(QLatin1Char('/')))
        dir.append(QLatin1Char('/'));
    return dir;
}

QString ConfigFile::excludeFile(Scope scope) const
{
    switch (scope) {
    case UserScope:
        return configPath() + excludeFileNameC();
    case SystemScope:
        return ConfigFile::defaultExcludeFile();
    }
    Q_UNREACHABLE();
}

QString ConfigFile::defaultExcludeFile()
{
    return QStringLiteral(":/client/OpenCloud/theme/universal/%1").arg(excludeFileNameC());
}

QString ConfigFile::backup() const
{
    QString baseFile = configFile();
    auto versionString = clientVersionWithBuildNumberString();
    if (!versionString.isEmpty())
        versionString.prepend(QLatin1Char('_'));
    const QString backupFile =
        QStringLiteral("%1.backup_%2%3")
            .arg(baseFile, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")), versionString);

    // If this exact file already exists it's most likely that a backup was
    // already done. (two backup calls directly after each other, potentially
    // even with source alterations in between!)
    if (!QFile::exists(backupFile)) {
        QFile f(baseFile);
        f.copy(backupFile);
    }
    return backupFile;
}

QString ConfigFile::configFile()
{
    return configPath() + Theme::instance()->configFileName();
}

QSettings ConfigFile::makeQSettings()
{
    return {configFile(), QSettings::IniFormat};
}

std::unique_ptr<QSettings> ConfigFile::makeQSettingsPointer()
{
    return std::unique_ptr<QSettings>(new QSettings(makeQSettings()));
}

bool ConfigFile::exists()
{
    return QFileInfo::exists(configFile());
}


chrono::milliseconds ConfigFile::remotePollInterval(std::chrono::seconds defaultVal) const
{
    auto settings = makeQSettings();

    auto defaultPollInterval { DefaultRemotePollInterval };

    // The server default-capabilities was set to 60 in some server releases,
    // which, if interpreted in milliseconds, is pretty small.
    // If the value is above 5 seconds, it was set intentionally.
    // Server admins have to set the value in Milliseconds!
    // i.e. set to greater than 5000 milliseconds on the server to be effective.
    if (defaultVal > chrono::seconds(5)) {
        defaultPollInterval = defaultVal;
    }
    auto remoteInterval = millisecondsValue(settings, remotePollIntervalC(), defaultPollInterval);
    if (remoteInterval < chrono::seconds(5)) {
        remoteInterval = defaultPollInterval;
        qCWarning(lcConfigFile) << u"Remote Interval is less than 5 seconds, reverting to" << remoteInterval.count();
    }
    return remoteInterval;
}

chrono::milliseconds OCC::ConfigFile::fullLocalDiscoveryInterval() const
{
    auto settings = makeQSettings();
    return millisecondsValue(settings, fullLocalDiscoveryIntervalC(), 1h);
}

chrono::milliseconds ConfigFile::updateCheckInterval() const
{
    auto settings = makeQSettings();

    auto defaultInterval = chrono::hours(10);
    auto interval = millisecondsValue(settings, updateCheckIntervalC(), defaultInterval);

    auto minInterval = chrono::minutes(5);
    if (interval < minInterval) {
        qCWarning(lcConfigFile) << u"Update check interval less than five minutes, resetting to 5 minutes";
        interval = minInterval;
    }
    return interval;
}

bool ConfigFile::skipUpdateCheck() const
{
    const auto fallback = getValue(skipUpdateCheckC(), false);
#ifdef Q_OS_WIN
    return GlobalConfig::getPolicySetting(skipUpdateCheckC(), fallback).toBool();
#else
    return fallback.toBool();
#endif
}

void ConfigFile::setSkipUpdateCheck(bool skip)
{
    auto settings = makeQSettings();
    settings.setValue(skipUpdateCheckC(), QVariant(skip));
    settings.sync();
}

QString ConfigFile::updateChannel() const
{
    QString defaultUpdateChannel = QStringLiteral("stable");
    const QString suffix = OCC::Version::suffix();
    if (suffix.startsWith(QLatin1String("daily"))
        || suffix.startsWith(QLatin1String("nightly"))
        || suffix.startsWith(QLatin1String("alpha"))
        || suffix.startsWith(QLatin1String("rc"))
        || suffix.startsWith(QLatin1String("beta"))) {
        defaultUpdateChannel = QStringLiteral("beta");
    }

    auto settings = makeQSettings();
    return settings.value(updateChannelC(), defaultUpdateChannel).toString();
}

void ConfigFile::setUpdateChannel(const QString &channel)
{
    auto settings = makeQSettings();
    settings.setValue(updateChannelC(), channel);
}

QString ConfigFile::uiLanguage() const
{
    auto settings = makeQSettings();
    return settings.value(uiLanguageC(), QString()).toString();
}

void ConfigFile::setUiLanguage(const QString &uiLanguage)
{
    auto settings = makeQSettings();
    settings.setValue(uiLanguageC(), uiLanguage);
}

void ConfigFile::setProxyType(QNetworkProxy::ProxyType proxyType, const QString &host, int port, bool needsAuth, const QString &user)
{
    auto settings = makeQSettings();

    settings.setValue(proxyTypeC(), proxyType);

    if (proxyType == QNetworkProxy::HttpProxy || proxyType == QNetworkProxy::Socks5Proxy) {
        settings.setValue(proxyHostC(), host);
        settings.setValue(proxyPortC(), port);
        settings.setValue(proxyNeedsAuthC(), needsAuth);
        settings.setValue(proxyUserC(), user);
    }
    settings.sync();
}

QVariant ConfigFile::getValue(const QString &param, const QVariant &defaultValue) const
{
    auto setting = makeQSettings().value(param);
    if (!setting.isValid()) {
        return GlobalConfig::getValue(param, defaultValue);
    }
    return setting;
}

void ConfigFile::setValue(const QString &key, const QVariant &value)
{
    auto settings = makeQSettings();

    settings.setValue(key, value);
}

int ConfigFile::proxyType() const
{
    if (Theme::instance()->forceSystemNetworkProxy()) {
        return QNetworkProxy::DefaultProxy;
    }
    return getValue(proxyTypeC()).toInt();
}

QString ConfigFile::proxyHostName() const
{
    return getValue(proxyHostC()).toString();
}

int ConfigFile::proxyPort() const
{
    return getValue(proxyPortC()).toInt();
}

bool ConfigFile::proxyNeedsAuth() const
{
    return getValue(proxyNeedsAuthC()).toBool();
}

QString ConfigFile::proxyUser() const
{
    return getValue(proxyUserC()).toString();
}

int ConfigFile::useUploadLimit() const
{
    return getValue(useUploadLimitC(), 0).toInt();
}

int ConfigFile::useDownloadLimit() const
{
    return getValue(useDownloadLimitC(), 0).toInt();
}

void ConfigFile::setUseUploadLimit(int val)
{
    setValue(useUploadLimitC(), val);
}

void ConfigFile::setUseDownloadLimit(int val)
{
    setValue(useDownloadLimitC(), val);
}

int ConfigFile::uploadLimit() const
{
    return getValue(uploadLimitC(), 10).toInt();
}

int ConfigFile::downloadLimit() const
{
    return getValue(downloadLimitC(), 80).toInt();
}

void ConfigFile::setUploadLimit(int kbytes)
{
    setValue(uploadLimitC(), kbytes);
}

void ConfigFile::setDownloadLimit(int kbytes)
{
    setValue(downloadLimitC(), kbytes);
}

bool ConfigFile::pauseSyncWhenMetered() const
{
    return getValue(pauseSyncWhenMeteredC(), false).toBool();
}

void ConfigFile::setPauseSyncWhenMetered(bool isChecked)
{
    setValue(pauseSyncWhenMeteredC(), isChecked);
}

bool ConfigFile::moveToTrash() const
{
    if (Theme::instance()->enableMoveToTrash()) {
        return getValue(moveToTrashC(), false).toBool();
    }

    return false;
}

void ConfigFile::setMoveToTrash(bool isChecked)
{
    setValue(moveToTrashC(), isChecked);
}

bool ConfigFile::crashReporter() const
{
    auto settings = makeQSettings();
    return settings.value(crashReporterC(), true).toBool();
}

void ConfigFile::setCrashReporter(bool enabled)
{
    auto settings = makeQSettings();
    settings.setValue(crashReporterC(), enabled);
}

bool ConfigFile::automaticLogDir() const
{
    auto settings = makeQSettings();
    return settings.value(automaticLogDirC(), false).toBool();
}

void ConfigFile::setAutomaticLogDir(bool enabled)
{
    auto settings = makeQSettings();
    settings.setValue(automaticLogDirC(), enabled);
}

int ConfigFile::automaticDeleteOldLogs() const
{
    auto settings = makeQSettings();
    return settings.value(numberOfLogsToKeepC()).toInt();
}

void ConfigFile::setAutomaticDeleteOldLogs(int number)
{
    auto settings = makeQSettings();
    settings.setValue(numberOfLogsToKeepC(), number);
}

void ConfigFile::configureHttpLogging(std::optional<bool> enable)
{
    if (enable == std::nullopt) {
        enable = logHttp();
    }

    auto settings = makeQSettings();
    settings.setValue(logHttpC(), enable.value());

    static const QSet<QString> rule = { QStringLiteral("sync.httplogger=true") };

    if (enable.value()) {
        Logger::instance()->addLogRule(rule);
    } else {
        Logger::instance()->removeLogRule(rule);
    }
}

bool ConfigFile::logHttp() const
{
    auto settings = makeQSettings();
    return settings.value(logHttpC(), false).toBool();
}

QString ConfigFile::clientVersionWithBuildNumberString() const
{
    auto settings = makeQSettings();
    return settings.value(clientVersionC(), QString()).toString();
}

void ConfigFile::setClientVersionWithBuildNumberString(const QString &version)
{
    auto settings = makeQSettings();
    settings.setValue(clientVersionC(), version);
}

void ConfigFile::setupDefaultExcludeFilePaths(ExcludedFiles &excludedFiles)
{
    ConfigFile cfg;
    QString systemList = cfg.excludeFile(ConfigFile::SystemScope);
    qCInfo(lcConfigFile) << u"Adding system ignore list to csync:" << systemList;
    excludedFiles.addExcludeFilePath(systemList);

    QString userList = cfg.excludeFile(ConfigFile::UserScope);
    if (QFile::exists(userList)) {
        qCInfo(lcConfigFile) << u"Adding user defined ignore list to csync:" << userList;
        excludedFiles.addExcludeFilePath(userList);
    }
}
}

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

#include "theme.h"
#include "common/depreaction.h"
#include "common/utility.h"
#include "common/version.h"
#include "common/vfs.h"
#include "config.h"
#include "configfile.h"

#include "resources/qmlresources.h"
#include "resources/resources.h"

#include <QSslSocket>
#include <QStyle>
#include <QtCore>
#include <QtGui>

#include "themewatcher.h"

#ifdef THEME_INCLUDE
#include THEME_INCLUDE
#endif

namespace OCC {

Theme *Theme::_instance = nullptr;

QmlUrlButton::QmlUrlButton() { }

QmlUrlButton::QmlUrlButton(const std::tuple<QString, QString, QUrl> &tuple)
    : icon(QStringLiteral("urlIcons/%1").arg(std::get<0>(tuple)))
    , name(std::get<1>(tuple))
    , url(std::get<2>(tuple))
{
}

bool QmlButtonColor::valid() const
{
    return color.isValid() && textColor.isValid() && textColorDisabled.isValid();
}

Theme *Theme::instance()
{
    if (!_instance) {
        _instance = new THEME_CLASS;
        auto *watcher = new Resources::ThemeWatcher(_instance);
        connect(watcher, &Resources::ThemeWatcher::themeChanged, _instance, &Theme::themeChanged);
    }
    return _instance;
}

Theme *Theme::create(QQmlEngine *qmlEngine, QJSEngine *)
{
    Q_ASSERT(qmlEngine->thread() == Theme::instance()->thread());
    QJSEngine::setObjectOwnership(Theme::instance(), QJSEngine::CppOwnership);
    return instance();
}

Theme::~Theme()
{
}

QString Theme::appNameGUI() const
{
    return QStringLiteral(APPLICATION_NAME);
}

QString Theme::appName() const
{
    return QStringLiteral(APPLICATION_SHORTNAME);
}

QString Theme::orgDomainName() const
{
    return QStringLiteral(APPLICATION_REV_DOMAIN);
}

QString Theme::vendor() const
{
    return QStringLiteral(APPLICATION_VENDOR);
}

QString Theme::configFileName() const
{
    return QStringLiteral(APPLICATION_EXECUTABLE ".cfg");
}

QIcon Theme::applicationIcon() const
{
    return Resources::themeUniversalIcon(applicationIconName() + QStringLiteral("-icon"));
}

QString Theme::applicationIconName() const
{
    return QStringLiteral(APPLICATION_ICON_NAME);
}

QIcon Theme::aboutIcon() const
{
    return applicationIcon();
}

QIcon Theme::themeTrayIcon(const SyncResult &result, Resources::IconType iconType) const
{
#ifndef Q_OS_MAC
    // we have a dark sys tray and the theme has support for that
    auto icon =
        Resources::loadIcon(Utility::hasDarkSystray() ? QStringLiteral("dark-systray") : QStringLiteral("light-systray"), syncStateIconName(result), iconType);
#else
    // This defines the icon as a template and enables automatic macOS color handling
    auto icon = Resources::loadIcon(QStringLiteral("mask-systray"), syncStateIconName(result), iconType);
    icon.setIsMask(true);
#endif
    return icon;
}

Theme::Theme()
    : QObject(nullptr)
{
}

QList<QmlUrlButton> Theme::qmlUrlButtons() const
{
    const auto urls = urlButtons();
    QList<QmlUrlButton> out;
    out.reserve(urls.size());
    for (const auto &u : urls) {
        out.append(QmlUrlButton(u));
    }
    return out;
}

bool Theme::multiAccount() const
{
    return true;
}

QUrl Theme::helpUrl() const
{
    return QUrl(QStringLiteral("https://docs.opencloud.eu/docs/category/opencloud-desktop-1"));
}

QUrl Theme::updateCheckUrl() const
{
#ifdef APPLICATION_UPDATE_URL
    return QUrl(QStringLiteral(APPLICATION_UPDATE_URL));
#else
    return {};
#endif
}

bool Theme::wizardSkipAdvancedPage() const
{
    return false;
}

QString Theme::gitSHA1(VersionFormat format) const
{
    const QString gitShahSort = Version::gitSha().left(6);
    if (!aboutShowCopyright()) {
        return gitShahSort;
    }
    const auto gitUrl = QStringLiteral("https://github.com/opencloud-eu/desktop/commit/%1").arg(Version::gitSha());
    switch (format) {
    case Theme::VersionFormat::OneLiner:
        Q_FALLTHROUGH();
    case Theme::VersionFormat::Plain:
        return gitShahSort;
    case Theme::VersionFormat::Url:
        return gitUrl;
    case Theme::VersionFormat::RichText:
        return QStringLiteral("<a href=\"%1\">%3</a>").arg(gitUrl, gitShahSort);
    }
    return QString();
}

QString Theme::aboutVersions(Theme::VersionFormat format) const
{
    const QString br = [&format] {
        switch (format) {
        case Theme::VersionFormat::RichText:
            return QStringLiteral("<br>");
        case Theme::VersionFormat::Url:
            Q_FALLTHROUGH();
        case Theme::VersionFormat::Plain:
            return QStringLiteral("\n");
        case Theme::VersionFormat::OneLiner:
            return QStringLiteral(" ");
        }
        Q_UNREACHABLE();
    }();
    const QString qtVersion = QString::fromUtf8(qVersion());
    const QString qtVersionString = (QLatin1String(QT_VERSION_STR) == qtVersion
            ? qtVersion
            : QCoreApplication::translate("OpenCloudTheme::qtVer", "%1 (Built against Qt %2)").arg(qtVersion, QStringLiteral(QT_VERSION_STR)));
    QString _version = Version::displayString();
    QString gitUrl;
    if (!Version::gitSha().isEmpty()) {
        if (format != Theme::VersionFormat::Url) {
            _version = QCoreApplication::translate("OpenCloudTheme::versionWithSha", "%1 %2").arg(_version, gitSHA1(format));
        } else {
            gitUrl = gitSHA1(format) + br;
        }
    }
    QStringList sysInfo = {QStringLiteral("OS: %1-%2 (build arch: %3, CPU arch: %4)")
                               .arg(QSysInfo::productType(), QSysInfo::kernelVersion(), QSysInfo::buildCpuArchitecture(), Utility::currentCpuArch())};
    // may be called by both GUI and CLI, but we can display QPA only for the former
    if (auto guiApp = qobject_cast<QGuiApplication *>(qApp)) {
        sysInfo << QStringLiteral("QPA: %1").arg(guiApp->platformName());
    }

    return QCoreApplication::translate("OpenCloudTheme::aboutVersions()",
        "%1 %2%7"
        "%8"
        "Libraries Qt %3, %4%7"
        "Using virtual files plugin: %5%7"
        "%6")
        .arg(appName(), _version, qtVersionString, QSslSocket::sslLibraryVersionString(),
            Utility::enumToString(VfsPluginManager::instance().bestAvailableVfsMode()), sysInfo.join(br), br, gitUrl);
}


QString Theme::about() const
{
    return tr("<p>Version %1. For more information visit <a href=\"https://opencloud.eu/\">https://opencloud.eu/</a></p>"
              "<p>For known issues and help, please visit: <a href=\"https://github.com/opencloud-eu/desktop\">GitHub</a></p>"
              "<p>Copyright OpenCloud GmbH<br/>"
              "Copyright ownCloud GmbH</p>"
              "<p>Distributed by OpenCloud GmbH and licensed under the GNU General Public License (GPL) Version 2.0.<br/>"
              "<p><small>%2</small></p>")
        .arg(Utility::escape(Version::displayString()), aboutVersions(Theme::VersionFormat::RichText));
}

bool Theme::aboutShowCopyright() const
{
    return true;
}

QString Theme::syncStateIconName(const SyncResult &result) const
{
    switch (result.status()) {
    case SyncResult::NotYetStarted:
        [[fallthrough]];
    case SyncResult::SyncRunning:
        return QStringLiteral("sync");
    case SyncResult::SyncAbortRequested:
        [[fallthrough]];
    case SyncResult::Paused:
        return QStringLiteral("pause");
    case SyncResult::SyncPrepare:
        [[fallthrough]];
    case SyncResult::Success:
        if (!result.hasUnresolvedConflicts()) {
            return QStringLiteral("ok");
        }
        [[fallthrough]];
    case SyncResult::Problem:
        [[fallthrough]];
    case SyncResult::Undefined:
        // this can happen if no sync connections are configured.
        return QStringLiteral("information");
    case SyncResult::Offline:
        return QStringLiteral("offline");
    case SyncResult::Error:
        [[fallthrough]];
    case SyncResult::SetupError:
        // FIXME: Use problem once we have an icon.
        return QStringLiteral("error");
    }
    Q_UNREACHABLE();
}


QColor Theme::wizardHeaderTitleColor() const
{
    return qApp->palette().text().color();
}

QColor Theme::wizardHeaderBackgroundColor() const
{
    return QColor();
}

QmlButtonColor Theme::primaryButtonColor() const
{
    return {};
}

QmlButtonColor Theme::secondaryButtonColor() const
{
    return {};
}

QIcon Theme::wizardHeaderLogo() const
{
    return applicationIcon();
}

bool Theme::forceSystemNetworkProxy() const
{
    return false;
}

QString Theme::oauthClientId() const
{
    return QStringLiteral("OpenCloudDesktop");
}

QString Theme::oauthClientSecret() const
{
    return QString();
}

QPair<QString, QString> Theme::oauthOverrideAuthUrl() const
{
    return {};
}

QVector<quint16> Theme::oauthPorts() const
{
    // zero means a random port
    return {0};
}

QString Theme::openIdConnectScopes() const
{
    return QStringLiteral("openid offline_access email profile");
}

QString Theme::openIdConnectPrompt() const
{
    return QStringLiteral("select_account consent");
}

bool Theme::oidcEnableDynamicRegistration() const
{
    return true;
}

QString Theme::versionSwitchOutput() const
{
    return aboutVersions(Theme::VersionFormat::Url);
}

bool Theme::connectionValidatorClearCookies() const
{
    return false;
}

bool Theme::enableSocketApiIconSupport() const
{
    return true;
}

bool Theme::warnOnMultipleDb() const
{
    return Resources::isVanillaTheme();
}

bool Theme::allowDuplicatedFolderSyncPair() const
{
    return true;
}

QVector<std::tuple<QString, QString, QUrl>> Theme::urlButtons() const
{
    return {};
}

bool Theme::enableMoveToTrash() const
{
    return true;
}

bool Theme::enableCernBranding() const
{
    return false;
}

bool Theme::withCrashReporter() const
{
#ifdef WITH_CRASHREPORTER
    return true;
#else
    return false;
#endif
}

} // end namespace client

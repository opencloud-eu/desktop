/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
 * Copyright (C) by Erik Verbruggen <erik@verbruggen.consulting>
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

/*
 * Changes for Unix socket support based on Nextcloud Desktop Client:
 * https://github.com/nextcloud/desktop/blob/master/src/gui/socketapi/socketapi_mac.mm
 */

#include "application.h"
#include "guiutility.h"
#include "macOS/fileprovider.h"

#include "libsync/theme.h"

#include <QProcess>

#import <Cocoa/Cocoa.h>
#import <Foundation/NSBundle.h>
#import <Security/Security.h>

namespace OCC {

void Utility::startShellIntegration()
{
    QString bundlePath = QUrl::fromNSURL([NSBundle mainBundle].bundleURL).path();

    auto _system = [](const QString &cmd, const QStringList &args) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(cmd, args);
        if (!process.waitForFinished()) {
            qCWarning(lcGuiUtility) << "Failed to load shell extension:" << cmd
                                    << args.join(QLatin1Char(' ')) << process.errorString();
        } else {
            qCInfo(lcGuiUtility) << (process.exitCode() != 0 ? "Failed to load" : "Loaded")
                                 << "shell extension:" << cmd << args.join(QLatin1Char(' '))
                                 << process.readAll();
        }
    };

    // Check if extension is already registered by querying pluginkit
    auto isExtensionRegistered = [](const QString &bundleId) -> bool {
        QProcess process;
        process.start(QStringLiteral("pluginkit"), {QStringLiteral("-m"), QStringLiteral("-i"), bundleId});
        if (!process.waitForFinished(5000)) {
            return false;
        }
        QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        // Output is empty or "(no matches)" if not registered
        return !output.isEmpty() && !output.contains(QStringLiteral("no matches"));
    };

    QString finderSyncBundleId = Theme::instance()->orgDomainName() + QStringLiteral(".FinderSyncExt");
    QString fileProviderBundleId = Theme::instance()->orgDomainName() + QStringLiteral(".FileProviderExt");

    // Register FinderSyncExt only if not already registered
    // Re-registering can reset the user's enabled/disabled preference
    if (!isExtensionRegistered(finderSyncBundleId)) {
        qCInfo(lcGuiUtility) << "Registering FinderSyncExt for the first time";
        _system(QStringLiteral("pluginkit"), { QStringLiteral("-a"), QStringLiteral("%1Contents/PlugIns/FinderSyncExt.appex/").arg(bundlePath) });
        _system(QStringLiteral("pluginkit"),
            {QStringLiteral("-e"), QStringLiteral("use"), QStringLiteral("-i"), finderSyncBundleId});
    } else {
        qCDebug(lcGuiUtility) << "FinderSyncExt already registered, skipping re-registration";
    }

    // Register FileProviderExt only if not already registered
    if (!isExtensionRegistered(fileProviderBundleId)) {
        qCInfo(lcGuiUtility) << "Registering FileProviderExt for the first time";
        _system(QStringLiteral("pluginkit"), { QStringLiteral("-a"), QStringLiteral("%1Contents/PlugIns/FileProviderExt.appex/").arg(bundlePath) });
        _system(QStringLiteral("pluginkit"),
            {QStringLiteral("-e"), QStringLiteral("use"), QStringLiteral("-i"), fileProviderBundleId});
    } else {
        qCDebug(lcGuiUtility) << "FileProviderExt already registered, skipping re-registration";
    }

    // Initialize FileProvider integration (domain manager + XPC)
    // This will register domains for all existing accounts
    Mac::FileProvider::instance();
}

QString Utility::socketApiSocketPath()
{
    // Unix socket path in App Group container for FinderSyncExt communication
    // Based on Nextcloud's approach: https://github.com/nextcloud/desktop
    //
    // The socket must be in a location accessible to both the main app and the FinderSyncExt.
    // On macOS, this is the App Group container: ~/Library/Group Containers/<TEAM>.<bundle-id>/
    //
    // We try multiple possible App Group IDs to handle both dev and signed builds:
    // 1. Build-time configured prefix (SOCKETAPI_TEAM_IDENTIFIER_PREFIX)
    // 2. Team ID from code signing (if signed)
    // 3. Plain domain without prefix (dev builds)

    QString revDomain = Theme::instance()->orgDomainName();

    // Try to get team identifier from code signing at runtime
    auto getTeamIdentifierFromSigning = []() -> QString {
        NSBundle *mainBundle = [NSBundle mainBundle];
        if (!mainBundle) return QString();

        SecStaticCodeRef staticCode = NULL;
        OSStatus status = SecStaticCodeCreateWithPath((__bridge CFURLRef)mainBundle.bundleURL, kSecCSDefaultFlags, &staticCode);
        if (status != errSecSuccess || !staticCode) return QString();

        CFDictionaryRef signingInfo = NULL;
        status = SecCodeCopySigningInformation(staticCode, kSecCSSigningInformation, &signingInfo);
        CFRelease(staticCode);

        if (status != errSecSuccess || !signingInfo) return QString();

        QString teamId;
        CFStringRef teamIdentifier = (CFStringRef)CFDictionaryGetValue(signingInfo, kSecCodeInfoTeamIdentifier);
        if (teamIdentifier) {
            teamId = QString::fromCFString(teamIdentifier);
        }
        CFRelease(signingInfo);
        return teamId;
    };

    // Build list of App Group IDs to try (in priority order)
    QStringList appGroupIds;

    // 1. Build-time configured prefix
    QString buildTimePrefix = QStringLiteral(SOCKETAPI_TEAM_IDENTIFIER_PREFIX);
    if (!buildTimePrefix.isEmpty()) {
        appGroupIds << (buildTimePrefix + revDomain);
    }

    // 2. Runtime-detected team ID from code signing
    QString teamId = getTeamIdentifierFromSigning();
    if (!teamId.isEmpty()) {
        QString signedAppGroup = teamId + QStringLiteral(".") + revDomain;
        if (!appGroupIds.contains(signedAppGroup)) {
            appGroupIds << signedAppGroup;
        }
    }

    // 3. Plain domain without prefix (dev builds)
    if (!appGroupIds.contains(revDomain)) {
        appGroupIds << revDomain;
    }

    // Try each App Group ID until we find one that works
    for (const QString &appGroupIdStr : appGroupIds) {
        NSString *appGroupId = appGroupIdStr.toNSString();
        qCDebug(lcGuiUtility) << "Trying App Group container:" << appGroupIdStr;

        NSURL *container = [[NSFileManager defaultManager] containerURLForSecurityApplicationGroupIdentifier:appGroupId];
        if (container) {
            NSURL *socketPath = [container URLByAppendingPathComponent:@".socket" isDirectory:NO];
            qCInfo(lcGuiUtility) << "Using App Group socket path:" << QString::fromNSString(socketPath.path);
            return QString::fromNSString(socketPath.path);
        }
    }

    // Fallback for development without App Groups configured
    qCWarning(lcGuiUtility) << "Could not get App Group container for any of:" << appGroupIds
                            << "- falling back to Application Support";

    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
        NSString *appSupport = [paths firstObject];
        NSString *socketDir = [appSupport stringByAppendingPathComponent:@"OpenCloud"];
        [[NSFileManager defaultManager] createDirectoryAtPath:socketDir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        NSString *socketPath = [socketDir stringByAppendingPathComponent:@".socket"];
        qCInfo(lcGuiUtility) << "Using fallback socket path:" << QString::fromNSString(socketPath);
        return QString::fromNSString(socketPath);
    }

    qCCritical(lcGuiUtility) << "Could not determine socket path!";
    return QString();
}

bool Utility::isInstalledByStore()
{
    return false;
}

bool Utility::isFinderSyncExtensionEnabled()
{
    // Check if our FinderSync extension is enabled by querying pluginkit
    // pluginkit -m output format: "+/-  bundle.id(version)"
    // "+" prefix = enabled, "-" prefix = disabled
    QString bundleId = Theme::instance()->orgDomainName() + QStringLiteral(".FinderSyncExt");

    QProcess process;
    process.start(QStringLiteral("pluginkit"), {QStringLiteral("-m"), QStringLiteral("-i"), bundleId});
    if (!process.waitForFinished(5000)) {
        qCWarning(lcGuiUtility) << "pluginkit query timed out";
        return false;
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    // Output format: "+    eu.opencloud.desktop.FinderSyncExt(1.0)" (enabled)
    //            or: "-    eu.opencloud.desktop.FinderSyncExt(1.0)" (disabled)
    //            or: "(no matches)" (not registered)
    bool enabled = output.startsWith(QLatin1Char('+'));
    qCDebug(lcGuiUtility) << "Finder Sync extension" << bundleId << "enabled:" << enabled << "output:" << output;
    return enabled;
}

void Utility::showFinderSyncExtensionManagementInterface()
{
    // Open System Settings/Preferences to the Extensions pane
    // On macOS 13+ (Ventura): System Settings > Privacy & Security > Extensions > Added Extensions
    // On macOS 12 and earlier: System Preferences > Extensions > Finder Extensions

    if (@available(macOS 13.0, *)) {
        // macOS Ventura and later - open Privacy & Security > Extensions
        // The URL scheme for System Settings
        NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.ExtensionsPreferences"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    } else {
        // macOS Monterey and earlier - open System Preferences Extensions pane
        NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preferences.extensions"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }

    qCInfo(lcGuiUtility) << "Opened System Settings Extensions pane";
}

} // namespace OCC

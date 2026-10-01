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

#ifndef _THEME_H
#define _THEME_H

#include "common/utility.h"
#include "resources/resources.h"
#include "syncresult.h"

#include <QFileInfo>
#include <QObject>

namespace OCC {

class SyncResult;

/**
 * @brief The Theme class
 * @ingroup libsync
 */


class OPENCLOUD_SYNC_EXPORT Theme : public QObject
{
    Q_OBJECT
public:
    enum class VersionFormat {
        Plain,
        Url,
        RichText,
        OneLiner
    };
    Q_ENUM(VersionFormat);

    /* returns a singleton instance. */
    static Theme *instance();

    ~Theme() override;

    /**
     * get an sync state icon
     */

    QIcon themeTrayIcon(const SyncResult &result, Resources::IconType iconType = Resources::IconType::BrandedIconWithFallbackToVanillaIcon) const;

    QString syncStateIconName(const SyncResult &result) const;


    /**
     * URL to documentation.
     *
     * This is opened in the browser when the "Help" action is selected from the tray menu.
     *
     * If the function is overridden to return an empty string the action is removed from
     * the menu.
     */
    virtual QUrl helpUrl() const;

    /**
     * The SHA sum of the released git commit
     */
    QString gitSHA1(VersionFormat format = VersionFormat::Plain) const;

    /**
     * The used library versions
     */
    QString aboutVersions(VersionFormat format = VersionFormat::Plain) const;

    /**
     * About dialog contents
     */
    virtual QString about() const;
    virtual bool aboutShowCopyright() const;


    /**
     * @brief Where to check for new Updates.
     */
    QUrl updateCheckUrl() const;

    /**
     * Skip the advanced page and create a sync with the default settings
     */
    virtual bool wizardSkipAdvancedPage() const;

    /**
     * The OAuth client_id, secret pair.
     * Note that client that change these value cannot connect to un-branded OpenCloud.
     */
    virtual QString oauthClientId() const;
    virtual QString oauthClientSecret() const;


    /**
     * By default the client tries to get the OAuth access endpoint and the OAuth token endpoint from /.well-known/openid-configuration
     * Setting this allow authentication without a well known url
     *
     * @return QPair<OAuth access endpoint, OAuth token endpoint>
     */
    virtual QPair<QString, QString> oauthOverrideAuthUrl() const;

    /**
     * List of ports to use for the local redirect server
     */
    virtual QVector<quint16> oauthPorts() const;

    /**
     * Defines whether the client attempts danamic registration with the IdP or uses the
     * oauthClientId() and oauthClientSecret()
     * Default: True
     */
    virtual bool oidcEnableDynamicRegistration() const;

    /**
     * @brief What should be output for the --version command line switch.
     *
     * By default, it's a combination of appName(), version(), the GIT SHA1 and some
     * important dependency versions.
     */
    virtual QString versionSwitchOutput() const;

    /**
     * Whether to clear cookies before checking status.php
     * This is used with F5 BIG-IP seups.
     */
    virtual bool connectionValidatorClearCookies() const;


    /**
     * Enables the response of V2/GET_CLIENT_ICON, default true.
     * See #9167
     */
    virtual bool enableSocketApiIconSupport() const;


    /**
     * Whether to or not to allow multiple sync folder pairs for the same remote folder.
     * Default: true
     */
    virtual bool allowDuplicatedFolderSyncPair() const;

    /**
     * Whether or not to enable move-to-trash instead of deleting files that are gone from the server.
     * Default: true
     */
    virtual bool enableMoveToTrash() const;

    /**
     * Whether to enable the special code for cernbox
     * This includes:
     * - spaces migration
     * - support for .sys.admin#recall#
     */
    bool enableCernBranding() const;

    bool withCrashReporter() const;

protected:
    Theme();

Q_SIGNALS:
    void themeChanged();

private:
    Theme(Theme const &);
    Theme &operator=(Theme const &);

    static Theme *_instance;
};
}
#endif // _THEME_H

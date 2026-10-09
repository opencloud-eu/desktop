// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "resources/opencloudresourceslib.h"

#include <QIcon>
#include <QQmlEngine>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QScopedPointer>
#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtQmlIntegration/qqmlintegration.h>

namespace OCC::Resources {

class JsonThemePrivate;

class ButtonColor
{
    Q_GADGET
    Q_PROPERTY(QColor color MEMBER color CONSTANT)
    Q_PROPERTY(QColor textColor MEMBER textColor CONSTANT)
    Q_PROPERTY(QColor textColorDisabled MEMBER textColorDisabled CONSTANT)
    Q_PROPERTY(bool valid READ valid CONSTANT)
    QML_VALUE_TYPE(buttonColor)

public:
    QColor color = {};
    QColor textColor = {};
    QColor textColorDisabled = {};

    bool valid() const;
};

class UrlButton
{
    Q_GADGET
    Q_PROPERTY(QIcon icon MEMBER icon CONSTANT)
    Q_PROPERTY(QString text MEMBER text CONSTANT)
    Q_PROPERTY(QUrl url MEMBER url CONSTANT)
    QML_VALUE_TYPE(urlButton)

public:
    QIcon icon;
    QString text;
    QUrl url;
};

class OPENCLOUD_RESOURCES_EXPORT JsonTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString applicationName READ applicationName CONSTANT)
    Q_PROPERTY(QString applicationDisplayName READ applicationDisplayName CONSTANT)
    Q_PROPERTY(QString organizationDomain READ organizationDomain CONSTANT)
    Q_PROPERTY(QIcon applicationIcon READ applicationIcon CONSTANT)
    Q_PROPERTY(QColor iconTint READ iconTint NOTIFY iconTintChanged)

    Q_PROPERTY(QColor primaryBackgroundColor READ primaryBackgroundColor CONSTANT)
    Q_PROPERTY(QColor primaryForegroundColor READ primaryForegroundColor CONSTANT)
    Q_PROPERTY(ButtonColor primaryButtonColor READ primaryButtonColor CONSTANT)
    Q_PROPERTY(ButtonColor secondaryButtonColor READ secondaryButtonColor CONSTANT)
    Q_PROPERTY(QIcon wizardLogo READ wizardLogo CONSTANT)
    Q_PROPERTY(bool multiAccount READ multiAccount FINAL CONSTANT)

    Q_PROPERTY(QList<UrlButton> urlButtons READ urlButtons CONSTANT)

    QML_SINGLETON
    QML_ELEMENT
public:
    enum class ThemeType : uint8_t {
        AccountTheme,
        SystemTheme,
    };
    Q_ENUM(ThemeType)

    static JsonTheme &instance();

    // qml signleton
    static JsonTheme *create(QQmlEngine *qmlEngine, QJSEngine *);

    JsonTheme(const QJsonDocument &theme, QObject *parent = nullptr);
    ~JsonTheme() override;

    /**
     * Load theme from json document.
     * @param theme json document
     * @param type theme type
     * @return error message if failed
     */
    // std::expected with c++23
    std::optional<QString> load(const QJsonDocument &theme, ThemeType type = ThemeType::AccountTheme);
    std::optional<QString> loadSystemTheme();

    QString applicationName() const;
    QString applicationDisplayName() const;
    QString organizationDomain() const;
    QIcon applicationIcon() const;

    QUrl serverUrl() const;

    QColor primaryBackgroundColor() const;
    QColor primaryForegroundColor() const;

    ButtonColor primaryButtonColor() const;
    ButtonColor secondaryButtonColor() const;

    QIcon wizardLogo() const;

    /**
     * Whether we allow adding multiple accounts.
     */
    bool multiAccount() const;

    QList<UrlButton> urlButtons() const;

    QColor iconTint();

    bool isUnBranded() const;

Q_SIGNALS:
    void themeLoaded();
    void iconTintChanged();

private:
    Q_DECLARE_PRIVATE(JsonTheme)
    QScopedPointer<JsonThemePrivate> d_ptr;
};
}

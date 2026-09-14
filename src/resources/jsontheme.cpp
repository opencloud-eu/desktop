// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "resources/jsontheme.h"
#include "resources/resources.h"
#include "resources/themewatcher.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtGui/QGuiApplication>


Q_LOGGING_CATEGORY(lcJsonTheme, "sync.resoruces.jsontheme", QtInfoMsg)

using namespace Qt::Literals::StringLiterals;

using namespace OCC::Resources;

namespace {

template <typename T>
T fromJson(const QJsonValue &val)
{
    return val.toVariant().value<T>();
}

template <typename T>
void setFromJson(T &target, const QJsonObject &obj, auto key)
{
    if (auto val = obj.find(key); val != obj.end()) {
        target = fromJson<T>(*val);
    }
}

template <typename T>
T getFromJson(const QJsonObject &obj, auto key, T &&defaultValue)
{
    if (auto val = obj.find(key); val != obj.end()) {
        return fromJson<T>(*val);
    }
    return defaultValue;
}


template <>
QIcon fromJson(const QJsonValue &val)
{
    auto object = val.toObject();
    if (auto result = QByteArray::fromBase64Encoding(object.value("base64"_L1).toString().toUtf8())) {
        QPixmap pixmap;
        pixmap.loadFromData(*result);
        return pixmap;
    }
    return {};
}

template <>
ButtonColor fromJson(const QJsonValue &val)
{
    const auto o = val.toObject();
    return {o.value("color"_L1).toString(), o.value("textColor"_L1).toString(), o.value("textColorDisabled"_L1).toString()};
}

template <>
UrlButton fromJson(const QJsonValue &val)
{
    const auto o = val.toObject();
    return {fromJson<QIcon>(o.value("icon"_L1)), o.value("text"_L1).toString(), QUrl(o.value("url"_L1).toString())};
}

template <>
QList<UrlButton> fromJson(const QJsonValue &val)
{
    const auto array = val.toArray();
    QList<UrlButton> out;
    out.reserve(array.size());
    for (const auto &a : array) {
        out.append(fromJson<UrlButton>(a));
    }
    return out;
}

}


namespace OCC::Resources {
class JsonThemePrivate
{
public:
    bool _systemThemeLoaded = false;

    QString _applicationName = u"OpenCloud"_s;
    QString _applicationDisplayName = u"OpenCloud Desktop"_s;
    QString _organizationDomain = u"eu.opencloud.desktop"_s;
    QIcon _applicationIcon = loadIcon(u"universal"_s, u"opencloud-icon"_s, IconType::VanillaIcon);

    // wizard
    QUrl _serverUrl;
    QIcon _wizardLogo = loadIcon(u"universal"_s, u"wizard_logo"_s, IconType::VanillaIcon);
    QColor _primaryBackgroundColor = QColor("#20434F");
    QColor _primaryForegroundColor = QColor(Qt::white);
    ButtonColor _primaryButtonColor = {.color = "#E2BAFF", .textColor = "#19353F", .textColorDisabled = "#DADADA"};
    ButtonColor _secondaryButtonColor = {.color = "#CA8DF5", .textColor = "#19353F", .textColorDisabled = "#B0B0B0"};
    bool _multiAccount = true;

    // themes
    QColor _tintDarkMode = QColor("#E2BAFF");
    QColor _tintLightMode = QColor("#20434F");

    QList<UrlButton> _urlButtons;
    bool _isUnbranded = true;

    void load(const QJsonDocument &theme, JsonTheme::ThemeType type)
    {
        _isUnbranded = false;
        const auto desktopSettings = theme.object().value("clients"_L1).toObject().value("desktop"_L1).toObject();

        if (type == JsonTheme::ThemeType::SystemTheme) {
            setFromJson(_applicationName, desktopSettings, "applicationName"_L1);
            setFromJson(_applicationDisplayName, desktopSettings, "applicationDisplayName"_L1);
            setFromJson(_organizationDomain, desktopSettings, "organizationDomain"_L1);
            setFromJson(_applicationIcon, desktopSettings, "applicationIcon"_L1);
        }

        setFromJson(_serverUrl, desktopSettings, "serverUrl"_L1);

        setFromJson(_urlButtons, desktopSettings, "urlButtons"_L1);

        if (const auto themes = getFromJson(desktopSettings, "themes"_L1, QJsonObject()); !themes.isEmpty()) {
            setFromJson(_tintDarkMode, themes, "tintDarkMode"_L1);
            setFromJson(_tintLightMode, themes, "tintLightMode"_L1);
        }

        if (const auto wizard = getFromJson(desktopSettings, "wizard"_L1, QJsonObject()); !wizard.isEmpty()) {
            setFromJson(_wizardLogo, wizard, "logo"_L1);
            setFromJson(_primaryBackgroundColor, wizard, "primaryBackgroundColor"_L1);
            setFromJson(_primaryForegroundColor, wizard, "primaryForegroundColor"_L1);
            setFromJson(_primaryButtonColor, wizard, "primaryButtonColor"_L1);
            setFromJson(_secondaryButtonColor, wizard, "secondaryButtonColor"_L1);
            setFromJson(_multiAccount, wizard, "multiAccount"_L1);
        }
    }
};
}

bool ButtonColor::valid() const
{
    return color.isValid() && textColor.isValid() && textColorDisabled.isValid();
}

JsonTheme &JsonTheme::instance()
{
    static auto *theme = new JsonTheme({}, qApp);
    return *theme;
}

JsonTheme *JsonTheme::create(QQmlEngine *qmlEngine, QJSEngine *)
{
    Q_ASSERT(qmlEngine->thread() == instance().thread());
    QJSEngine::setObjectOwnership(&instance(), QJSEngine::CppOwnership);
    return &instance();
}

JsonTheme::JsonTheme(const QJsonDocument &theme, QObject *parent)
    : QObject(parent)
    , d_ptr(new JsonThemePrivate)
{
    connect(new Resources::ThemeWatcher(this), &ThemeWatcher::themeChanged, this, &JsonTheme::iconTintChanged);
    if (!theme.isEmpty()) {
        load(theme);
    }
}

JsonTheme::~JsonTheme() = default;

std::optional<QString> JsonTheme::load(const QJsonDocument &theme, ThemeType type)
{
    Q_D(JsonTheme);
    d->load(theme, type);
    Q_EMIT themeLoaded();
    return {};
}

std::optional<QString> JsonTheme::loadSystemTheme()
{
    Q_D(JsonTheme);
    // this must be called only once
    Q_ASSERT(!d->_systemThemeLoaded);
    if (!d->_systemThemeLoaded) {
        // no matter the result of the load, set the app data
        auto updateAppData = qScopeGuard([&] {
            qApp->setOrganizationDomain(organizationDomain());
            qApp->setApplicationName(applicationName());
            qApp->setWindowIcon(applicationIcon());
        });
        d->_systemThemeLoaded = true;


#ifdef Q_OS_MAC
        const QString theme = u"%1/../Resources/opencloud_theme.json"_s.arg(qApp->applicationDirPath());
#else
        const QString theme = u"%1/opencloud_theme.json"_s.arg(qApp->applicationDirPath());
#endif
        if (QFileInfo::exists(theme)) {
            QFile file(theme);
            if (!file.open(QIODevice::ReadOnly)) {
                const QString msg = u"Failed to open %1"_s.arg(theme);
                qCCritical(lcJsonTheme) << msg;
                return {msg};
            }
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
            file.close();

            if (error.error != QJsonParseError::NoError) {
                const QString msg = u"Failed to parse %1: %2 at offset %3"_s.arg(theme, error.errorString(), QString::number(error.offset));
                qCCritical(lcJsonTheme) << msg;
                return {msg};
            }
            return load(doc, ThemeType::SystemTheme);
        }
        return {};
    } else {
        const QString msg = u"System theme already loaded"_s;
        qCCritical(lcJsonTheme) << msg;
        return {msg};
    }
}

QString JsonTheme::applicationName() const
{
    Q_D(const JsonTheme);
    return d->_applicationName;
}

QString JsonTheme::applicationDisplayName() const
{
    Q_D(const JsonTheme);
    return d->_applicationDisplayName;
}

QString JsonTheme::organizationDomain() const
{
    Q_D(const JsonTheme);
    return d->_organizationDomain;
}

QUrl JsonTheme::serverUrl() const
{
    Q_D(const JsonTheme);
    return d->_serverUrl;
}

QColor JsonTheme::primaryBackgroundColor() const
{
    Q_D(const JsonTheme);
    return d->_primaryBackgroundColor;
}

QColor JsonTheme::primaryForegroundColor() const
{
    Q_D(const JsonTheme);
    return d->_primaryForegroundColor;
}

ButtonColor JsonTheme::primaryButtonColor() const
{
    Q_D(const JsonTheme);
    return d->_primaryButtonColor;
}

ButtonColor JsonTheme::secondaryButtonColor() const
{
    Q_D(const JsonTheme);
    return d->_secondaryButtonColor;
}

QIcon JsonTheme::wizardLogo() const
{
    Q_D(const JsonTheme);
    return d->_wizardLogo;
}

bool JsonTheme::multiAccount() const
{
    Q_D(const JsonTheme);
    return d->_multiAccount;
}

QList<UrlButton> JsonTheme::urlButtons() const
{
    Q_D(const JsonTheme);
    return d->_urlButtons;
}

QIcon JsonTheme::applicationIcon() const
{
    Q_D(const JsonTheme);
    return d->_applicationIcon;
}

QColor JsonTheme::iconTint()
{
    Q_D(const JsonTheme);
    return isUsingDarkTheme() ? d->_tintDarkMode : d->_tintLightMode;
}

bool JsonTheme::isUnBranded() const
{
    Q_D(const JsonTheme);
    return d->_isUnbranded;
}

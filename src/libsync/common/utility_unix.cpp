/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "utility.h"

#include "resources/jsontheme.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QUrl>

namespace OCC {

namespace {
    QString installApplicationIcon()
    {
        static const QString name = [] {
            static const QRegularExpression invalidIconNameChars(QStringLiteral("[^a-z0-9_.-]+"));
            const QString iconName = Resources::JsonTheme::instance().applicationName().toLower().replace(invalidIconNameChars, QStringLiteral("-"));
            if (!Utility::runningInAppImage()) {
                return iconName;
            }
            const QIcon icon = Resources::JsonTheme::instance().applicationIcon();
            if (icon.isNull()) {
                return iconName;
            }
            const QString hicolor = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icons/hicolor");
            for (const int size : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
                QPixmap pixmap = icon.pixmap(QSize(size, size), 1.0);
                if (pixmap.size() != QSize(size, size)) {
                    pixmap = pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                QByteArray png;
                QBuffer buffer(&png);
                if (!buffer.open(QIODevice::WriteOnly) || !pixmap.save(&buffer, "PNG")) {
                    qCWarning(lcUtility) << u"Failed to render application icon at size" << size;
                    continue;
                }
                const QString dir = QStringLiteral("%1/%2x%2/apps").arg(hicolor, QString::number(size));
                const QString path = QStringLiteral("%1/%2.png").arg(dir, iconName);
                {
                    QFile existing(path);
                    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == png) {
                        continue;
                    }
                }
                if (!QDir().mkpath(dir)) {
                    qCWarning(lcUtility) << u"Failed to create" << dir;
                    continue;
                }
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) {
                    qCWarning(lcUtility) << u"Failed to write" << path << file.errorString();
                }
            }
            return iconName;
        }();
        return name;
    }

    bool sameLocation(const QUrl &a, const QUrl &b)
    {
        return a.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments) == b.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments);
    }

    void setupGtkBookmark(const QUrl &url, const QString &label, bool override)
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/gtk-3.0/bookmarks");
        QByteArrayList lines;
        {
            QFile file(path);
            if (file.exists()) {
                if (!file.open(QIODevice::ReadOnly)) {
                    qCWarning(lcUtility) << u"Failed to read" << path << file.errorString();
                    return;
                }
                lines = file.readAll().split('\n');
                if (lines.last().isEmpty()) {
                    lines.removeLast();
                }
            }
        }
        const QByteArray entry = url.toEncoded() + ' ' + label.toUtf8();
        auto it = std::find_if(lines.begin(), lines.end(), [&url](const QByteArray &line) {
            const QByteArray location = line.trimmed().split(' ').first();
            return !location.isEmpty() && sameLocation(QUrl::fromEncoded(location), url);
        });
        if (it == lines.end()) {
            lines.append(entry);
        } else if (override && *it != entry) {
            *it = entry;
        } else {
            return;
        }

        if (!QDir().mkpath(QFileInfo(path).path())) {
            qCWarning(lcUtility) << u"Failed to create directory for" << path;
            return;
        }
        const QByteArray content = lines.join('\n') + '\n';
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
            qCWarning(lcUtility) << u"Failed to write" << path << file.errorString();
        }
    }

    void setupKdePlace(const QUrl &url, const QString &title, bool override)
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/user-places.xbel");
        QFile file(path);
        if (!file.exists()) {
            return;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcUtility) << u"Failed to read" << path << file.errorString();
            return;
        }
        QDomDocument doc;
        if (const auto result = doc.setContent(&file); !result) {
            qCWarning(lcUtility) << u"Failed to parse" << path << result.errorMessage << result.errorLine << result.errorColumn;
            return;
        }
        file.close();

        QDomElement root = doc.documentElement();
        if (root.tagName() != QLatin1String("xbel")) {
            qCWarning(lcUtility) << u"Unexpected root element in" << path << root.tagName();
            return;
        }

        const QString iconName = installApplicationIcon();
        auto save = [&doc, &path] {
            QSaveFile out(path);
            const QByteArray data = doc.toByteArray(1);
            if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
                qCWarning(lcUtility) << u"Failed to write" << path << out.errorString();
            }
        };

        int bookmarkCount = 0;
        QDomElement insertAfter;
        for (auto bookmark = root.firstChildElement(QStringLiteral("bookmark")); !bookmark.isNull();
            bookmark = bookmark.nextSiblingElement(QStringLiteral("bookmark"))) {
            const QUrl href(bookmark.attribute(QStringLiteral("href")));
            if (sameLocation(href, url)) {
                if (override) {
                    bool changed = false;
                    auto titleElement = bookmark.firstChildElement(QStringLiteral("title"));
                    if (titleElement.text() != title) {
                        if (titleElement.isNull()) {
                            titleElement = doc.createElement(QStringLiteral("title"));
                            bookmark.insertBefore(titleElement, {});
                        }
                        while (titleElement.hasChildNodes()) {
                            titleElement.removeChild(titleElement.firstChild());
                        }
                        titleElement.appendChild(doc.createTextNode(title));
                        changed = true;
                    }
                    const auto icons = bookmark.elementsByTagName(QStringLiteral("bookmark:icon"));
                    for (int i = 0; i < icons.size(); ++i) {
                        auto iconElement = icons.at(i).toElement();
                        if (iconElement.attribute(QStringLiteral("name")) != iconName) {
                            iconElement.setAttribute(QStringLiteral("name"), iconName);
                            changed = true;
                        }
                    }
                    if (changed) {
                        save();
                    }
                }
                return;
            }
            if (href.isLocalFile()) {
                insertAfter = bookmark;
            }
            ++bookmarkCount;
        }

        auto appendTextElement = [&doc](QDomElement &parent, const QString &name, const QString &text) {
            auto element = doc.createElement(name);
            element.appendChild(doc.createTextNode(text));
            parent.appendChild(element);
        };

        auto bookmark = doc.createElement(QStringLiteral("bookmark"));
        bookmark.setAttribute(QStringLiteral("href"), QString::fromUtf8(url.toEncoded()));
        appendTextElement(bookmark, QStringLiteral("title"), title);

        auto info = doc.createElement(QStringLiteral("info"));
        auto fdoMetadata = doc.createElement(QStringLiteral("metadata"));
        fdoMetadata.setAttribute(QStringLiteral("owner"), QStringLiteral("http://freedesktop.org"));
        auto icon = doc.createElement(QStringLiteral("bookmark:icon"));
        icon.setAttribute(QStringLiteral("name"), iconName);
        fdoMetadata.appendChild(icon);
        info.appendChild(fdoMetadata);

        auto kdeMetadata = doc.createElement(QStringLiteral("metadata"));
        kdeMetadata.setAttribute(QStringLiteral("owner"), QStringLiteral("http://www.kde.org"));
        appendTextElement(kdeMetadata, QStringLiteral("ID"),
            QStringLiteral("%1/%2").arg(QString::number(QDateTime::currentSecsSinceEpoch()), QString::number(bookmarkCount)));
        appendTextElement(kdeMetadata, QStringLiteral("isSystemItem"), QStringLiteral("false"));
        info.appendChild(kdeMetadata);
        bookmark.appendChild(info);

        if (insertAfter.isNull()) {
            root.appendChild(bookmark);
        } else {
            root.insertAfter(bookmark, insertAfter);
        }
        save();
    }
}

void Utility::setupFavLink(const QString &folder, const QString &displayName, const QString &description, bool override)
{
    Q_UNUSED(description)
    const QUrl url = QUrl::fromLocalFile(folder);
    const QString title = displayName.isEmpty() ? QFileInfo(folder).fileName() : displayName;
    setupGtkBookmark(url, title, override);
    setupKdePlace(url, title, override);
}

// returns the autostart directory the linux way
// and respects the XDG_CONFIG_HOME env variable
static QString getUserAutostartDir()
{
    QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    config += QLatin1String("/autostart/");
    return config;
}

bool Utility::hasSystemLaunchOnStartup(const QString &appName)
{
    Q_UNUSED(appName)
    return false;
}

bool Utility::hasLaunchOnStartup(const QString &appName)
{
    QString desktopFileLocation = getUserAutostartDir() + appName + QLatin1String(".desktop");
    return QFile::exists(desktopFileLocation);
}

void Utility::setLaunchOnStartup(const QString &appName, const QString &guiName, bool enable)
{
    const QString userAutoStartPath = getUserAutostartDir();
    const QFileInfo desktopFileLocation(userAutoStartPath + appName + QLatin1String(".desktop"));
    if (enable) {
        if (!QDir().exists(userAutoStartPath) && !QDir().mkpath(userAutoStartPath)) {
            qCWarning(lcUtility) << u"Could not create autostart folder" << userAutoStartPath;
            return;
        }
        QFile iniFile(desktopFileLocation.filePath());
        if (!iniFile.open(QIODevice::WriteOnly)) {
            qCWarning(lcUtility) << u"Could not write auto start entry" << desktopFileLocation;
            return;
        }

        auto autostartApplicationPath = []() -> QString {
            // $APPIMAGE will be set to the AppImage's path by the AppImage runtime
            // if it is set, we can assume to be run from within an AppImage
            // in that case, the desktop file should point to the AppImage rather than the
            // main binary, which will be in a temporary mount point
            if (Utility::runningInAppImage()) {
                return Utility::appImageLocation();
            }

            // we need to launch the client via its provided AppRun script (or at least source the AppRun hooks) to make sure it is displayed correctly
            // if installed as a native package generated by linuxdeploy-plugin-native_packages, a linuxdeploy.conf file will be available that points to the
            // location of the installed AppDir
            QString linuxdeployConfPath = qApp->applicationDirPath() + QStringLiteral("/linuxdeploy.conf");

            if (QFile(linuxdeployConfPath).exists()) {
                QSettings linuxdeployConf(linuxdeployConfPath, QSettings::IniFormat);

                const auto appdirInstallatedPath = linuxdeployConf.value(QStringLiteral("native_packages/appdir_installed_path"));
                if (!appdirInstallatedPath.isNull()) {
                    return appdirInstallatedPath.toString() + QStringLiteral("/AppRun");
                }
            }

            // otherwise, we just use the application binary's own path, which should work for distribution-packaged installations and also for development
            return QCoreApplication::applicationFilePath();
        }();

        QTextStream ts(&iniFile);
        ts.setEncoding(QStringConverter::Utf8);
        ts << QStringLiteral("[Desktop Entry]\n" //
                             "Name=%1\n" //
                             "GenericName=File Synchronizer\n" //
                             "Exec=%2\n" //
                             "Terminal=false\n" //
                             "Icon=%3\n" //
                             "Categories=Network\n" //
                             "Type=Application\n" //
                             "StartupNotify=false\n" //
                             "X-GNOME-Autostart-enabled=true\n" //
                             "X-GNOME-Autostart-Delay=10")
                  .arg(guiName, autostartApplicationPath, installApplicationIcon());
    } else {
        if (desktopFileLocation.exists() && !QFile::remove(desktopFileLocation.filePath())) {
            qCWarning(lcUtility) << u"Could not remove autostart desktop file";
        }
    }
}

bool Utility::hasDarkSystray()
{
    return true;
}

} // namespace OCC

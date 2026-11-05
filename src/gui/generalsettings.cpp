/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "generalsettings.h"

#include "ui_generalsettings.h"

#include "common/restartmanager.h"
#include "gui/application.h"
#include "gui/folderman.h"
#include "gui/ignorelisteditor.h"
#include "gui/logbrowser.h"
#include "gui/networkinformation.h"
#include "gui/settingsdialog.h"
#include "gui/translations.h"
#include "libsync/configfile.h"
#include "libsync/theme.h"
#include "resources/jsontheme.h"

#include <QMessageBox>
#include <QOperatingSystemVersion>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScopedValueRollback>

Q_LOGGING_CATEGORY(lcGeneralSettings, "gui.generalsettings", QtInfoMsg)
namespace OCC {

GeneralSettings::GeneralSettings(QWidget *parent)
    : QWidget(parent)
    , _ui(new Ui::GeneralSettings)
{
    _ui->setupUi(this);

    // Init languages
    _languageDisplayNames.push_back(QObject::tr("(use default)"));
    _languageIds.push_back(QString());

    // initialize map of locales to language names
    const auto availableLocales = []() {
        auto rv = Translations::listAvailableTranslations().values();
        rv.sort(Qt::CaseInsensitive);
        return rv;
    }();

    for (const auto &localeId : availableLocales) {
        auto nativeLanguageName = QLocale(localeId).nativeLanguageName();

        // fallback if there's a locale whose name Qt doesn't know
        // this indicates a broken filename
        if (nativeLanguageName.isEmpty()) {
            qCDebug(lcGeneralSettings) << u"Warning: could not find native language name for locale" << localeId;
            nativeLanguageName = QObject::tr("unknown (%1)").arg(localeId);
        }

        QString displayText = QStringLiteral("%1 (%2)").arg(nativeLanguageName, localeId);
        _languageDisplayNames.push_back(displayText);
        _languageIds.push_back(localeId);
    }

    _ui->quickWidget->setOCContext(QUrl(QStringLiteral("qrc:/qt/qml/eu/OpenCloud/gui/qml/GeneralSettings.qml")), this);
}

GeneralSettings::~GeneralSettings()
{
    delete _ui;
}

bool GeneralSettings::themeHasCrashReporter() const
{
    return Theme::instance()->withCrashReporter();
}

bool GeneralSettings::crashReporter() const
{
    return ConfigFile().crashReporter();
}

void GeneralSettings::setCrashReporter(bool enabled)
{
    ConfigFile().setCrashReporter(enabled);
    Q_EMIT crashReporterChanged();
}

bool GeneralSettings::moveToTrash() const
{
    return ConfigFile().moveToTrash();
}

void GeneralSettings::setMoveToTrash(bool enabled)
{
    ConfigFile().setMoveToTrash(enabled);
    Q_EMIT moveToTrashChanged();
    Q_EMIT syncOptionsChanged();
}

QString GeneralSettings::language() const
{
    return ConfigFile().uiLanguage();
}

void GeneralSettings::setLanguage(QString languageId)
{
    ConfigFile().setUiLanguage(languageId);
    Q_EMIT languageChanged();
}

const QStringList &GeneralSettings::languageDisplayNames()
{
    return _languageDisplayNames;
}

const QStringList &GeneralSettings::languageIds()
{
    return _languageIds;
}

bool GeneralSettings::supportsMetered() const
{
    return NetworkInformation::instance()->supports(NetworkInformation::Feature::Metered);
}

int GeneralSettings::useDownloadLimit() const
{
    return ConfigFile().useDownloadLimit();
}

void GeneralSettings::setUseDownloadLimit(int use)
{
    ConfigFile().setUseDownloadLimit(use);
    Q_EMIT useDownloadLimitChanged();
}

int GeneralSettings::downloadLimit() const
{
    return ConfigFile().downloadLimit();
}

void GeneralSettings::setDownloadLimit(int limit)
{
    ConfigFile().setDownloadLimit(limit);
    Q_EMIT downloadLimitChanged();
}

int GeneralSettings::useUploadLimit() const
{
    return ConfigFile().useUploadLimit();
}

void GeneralSettings::setUseUploadLimit(int use)
{
    ConfigFile().setUseUploadLimit(use);
    Q_EMIT useUploadLimitChanged();
}

int GeneralSettings::uploadLimit() const
{
    return ConfigFile().uploadLimit();
}

void GeneralSettings::setUploadLimit(int limit)
{
    ConfigFile().setUploadLimit(limit);
    Q_EMIT uploadLimitChanged();
}

bool GeneralSettings::pauseSyncWhenMetered() const
{
    return ConfigFile().pauseSyncWhenMetered();
}

void GeneralSettings::setPauseSyncWhenMetered(bool pause)
{
    ConfigFile().setPauseSyncWhenMetered(pause);
    FolderMan::instance()->scheduler()->setPauseSyncWhenMetered(pause);
    Q_EMIT pauseSyncWhenMeteredChanged();
}

void GeneralSettings::slotIgnoreFilesEditor()
{
    auto ignoreEditor = new IgnoreListEditor(ocApp()->settingsDialog());
    ignoreEditor->setAttribute(Qt::WA_DeleteOnClose, true);
    ocApp()->showSettings();
    ignoreEditor->open();
}

void GeneralSettings::slotLogBrowser()
{
    auto logBrowser = new LogBrowser(ocApp()->settingsDialog());
    logBrowser->setAttribute(Qt::WA_DeleteOnClose);
    ocApp()->showSettings();
    logBrowser->open();
}

void GeneralSettings::slotAboutButton()
{
    ocApp()->showAbout();
}

} // namespace OCC

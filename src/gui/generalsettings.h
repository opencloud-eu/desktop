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

#ifndef MIRALL_GENERALSETTINGS_H
#define MIRALL_GENERALSETTINGS_H

#include "qmlutils.h"

#include <QMap>
#include <QWidget>
#include <QPointer>

namespace OCC {
class IgnoreListEditor;
class SyncLogDialog;

namespace Ui {
    class GeneralSettings;
}

/**
 * @brief The GeneralSettings class
 * @ingroup gui
 */
class GeneralSettings : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool themeHasCrashReporter READ themeHasCrashReporter CONSTANT)
    Q_PROPERTY(bool crashReporter READ crashReporter WRITE setCrashReporter NOTIFY crashReporterChanged)
    Q_PROPERTY(bool moveToTrash READ moveToTrash WRITE setMoveToTrash NOTIFY moveToTrashChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QStringList languageDisplayNames READ languageDisplayNames CONSTANT)
    Q_PROPERTY(QStringList languageIds READ languageIds CONSTANT)
    Q_PROPERTY(bool supportsMetered READ supportsMetered CONSTANT)
    Q_PROPERTY(int useDownloadLimit READ useDownloadLimit WRITE setUseDownloadLimit NOTIFY useDownloadLimitChanged)
    Q_PROPERTY(int downloadLimit READ downloadLimit WRITE setDownloadLimit NOTIFY downloadLimitChanged)
    Q_PROPERTY(int useUploadLimit READ useUploadLimit WRITE setUseUploadLimit NOTIFY useUploadLimitChanged)
    Q_PROPERTY(int uploadLimit READ uploadLimit WRITE setUploadLimit NOTIFY uploadLimitChanged)
    Q_PROPERTY(bool pauseSyncWhenMetered READ pauseSyncWhenMetered WRITE setPauseSyncWhenMetered NOTIFY pauseSyncWhenMeteredChanged)
    OC_DECLARE_WIDGET_FOCUS
    QML_ELEMENT
    QML_UNCREATABLE("C++ only")

public:
    explicit GeneralSettings(QWidget *parent = nullptr);
    ~GeneralSettings() override;

    bool themeHasCrashReporter() const;
    bool crashReporter() const;
    void setCrashReporter(bool enabled);
    bool moveToTrash() const;
    void setMoveToTrash(bool enabled);
    QString language() const;
    void setLanguage(QString lang);
    const QStringList &languageDisplayNames();
    const QStringList &languageIds();
    bool supportsMetered() const;
    int useDownloadLimit() const;
    void setUseDownloadLimit(int use);
    int downloadLimit() const;
    void setDownloadLimit(int limit);
    int useUploadLimit() const;
    void setUseUploadLimit(int use);
    int uploadLimit() const;
    void setUploadLimit(int limit);
    bool pauseSyncWhenMetered() const;
    void setPauseSyncWhenMetered(bool pause);

Q_SIGNALS:
    void crashReporterChanged();
    void moveToTrashChanged();
    void languageChanged();
    void useDownloadLimitChanged();
    void downloadLimitChanged();
    void useUploadLimitChanged();
    void uploadLimitChanged();
    void pauseSyncWhenMeteredChanged();

    void syncOptionsChanged();

public Q_SLOTS:
    void slotIgnoreFilesEditor();
    void slotLogBrowser();
    void slotAboutButton();

private:
    Ui::GeneralSettings *_ui;
    QStringList _languageDisplayNames;
    QStringList _languageIds;
};


} // namespace OCC
#endif // MIRALL_GENERALSETTINGS_H

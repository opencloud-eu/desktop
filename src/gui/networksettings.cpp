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

#include "networksettings.h"
#include "networkinformation.h"
#include "ui_networksettings.h"

#include "accountmanager.h"
#include "configfile.h"
#include "folderman.h"
#include "theme.h"


namespace OCC {

Q_LOGGING_CATEGORY(lcNetworkSettings, "gui.networksettings.gui", QtInfoMsg)

NetworkSettings::NetworkSettings(QWidget *parent)
    : QWidget(parent)
    , _ui(new Ui::NetworkSettings)
{
    _ui->setupUi(this);

    setFocusProxy(_ui->pauseSyncWhenMeteredCheckbox);
    loadBWLimitSettings();
    loadMeteredSettings();

    connect(_ui->uploadLimitRadioButton, &QAbstractButton::clicked, this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->noUploadLimitRadioButton, &QAbstractButton::clicked, this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->autoUploadLimitRadioButton, &QAbstractButton::clicked, this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->downloadLimitRadioButton, &QAbstractButton::clicked, this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->noDownloadLimitRadioButton, &QAbstractButton::clicked, this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->autoDownloadLimitRadioButton, &QAbstractButton::clicked, this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->downloadSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &NetworkSettings::saveBWLimitSettings);
    connect(_ui->uploadSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &NetworkSettings::saveBWLimitSettings);

    connect(_ui->pauseSyncWhenMeteredCheckbox, &QAbstractButton::clicked, this, &NetworkSettings::saveMeteredSettings);
}

NetworkSettings::~NetworkSettings()
{
    delete _ui;
}

void NetworkSettings::loadBWLimitSettings()
{
    ConfigFile cfgFile;

    int useDownloadLimit = cfgFile.useDownloadLimit();
    if (useDownloadLimit >= 1) {
        _ui->downloadLimitRadioButton->setChecked(true);
    } else if (useDownloadLimit == 0) {
        _ui->noDownloadLimitRadioButton->setChecked(true);
    } else {
        _ui->autoDownloadLimitRadioButton->setChecked(true);
    }
    _ui->downloadSpinBox->setValue(cfgFile.downloadLimit());

    int useUploadLimit = cfgFile.useUploadLimit();
    if (useUploadLimit >= 1) {
        _ui->uploadLimitRadioButton->setChecked(true);
    } else if (useUploadLimit == 0) {
        _ui->noUploadLimitRadioButton->setChecked(true);
    } else {
        _ui->autoUploadLimitRadioButton->setChecked(true);
    }
    _ui->uploadSpinBox->setValue(cfgFile.uploadLimit());
}

void NetworkSettings::loadMeteredSettings()
{
    if (Utility::isWindows() // The backend implements the metered feature, but does not report it as supported.
                             // See https://bugreports.qt.io/browse/QTBUG-118741
        || NetworkInformation::instance()->supports(NetworkInformation::Feature::Metered)) {
        _ui->pauseSyncWhenMeteredCheckbox->setChecked(ConfigFile().pauseSyncWhenMetered());
        return;
    }

    _ui->pauseSyncWhenMeteredCheckbox->setVisible(false);
}

void NetworkSettings::saveBWLimitSettings()
{
    ConfigFile cfgFile;
    if (_ui->downloadLimitRadioButton->isChecked()) {
        cfgFile.setUseDownloadLimit(1);
    } else if (_ui->noDownloadLimitRadioButton->isChecked()) {
        cfgFile.setUseDownloadLimit(0);
    } else if (_ui->autoDownloadLimitRadioButton->isChecked()) {
        cfgFile.setUseDownloadLimit(-1);
    }
    cfgFile.setDownloadLimit(_ui->downloadSpinBox->value());

    if (_ui->uploadLimitRadioButton->isChecked()) {
        cfgFile.setUseUploadLimit(1);
    } else if (_ui->noUploadLimitRadioButton->isChecked()) {
        cfgFile.setUseUploadLimit(0);
    } else if (_ui->autoUploadLimitRadioButton->isChecked()) {
        cfgFile.setUseUploadLimit(-1);
    }
    cfgFile.setUploadLimit(_ui->uploadSpinBox->value());

    FolderMan::instance()->setDirtyNetworkLimits();
}

void NetworkSettings::saveMeteredSettings()
{
    bool pauseSyncWhenMetered = _ui->pauseSyncWhenMeteredCheckbox->isChecked();
    ConfigFile().setPauseSyncWhenMetered(pauseSyncWhenMetered);
    FolderMan::instance()->scheduler()->setPauseSyncWhenMetered(pauseSyncWhenMetered);
}


} // namespace OCC

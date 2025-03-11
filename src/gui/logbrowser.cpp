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

#include "logbrowser.h"

#include <iostream>

#include <QDesktopServices>
#include <QDir>
#include <QLayout>
#include <QMessageBox>
#include <optional>

#include "configfile.h"
#include "guiutility.h"
#include "logger.h"
#include "resources/fonticon.h"
#include "ui_logbrowser.h"

#include "resources/resources.h"

namespace OCC {

// ==============================================================================

LogBrowser::LogBrowser(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LogBrowser)
{
    ui->setupUi(this);

    ui->warningIcon->setPixmap(Resources::FontIcon(u'').pixmap(ui->warningIcon->size()));
    ui->locationLabel->setText(Logger::instance()->temporaryFolderLogDirPath());

    ui->enableLoggingButton->setChecked(ConfigFile().automaticLogDir());
    connect(ui->enableLoggingButton, &QCheckBox::toggled, this, &LogBrowser::togglePermanentLogging);

    ui->httpLogButton->setChecked(ConfigFile().logHttp());
    connect(ui->httpLogButton, &QCheckBox::toggled, this, [](bool enable) {
        ConfigFile().configureHttpLogging(std::make_optional(enable));
    });

    ui->spinBox_numberOflogsToKeep->setValue(ConfigFile().automaticDeleteOldLogs());
    connect(ui->spinBox_numberOflogsToKeep, qOverload<int>(&QSpinBox::valueChanged), this, [](int i) {
        ConfigFile().setAutomaticDeleteOldLogs(i);
        Logger::instance()->setMaxLogFiles(i);
    });


    connect(ui->openFolderButton, &QPushButton::clicked, this, []() {
        QString path = Logger::instance()->temporaryFolderLogDirPath();
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(ui->buttonBox->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QWidget::close);

    ConfigFile cfg;
    cfg.restoreGeometry(this);
}

LogBrowser::~LogBrowser()
{
}

void LogBrowser::setupLoggingFromConfig()
{
    ConfigFile config;
    auto logger = Logger::instance();

    if (config.automaticLogDir()) {
        // Don't override other configured logging
        if (logger->isLoggingToFile())
            return;

        logger->setupTemporaryFolderLogDir();
        Logger::instance()->setMaxLogFiles(config.automaticDeleteOldLogs());
    } else {
        logger->disableTemporaryFolderLogDir();
    }
}

void LogBrowser::togglePermanentLogging(bool enabled)
{
    ConfigFile config;
    config.setAutomaticLogDir(enabled);
    setupLoggingFromConfig();
}

} // namespace

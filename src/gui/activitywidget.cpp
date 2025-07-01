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

#include "activitywidget.h"


#include "issueswidget.h"
#include "protocolwidget.h"
#include "resources/fonticon.h"
#include "theme.h"

#include <QtGui>
#include <QtWidgets>


using namespace std::chrono;
using namespace std::chrono_literals;


namespace OCC {

ActivitySettings::ActivitySettings(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *hbox = new QHBoxLayout(this);
    setLayout(hbox);

    // create a tab widget for the three activity views
    _tab = new QTabWidget(this);
    hbox->addWidget(_tab);

    _protocolWidget = new ProtocolWidget(this);
    _tab->addTab(_protocolWidget, Resources::FontIcon(u''), tr("Local Activity"));

    _issuesWidget = new IssuesWidget(this);
    const int issueTabId = _tab->addTab(_issuesWidget, Resources::FontIcon(u''), tr("Not Synced"));
    connect(_issuesWidget, &IssuesWidget::issueCountUpdated, this, [issueTabId, this](int issueCount) {
        QString cntText = tr("Not Synced");
        if (issueCount) {
            //: %1 is the number of not synced files.
            cntText = tr("Not Synced (%1)").arg(issueCount);
        }
        _tab->setTabText(issueTabId, cntText);
    });
}

void ActivitySettings::slotShowIssuesTab()
{
    _tab->setCurrentWidget(_issuesWidget);
}

ActivitySettings::~ActivitySettings()
{
}
}

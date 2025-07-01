/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
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

#pragma once

#include "gui/qmlutils.h"

#include <QDialogButtonBox>

namespace OCC {

namespace Ui {
    class AccountModalWidget;
}

class AccountModalWidget : public QWidget
{
    Q_OBJECT
public:
    AccountModalWidget(const QString &title, QWidget *widget, QWidget *parent);
    AccountModalWidget(const QString &title, const QUrl &qmlSource, QObject *qmlContext, QWidget *parent);

    enum class Result { Rejected, Accepted };
    Q_ENUM(Result)

    void setStandardButtons(QDialogButtonBox::StandardButtons buttons);
    QPushButton *addButton(const QString &text, QDialogButtonBox::ButtonRole role);

public Q_SLOTS:
    void accept();
    void reject();

Q_SIGNALS:
    void accepted();
    void rejected();
    void finished(Result result);

private:
    Ui::AccountModalWidget *ui;
};

} // OCC

// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "fonticonmessagebox.h"

#include <QStyle>

OCC::FontIconMessageBox::FontIconMessageBox(
    Resources::FontIcon icon, const QString &title, const QString &text, StandardButtons buttons, QWidget *parent, Qt::WindowFlags flags)
    : QMessageBox(NoIcon, title, text, buttons, parent, flags)
{
    const int iconSize = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    setIconPixmap(icon.pixmap({iconSize, iconSize}, devicePixelRatio()));
}

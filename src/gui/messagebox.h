// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "gui/opencloudguilib.h"

#include "resources/fonticon.h"

#include <QMessageBox>

namespace OCC {
class OPENCLOUD_GUI_EXPORT MessageBox : public QMessageBox
{
public:
    MessageBox(Resources::FontIcon icon, const QString &title, const QString &text, StandardButtons buttons = NoButton, QWidget *parent = nullptr,
        Qt::WindowFlags flags = Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
};
}

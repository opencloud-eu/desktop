/*
 * Copyright (C) Fabian Müller <fmueller@owncloud.com>
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

#include <QWidget>

namespace OCC::Wizard {

class AbstractSetupWizardPage : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    /**
     * Check whether the user input appears valid, so that the user can be allowed to proceed through the wizard.
     * A minimal check for instance is to check whether all required line edits have got data.
     * In case a page does not have user input to be validated, this function may always return true.
     */
    virtual bool validateInput() const = 0;

Q_SIGNALS:
    /**
     * Emitted after a page has been displayed within the wizard.
     * Can be used to, e.g., set the focus on widgets in order to make navigation with the keyboard easier.
     */
    void pageDisplayed();

    /**
     * Emitted whenever the content widgets on the page have their content changed. Useful in cases where changes
     * cannot just be detected with an event filter, e.g., when a user pastes data using the context menu.
     */
    void contentChanged();
};

}

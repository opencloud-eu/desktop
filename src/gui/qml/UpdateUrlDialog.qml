// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import eu.OpenCloud.gui 1.0
import eu.OpenCloud.libsync 1.0
import eu.OpenCloud.resources 1.0

Pane {
    readonly property UpdateUrlDialog updateUrlDialog: ocContext

    palette.window: Theme.brandedBackgoundColor
    palette.windowText: Theme.brandedForegroundColor
    palette.button: Theme.primaryButtonColor.color
    palette.buttonText: Theme.primaryButtonColor.textColor
    palette.disabled.buttonText: Theme.primaryButtonColor.textColorDisabled

    RowLayout {
        anchors.fill: parent
        RowLayout {

            Layout.alignment: Qt.AlignHCenter
            spacing: 30
            Image {
                Layout.alignment: Qt.AlignCenter
                source: OCUtils.resourcePath("fontawesome", "", enabled, FontIcon.Normal, Theme.primaryButtonColor.color)
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 128
                sourceSize.height: 128
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 20
                Label {
                    text: updateUrlDialog.content
                    wrapMode: Label.WordWrap
                }

                RowLayout {
                    spacing: 10
                    Button {
                        text: qsTr("Cancel")
                        onClicked: updateUrlDialog.rejected()
                    }
                    Button {
                        text: updateUrlDialog.requiresRestart ? qsTr("Accept and Restart") : qsTr("Accept")
                        onClicked: updateUrlDialog.accepted()
                    }
                }
            }
        }
    }
}

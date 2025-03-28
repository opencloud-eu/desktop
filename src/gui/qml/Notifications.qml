// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import eu.OpenCloud.gui 1.0
import eu.OpenCloud.libsync 1.0
import eu.OpenCloud.resources 1.0

Pane {
    id: notificationPane
    required property list<notification> notifications

    signal markReadClicked

    spacing: 10

    ColumnLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Notifications")
                font.bold: true
            }

            Item {

                // spacer

                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Mark all as read")
                onClicked: notificationPane.markReadClicked()
            }
        }

        ScrollView {
            id: scrollView

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                anchors.fill: parent

                model: notifications

                delegate: Pane {
                    required property string title
                    required property string message

                    width: ListView.view.width - scrollView.ScrollBar.vertical.width - notificationPane.spacing

                    ColumnLayout {
                        anchors.fill: parent
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.margins: notificationPane.spacing
                            Label {
                                Layout.fillWidth: true
                                text: title
                                wrapMode: Label.WordWrap
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                text: message
                                wrapMode: Label.WordWrap
                            }
                        }
                        MenuSeparator {
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}

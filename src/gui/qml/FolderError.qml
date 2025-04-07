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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import eu.OpenCloud.gui 1.0

ColumnLayout {
    property bool collapsed: true
    required property var errorMessages

    component ErrorItem: RowLayout {
        property alias text: label.text
        property alias maximumLineCount: label.maximumLineCount
        property alias truncated: label.truncated
        Image {
            Layout.alignment: Qt.AlignTop
            source: OpenCloud.resourcePath("fontawesome", "", enabled)
            Layout.maximumHeight: 16
            Layout.maximumWidth: 16
            sourceSize.width: width
            sourceSize.height: height
        }
        Label {
            id: label
            Layout.fillWidth: true
            elide: Label.ElideRight
            wrapMode: Label.WordWrap
        }
    }

    Component {
        id: expandedError
        ColumnLayout {
            Layout.fillWidth: true
            ScrollView {
                id: scrollView
                clip: true
                Layout.fillHeight: true
                Layout.fillWidth: true

                contentWidth: availableWidth

                ListView {
                    model: errorMessages
                    delegate: ErrorItem {
                        width: scrollView.availableWidth
                        required property string modelData
                        text: modelData
                    }
                }
            }
            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Show less")
                onClicked: {
                    collapsed = true;
                }
            }
        }
    }

    Component {
        id: collapsedError
        ColumnLayout {
            Layout.fillWidth: true
            // we will show 2 lines of text or one line and a button
            ErrorItem {
                id: errorItem
                Layout.fillWidth: true
                text: errorMessages[0]
                maximumLineCount: errorMessages.length > 1 ? 1 : 2
            }
            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Show more")
                visible: errorMessages.length > 1 || errorItem.truncated
                onClicked: {
                    collapsed = false;
                }
            }
        }
    }

    function loadComponent() {
        if (errorMessages.length) {
            return collapsed ? collapsedError : expandedError;
        }
        return undefined;
    }

    Loader {
        id: loader
        Layout.fillHeight: true
        Layout.fillWidth: true
        sourceComponent: loadComponent()
    }

    onCollapsedChanged: {
        loader.sourceComponent = loadComponent();
    }

    onErrorMessagesChanged: {
        if (errorMessages.length === 0) {
            collapsed = true;
        }
    }
}

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
import eu.OpenCloud.libsync 1.0

Pane {
    id: spacesView
    // TODO: not cool
    readonly property real normalSize: 170

    readonly property SpacesBrowser spacesBrowser: ocContext
    readonly property OCQuickWidget widget: ocQuickWidget

    Accessible.role: Accessible.List
    Accessible.name: qsTr("Spaces")

    spacing: 10
    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        rightPadding: spacesView.spacing * 2
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AlwaysOn

        contentWidth: availableWidth

        Connections {
            target: widget

            function onFocusFirst() {
                listView.forceActiveFocus(Qt.TabFocusReason);
            }

            function onFocusLast() {
                listView.forceActiveFocus(Qt.TabFocusReason);
            }
        }

        ListView {
            id: listView

            // TODO: why do I need to fill parent here but must not fill parent in FolderDelegate
            anchors.fill: parent

            spacing: spacesView.spacing

            focus: true
            boundsBehavior: Flickable.StopAtBounds

            model: spacesBrowser.model

            Component.onCompleted: {
                // clear the selection delayed, else the palette is messed up
                currentIndex = -1;
            }

            onCurrentItemChanged: {
                if (currentItem) {
                    spacesBrowser.currentSpace = currentItem.space;
                    listView.currentItem.forceActiveFocus(Qt.TabFocusReason);
                } else {
                    // clear the selected item
                    spacesBrowser.currentSpace = null;
                }
            }

            delegate: FocusScope {
                id: spaceDelegate
                required property string name
                required property string subtitle
                required property string accessibleDescription
                required property Space space

                required property int index

                width: scrollView.availableWidth
                implicitHeight: normalSize

                Pane {
                    id: delegatePane

                    anchors.fill: parent

                    Accessible.name: spaceDelegate.accessibleDescription
                    Accessible.role: Accessible.ListItem
                    Accessible.selectable: true
                    Accessible.selected: space === spacesBrowser.currentSpace

                    clip: true

                    activeFocusOnTab: true
                    focus: true

                    Keys.onBacktabPressed: {
                        widget.parentFocusWidget.focusPrevious();
                    }

                    Keys.onTabPressed: {
                        widget.parentFocusWidget.focusNext();
                    }

                    background: Rectangle {
                        color: spaceDelegate.ListView.isCurrentItem ? scrollView.palette.highlight : scrollView.palette.base
                    }
                    SpaceDelegate {
                        anchors.fill: parent
                        spacing: spacesView.spacing

                        title: spaceDelegate.name
                        description: spaceDelegate.subtitle
                        imageSource: spaceDelegate.space.image.qmlImageUrl
                        descriptionWrapMode: Label.WordWrap
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            spaceDelegate.ListView.view.currentIndex = spaceDelegate.index;
                        }
                    }
                }
            }
        }
    }
}

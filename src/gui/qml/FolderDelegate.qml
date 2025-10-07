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
import eu.OpenCloud.resources 1.0

Pane {
    id: folderSyncPanel
    // TODO: not cool
    readonly property real normalSize: 170
    readonly property AccountSettings accountSettings: ocContext
    readonly property OCQuickWidget widget: ocQuickWidget

    spacing: 10

    Accessible.role: Accessible.List
    Accessible.name: qsTr("Folder Sync")

    Connections {
        target: widget

        function onFocusFirst() {
            manageAccountButton.forceActiveFocus(Qt.TabFocusReason);
        }

        function onFocusLast() {
            if (addSyncButton.enabled) {
                addSyncButton.forceActiveFocus(Qt.TabFocusReason);
            } else {
                addSyncButton.nextItemInFocusChain(false).forceActiveFocus(Qt.TabFocusReason);
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            Image {
                source: OpenCloud.resourcePath("fontawesome", accountSettings.accountStateIconGlype, enabled)
                Layout.preferredHeight: 16
                Layout.preferredWidth: 16
                sourceSize.width: width
                sourceSize.height: height
            }

            Label {
                text: accountSettings.connectionLabel
                Layout.fillWidth: true
            }
            // spacer
            Item {}

            Button {
                id: manageAccountButton
                text: qsTr("Manage Account")

                Menu {
                    id: accountMenu

                    MenuItem {
                        text: accountSettings.accountState.state === AccountState.SignedOut ? qsTr("Log in") : qsTr("Log out")
                        onTriggered: accountSettings.slotToggleSignInState()
                    }
                    MenuItem {
                        text: qsTr("Reconnect")
                        enabled: accountSettings.accountState.state !== AccountState.SignedOut && accountSettings.accountState.state !== AccountState.Connected
                        onTriggered: accountSettings.accountState.checkConnectivity(true)
                    }
                    MenuItem {
                        text: CommonStrings.showInWebBrowser()
                        onTriggered: Qt.openUrlExternally(accountSettings.accountState.account.url)
                    }

                    MenuItem {
                        text: qsTr("Remove")
                        onTriggered: accountSettings.slotDeleteAccount()
                    }
                }

                onClicked: {
                    accountMenu.open();
                    Accessible.announce(qsTr("Account options Menu"));
                }

                Keys.onBacktabPressed: {
                    widget.parentFocusWidget.focusPrevious();
                }
            }
        }

        ScrollView {
            id: scrollView
            Layout.fillHeight: true
            Layout.fillWidth: true
            rightPadding: folderSyncPanel.spacing * 2

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

            contentWidth: availableWidth

            clip: true

            ListView {
                id: listView
                focus: true
                boundsBehavior: Flickable.StopAtBounds

                model: accountSettings.model
                spacing: folderSyncPanel.spacing

                onCurrentItemChanged: {
                    if (currentItem) {
                        currentItem.forceActiveFocus(Qt.TabFocusReason);
                    }
                }

                delegate: FocusScope {
                    id: folderDelegate

                    implicitHeight: normalSize
                    width: scrollView.availableWidth

                    required property string displayName
                    required property var errorMsg
                    required property Folder folder
                    required property string itemText
                    required property string overallText
                    required property double progress
                    required property string quota
                    required property string accessibleDescription
                    required property string statusIcon
                    required property string subtitle
                    // model index
                    required property int index

                    Pane {
                        id: delegatePane
                        anchors.fill: parent

                        Accessible.name: folderDelegate.accessibleDescription
                        Accessible.role: Accessible.ListItem

                        clip: true
                        activeFocusOnTab: true
                        focus: true

                        background: Rectangle {
                            color: scrollView.palette.base
                            border.width: delegatePane.visualFocus || folderDelegate.ListView.isCurrentItem ? 2 : 0
                            border.color: delegatePane.visualFocus || folderDelegate.ListView.isCurrentItem ? scrollView.palette.highlight : scrollView.palette.base
                        }

                        Keys.onBacktabPressed: {
                            manageAccountButton.forceActiveFocus(Qt.TabFocusReason);
                        }
                        Keys.onTabPressed: {
                            if (addSyncButton.enabled) {
                                addSyncButton.forceActiveFocus(Qt.TabFocusReason);
                            } else {
                                widget.parentFocusWidget.focusNext();
                            }
                        }

                        Keys.onSpacePressed: {
                            contextMenu.popup();
                        }

                        MouseArea {
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            anchors.fill: parent

                            onClicked: mouse => {
                                if (mouse.button === Qt.RightButton) {
                                    contextMenu.popup();
                                } else {
                                    folderDelegate.ListView.view.currentIndex = folderDelegate.index;
                                    folderDelegate.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                        }

                        SpaceDelegate {
                            anchors.fill: parent
                            spacing: folderSyncPanel.spacing

                            description: folderDelegate.subtitle
                            imageSource: folderDelegate.folder.space ? folderDelegate.folder.space.image.qmlImageUrl : OpenCloud.resourcePath("remixicons", "", enabled, FontIcon.Half)
                            statusSource: OpenCloud.resourcePath("fontawesome", statusIcon, enabled)
                            title: displayName

                            Component {
                                id: progressBar

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    ProgressBar {
                                        Layout.fillWidth: true
                                        value: folderDelegate.progress
                                        visible: folderDelegate.overallText || folderDelegate.itemText
                                        indeterminate: value === 0
                                    }
                                    Label {
                                        id: overallLabel
                                        Accessible.ignored: true
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: folderDelegate.overallText
                                    }
                                    Label {
                                        id: itemTextLabel
                                        Accessible.ignored: true
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: folderDelegate.itemText
                                    }
                                }
                            }
                            Component {
                                id: quotaDisplay

                                Label {
                                    Accessible.ignored: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: folderDelegate.quota
                                    visible: folderDelegate.quota && !folderDelegate.overallText
                                }
                            }

                            // we will either display quota or overallText

                            Loader {
                                id: progressLoader
                                Accessible.ignored: true
                                Layout.fillWidth: true
                                Layout.minimumHeight: folderSyncPanel.spacing

                                Timer {
                                    id: debounce
                                    interval: 500
                                    onTriggered: progressLoader.sourceComponent = folderDelegate.overallText || folderDelegate.itemText ? progressBar : quotaDisplay
                                }

                                Connections {
                                    target: folderDelegate
                                    function onOverallTextChanged() {
                                        debounce.start();
                                    }
                                }
                                Connections {
                                    target: folderDelegate
                                    function onItemTextChanged() {
                                        debounce.start();
                                    }
                                }
                            }

                            FolderError {
                                Accessible.ignored: true
                                Layout.fillWidth: true
                                errorMessages: folderDelegate.errorMsg
                                onCollapsedChanged: {
                                    if (!collapsed) {
                                        // TODO: not cool
                                        folderDelegate.implicitHeight = normalSize + implicitHeight + 10;
                                    } else {
                                        folderDelegate.implicitHeight = normalSize;
                                    }
                                }
                            }
                        }
                    }

                    Image {
                        id: moreButton

                        // directly position at the top
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: folderSyncPanel.spacing

                        // this is just a visual hint that we have a context menu
                        Accessible.ignored: true
                        source: OpenCloud.resourcePath("fontawesome", "", enabled)
                        height: 24
                        width: 24
                        sourceSize: Qt.size(height, width)
                        fillMode: Image.PreserveAspectFit
                        clip: true

                        MouseArea {
                            anchors.fill: parent
                            onClicked: contextMenu.popup()
                        }

                        Menu {
                            id: contextMenu

                            MenuItem {
                                text: CommonStrings.showInFileBrowser()
                                onTriggered: Qt.openUrlExternally("file:///" + folderDelegate.folder.path)
                            }

                            MenuItem {
                                text: CommonStrings.showInWebBrowser()
                                onTriggered: folderDelegate.folder.openInWebBrowser()
                            }

                            MenuSeparator {}

                            MenuItem {
                                text: folderDelegate.folder.isSyncRunning ? qsTr("Restart sync") : qsTr("Force sync now")
                                enabled: accountSettings.accountState.state === AccountState.Connected && !folderDelegate.folder.isSyncPaused
                                onTriggered: accountSettings.slotForceSyncCurrentFolder(folderDelegate.folder)
                                visible: folderDelegate.folder.isReady
                                height: visible ? implicitHeight : 0
                            }

                            MenuItem {
                                text: folderDelegate.folder.isSyncPaused ? qsTr("Resume sync") : qsTr("Pause sync")
                                enabled: accountSettings.accountState.state === AccountState.Connected
                                onTriggered: accountSettings.slotEnableCurrentFolder(folderDelegate.folder, true)
                                visible: folderDelegate.folder.isReady
                                height: visible ? implicitHeight : 0
                            }

                            MenuItem {
                                text: qsTr("Choose what to sync")
                                onTriggered: accountSettings.showSelectiveSyncDialog(folderDelegate.folder)
                                visible: folderDelegate.folder.isReady && folderDelegate.folder.vfsMode !== 1
                                height: visible ? implicitHeight : 0
                            }

                            MenuItem {
                                text: qsTr("Remove Space")
                                onTriggered: accountSettings.slotRemoveCurrentFolder(folderDelegate.folder)
                                visible: !folderDelegate.isDeployed
                                height: visible ? implicitHeight : 0
                            }

                            onOpened: {
                                Accessible.announce(qsTr("Sync options menu"));
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true

            Button {
                id: addSyncButton
                text: qsTr("Add Space")

                onClicked: {
                    accountSettings.slotAddFolder();
                }
                enabled: (accountSettings.accountState.state === AccountState.Connected) && accountSettings.unsyncedSpaces

                Keys.onBacktabPressed: {
                    listView.currentItem.forceActiveFocus(Qt.TabFocusReason);
                }

                Keys.onTabPressed: {
                    widget.parentFocusWidget.focusNext();
                }
            }
            Item {
                // spacer
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("You are synchronizing %1 out of %2 Spaces").arg(accountSettings.syncedSpaces).arg(accountSettings.syncedSpaces + accountSettings.unsyncedSpaces)
                visible: accountSettings.accountState.state === AccountState.Connected
            }
        }
    }
}

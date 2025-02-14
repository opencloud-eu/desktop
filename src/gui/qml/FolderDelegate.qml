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
                source: QMLResources.resourcePath("core", accountSettings.accountStateIconName, enabled)
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

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

            clip: true

            ListView {
                id: listView
                anchors.fill: parent
                focus: true
                boundsBehavior: Flickable.StopAtBounds

                model: accountSettings.model
                spacing: 20

                onCurrentItemChanged: {
                    if (currentItem) {
                        currentItem.forceActiveFocus(Qt.TabFocusReason);
                    }
                }

                delegate: FocusScope {
                    id: folderDelegate

                    implicitHeight: normalSize
                    width: ListView.view.width - scrollView.ScrollBar.vertical.width - spacing

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

                    readonly property real spacing: 10

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
                            moreButton.forceActiveFocus(Qt.TabFocusReason);
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
                        RowLayout {
                            anchors.fill: parent
                            spacing: folderDelegate.spacing

                            SpaceDelegate {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                spacing: folderDelegate.spacing

                                description: folderDelegate.subtitle
                                imageSource: folderDelegate.folder.space ? folderDelegate.folder.space.image.qmlImageUrl : QMLResources.resourcePath("core", "space", enabled)
                                statusSource: QMLResources.resourcePath("core", statusIcon, enabled)
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
                                    Layout.minimumHeight: folderDelegate.spacing

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
                            Button {
                                id: moreButton

                                Accessible.name: delegatePane.Accessible.name
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                Layout.maximumHeight: 30
                                display: AbstractButton.IconOnly
                                icon.source: QMLResources.resourcePath("core", "more", enabled)
                                // this should have no effect, but without it the highlight is not displayed in Qt 6.7 on Windows
                                palette.highlight: folderSyncPanel.palette.higlight

                                Keys.onTabPressed: {
                                    if (addSyncButton.enabled) {
                                        addSyncButton.forceActiveFocus(Qt.TabFocusReason);
                                    } else {
                                        widget.parentFocusWidget.focusNext();
                                    }
                                }

                                Keys.onBacktabPressed: {
                                    parent.forceActiveFocus(Qt.TabFocusReason);
                                }

                                onClicked: {
                                    contextMenu.open();
                                }

                                // select the list item the button belongs to
                                onFocusChanged: {
                                    if (moreButton.focusReason == Qt.TabFocusReason || moreButton.focusReason == Qt.BacktabFocusReason) {
                                        folderDelegate.ListView.view.currentIndex = folderDelegate.index;
                                    }
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
                                    }

                                    MenuItem {
                                        text: folderDelegate.folder.isSyncPaused ? qsTr("Resume sync") : qsTr("Pause sync")
                                        enabled: accountSettings.accountState.state === AccountState.Connected
                                        onTriggered: accountSettings.slotEnableCurrentFolder(folderDelegate.folder, true)
                                        visible: folderDelegate.folder.isReady
                                    }

                                    MenuItem {
                                        text: qsTr("Choose what to sync")
                                        onTriggered: accountSettings.showSelectiveSyncDialog(folderDelegate.folder)
                                        visible: folderDelegate.folder.isReady
                                    }

                                    MenuItem {
                                        text: qsTr("Remove folder sync connection")
                                        onTriggered: accountSettings.slotRemoveCurrentFolder(folderDelegate.folder)
                                        visible: !folderDelegate.isDeployed
                                    }

                                    onOpened: {
                                        Accessible.announce(qsTr("Sync options menu"));
                                    }
                                }
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
                // this should have no effect, but without it the highlight is not displayed in Qt 6.7 on Windows
                palette.highlight: folderSyncPanel.palette.highlight

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
                text: qsTr("You are synchronizing %1 out of %2 spaces").arg(accountSettings.syncedSpaces).arg(accountSettings.syncedSpaces + accountSettings.unsyncedSpaces)
                visible: accountSettings.accountState.state === AccountState.Connected
            }
        }
    }
}

// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Kaloyan Chehlarski <k.chehlarski@opencloud.eu>

import QtQml.Models
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import eu.OpenCloud.gui 1.0

Pane {
    id: generalSettingsPanel
    readonly property GeneralSettings generalSettings: ocContext
    readonly property OCQuickWidget widget: ocQuickWidget

    anchors.fill: parent

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    Connections {
        target: widget

        function onFocusFirst() {
            if (startOnLoginCheckBox.enabled && startOnLoginCheckBox.visible) {
                startOnLoginCheckBox.forceActiveFocus(Qt.TabFocusReason);
            } else {
                languageComboBox.forceActiveFocus(Qt.TabFocusReason);
            }
        }

        function onFocusLast() {
            aboutButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    // Reusable GroupBox with title inside the rectangle
    component OCGroupBox: GroupBox {
        id: groupBoxRoot
        default property alias content: layout.children // add children to the ColumnLayout

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 1

        label: Label {
            visible: false
        }

        ColumnLayout {
            id: layout
            anchors.fill: parent

            spacing: 0

            Label {
                text: groupBoxRoot.title
                horizontalAlignment: Text.AlignHCenter
                Layout.bottomMargin: 10
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top

        OCGroupBox {
            title: qsTr("General Settings")

            CheckBox {
                id: startOnLoginCheckBox
                text: qsTr("Start on login")
                leftPadding: 0
                rightPadding: 0

                // Hide the option completely when installed from the Windows store
                visible: !(Qt.platform.os == "windows" && OCUtils.isInstalledByStore)

                checked: OCUtils.hasSystemLaunchOnStartup || OCUtils.hasLaunchOnStartup
                onToggled: {
                    if (visible && enabled)
                        OCUtils.hasLaunchOnStartup = checked;
                }

                // When the system launch on startup is enabled, grey out the checkbox and show an info tooltip
                enabled: !OCUtils.hasSystemLaunchOnStartup
                ToolTip.text: qsTr("You cannot disable autostart because system-wide autostart is enabled.")
                ToolTip.visible: OCUtils.hasSystemLaunchOnStartup && hovered
            }

            RowLayout {
                Label {
                    text: qsTr("Language")
                }

                ComboBox {
                    id: languageComboBox
                    Layout.fillWidth: true

                    Accessible.name: qsTr("Language selector")

                    Dialog {
                        id: languageChangeRestartDialog
                        parent: generalSettingsPanel

                        anchors.centerIn: parent

                        contentItem: Label {
                            text: qsTr("Language changes require a restart of this application to take effect.")
                        }

                        standardButtons: Dialog.Ok // TODO clicking the button makes it disappear?
                        modal: true
                        closePolicy: Popup.CloseOnEscape
                        popupType: Popup.Window
                        visible: false
                    }

                    textRole: "displayName"
                    valueRole: "languageId"
                    model: generalSettings.allLanguages
                    currentValue: generalSettings.language
                    onCurrentValueChanged: {
                        if (currentValue == generalSettings.language) // Triggered on instantiation
                            return;

                        generalSettings.language = currentValue;
                        languageChangeRestartDialog.visible = true;
                    }

                    Binding { // Without this, selecting Arabic will move the text to the right
                        target: languageComboBox.contentItem
                        property: "horizontalAlignment"
                        value: Qt.AlignLeft
                    }
                }
            }
        }

        OCGroupBox {
            title: qsTr("Advanced")

            CheckBox {
                text: qsTr("Sync hidden files")
                leftPadding: 0
                rightPadding: 0

                checked: !FolderMan.ignoreHiddenFiles
                onToggled: FolderMan.ignoreHiddenFiles = !checked
            }

            CheckBox {
                text: qsTr("Show crash reporter")
                leftPadding: 0
                rightPadding: 0

                checked: generalSettings.crashReporter
                onToggled: generalSettings.crashReporter = checked
                visible: generalSettings.themeHasCrashReporter
            }

            CheckBox {
                text: qsTr("Move remotely deleted files to the local trash bin instead of deleting them")
                leftPadding: 0
                rightPadding: 0

                checked: generalSettings.moveToTrash
                onToggled: generalSettings.moveToTrash = checked
            }

            RowLayout {
                Button {
                    text: qsTr("Edit ignored files")
                    onClicked: generalSettings.slotIgnoreFilesEditor()
                }

                Button {
                    text: qsTr("Log settings")
                    onClicked: generalSettings.slotLogBrowser()
                }
            }
        }

        OCGroupBox {
            title: qsTr("Network")

            CheckBox {
                text: qsTr("Pause synchronization when the Internet connection is metered")
                leftPadding: 0
                rightPadding: 0

                checked: generalSettings.pauseSyncWhenMetered
                onToggled: {
                    if (visible)
                        generalSettings.pauseSyncWhenMetered = checked;
                }

                visible: generalSettings.supportsMetered
            }

            RowLayout {
                NetworkSettingGroup {
                    title: qsTr("Download Bandwidth")

                    useLimit: {
                        useLimit = generalSettings.useDownloadLimit;
                    }
                    onUseLimitChanged: generalSettings.useDownloadLimit = useLimit
                    limit: {
                        limit = generalSettings.downloadLimit;
                    }
                    onLimitChanged: generalSettings.downloadLimit = limit

                    spinBoxAccessibleName: qsTr("Download bandwidth in kilobytes per second")
                }

                NetworkSettingGroup {
                    title: qsTr("Upload Bandwidth")

                    useLimit: {
                        useLimit = generalSettings.useUploadLimit;
                    }
                    onUseLimitChanged: generalSettings.useUploadLimit = useLimit
                    limit: {
                        limit = generalSettings.uploadLimit;
                    }
                    onLimitChanged: generalSettings.uploadLimit = limit

                    spinBoxAccessibleName: qsTr("Upload bandwidth in kilobytes per second")
                }
            }
        }
    }

    // Reusable GroupBox with three radio buttons and a SpinBox
    component NetworkSettingGroup: OCGroupBox {
        required property int useLimit
        required property int limit
        required property string spinBoxAccessibleName

        Item { // needed because of the property alias above
            ButtonGroup {
                id: buttonGroup
                onClicked: function (button) {
                    useLimit = button.value;
                }

                Component.onCompleted: {
                    for (let i = 0; i < buttonGroup.buttons.length; ++i) {
                        if (useLimit == buttonGroup.buttons[i].value) {
                            buttonGroup.buttons[i].checked = true;
                        }
                    }
                }
            }
        }

        RadioButton {
            text: qsTr("No limit")
            ButtonGroup.group: buttonGroup
            property int value: 0
        }

        RadioButton {
            text: qsTr("Limit automatically")
            ButtonGroup.group: buttonGroup
            property int value: -1
        }

        RowLayout {
            RadioButton {
                id: manualLimitRadio
                text: qsTr("Manual limit")
                ButtonGroup.group: buttonGroup
                property int value: 1
            }

            SpinBox {
                id: spinBox
                enabled: manualLimitRadio.checked
                editable: true

                Accessible.name: spinBoxAccessibleName

                from: 1
                to: 9999
                value: limit
                onValueModified: limit = value

                Binding {
                    target: spinBox.contentItem
                    property: "horizontalAlignment"
                    value: Qt.AlignLeft
                }
            }

            Label {
                text: qsTr("KBytes/s")
            }
        }
    }

    Button {
        id: aboutButton
        anchors.bottom: parent.bottom
        anchors.right: parent.right

        text: qsTr("About")
        onClicked: generalSettings.slotAboutButton()
    }
}

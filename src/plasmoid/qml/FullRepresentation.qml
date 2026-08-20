// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.extras as PlasmaExtras

Item {
    id: root

    required property var bridge
    property bool narrow: width < Kirigami.Units.gridUnit * 32
    property var selectedDevice: null

    implicitWidth: Kirigami.Units.gridUnit * 36
    implicitHeight: Kirigami.Units.gridUnit * 26

    Instantiator {
        model: root.bridge ? root.bridge.devices : null

        delegate: QtObject {
            required property int index
            required property string serial
            required property string name
            required property string address
            required property string health
            required property string power
            required property string screensaver
            required property string activeLabel
            required property var volume
            required property var muted
            required property var capabilities
            required property bool stale
            required property bool pendingCommand

            readonly property bool selected: root.bridge
                && serial === root.bridge.selectedSerial

            onSelectedChanged: {
                if (selected) {
                    root.selectedDevice = this;
                    deviceList.currentIndex = index;
                }
            }
            Component.onCompleted: {
                if (selected) {
                    root.selectedDevice = this;
                    deviceList.currentIndex = index;
                }
            }
        }
    }

    GridLayout {
        id: masterDetailLayout

        objectName: "masterDetailLayout"
        anchors.fill: parent
        columns: root.narrow ? 1 : 2
        rows: root.narrow ? 2 : 1
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.largeSpacing

        Item {
            id: devicesPane

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
            Layout.preferredWidth: root.narrow ? -1 : Kirigami.Units.gridUnit * 12
            Layout.minimumHeight: root.narrow ? Kirigami.Units.gridUnit * 7
                                               : Kirigami.Units.gridUnit * 14
            Layout.preferredHeight: root.narrow ? Kirigami.Units.gridUnit * 9 : -1

            PlasmaExtras.Heading {
                id: devicesHeading

                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                }
                level: 2
                text: KI18n.i18n("Devices")
            }

            ListView {
                id: deviceList

                objectName: "deviceList"
                anchors {
                    top: devicesHeading.bottom
                    topMargin: Kirigami.Units.smallSpacing
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                activeFocusOnTab: true
                clip: true
                keyNavigationEnabled: true
                model: root.bridge ? root.bridge.devices : null
                currentIndex: -1
                Accessible.name: KI18n.i18n("Roku devices")
                Accessible.description: KI18n.i18n("Use the arrow keys to choose a Roku device")
                onCurrentItemChanged: {
                    const item = currentItem as DeviceListItem;
                    if (item && item.serial !== root.bridge.selectedSerial) {
                        root.bridge.selectedSerial = item.serial;
                    }
                }
                onCountChanged: {
                    if (count > 0 && currentIndex < 0) {
                        currentIndex = 0;
                    }
                }

                delegate: DeviceListItem {
                    required property int index
                    required property var model

                    width: ListView.view.width
                    current: ListView.isCurrentItem
                    serial: model.serial
                    displayName: model.name
                    health: model.health
                    power: model.power
                    stale: model.stale
                    onSelected: {
                        deviceList.currentIndex = index;
                        root.bridge.selectedSerial = model.serial;
                    }
                }
            }
        }

        Item {
            id: detailPane

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 16
            Layout.minimumHeight: Kirigami.Units.gridUnit * 14

            PlasmaExtras.PlaceholderMessage {
                anchors.centerIn: parent
                visible: root.selectedDevice === null
                iconName: "video-television-off"
                text: KI18n.i18n("No Roku device selected")
                explanation: KI18n.i18n("Add or select a Roku device to show its controls.")
            }

            DeviceDetail {
                objectName: root.selectedDevice ? "selectedDeviceDetail" : ""
                anchors.fill: parent
                visible: root.selectedDevice !== null
                bridge: root.bridge
                serial: root.selectedDevice?.serial ?? ""
                displayName: root.selectedDevice?.name ?? ""
                health: root.selectedDevice?.health ?? "offline"
                power: root.selectedDevice?.power ?? "unknown"
                screensaver: root.selectedDevice?.screensaver ?? "unknown"
                activeLabel: root.selectedDevice?.activeLabel ?? ""
                volume: root.selectedDevice?.volume
                muted: root.selectedDevice?.muted ?? false
                capabilities: root.selectedDevice?.capabilities ?? []
                stale: root.selectedDevice?.stale ?? true
                pendingCommand: root.selectedDevice?.pendingCommand ?? false
            }
        }
    }
}

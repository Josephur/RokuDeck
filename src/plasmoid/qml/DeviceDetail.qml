// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras

Item {
    id: root

    required property var bridge
    required property string serial
    required property string displayName
    required property string health
    required property string power
    required property string screensaver
    required property string activeLabel
    property string address: ""
    required property var volume
    required property bool muted
    required property var capabilities
    required property bool stale
    required property bool pendingCommand
    // Daemon-owned automation policy echoed through the device JSON.
    property int volumeCeiling: 100
    property var visibleInformation: ["status", "activeApp", "volume", "screensaver"]
    property var visibleControls: ["power", "volume", "mute", "navigation", "playback"]

    function shows(key) {
        return (visibleInformation ?? []).indexOf(key) >= 0;
    }

    readonly property string friendlyName: displayName.length > 0
        ? displayName
        : KI18n.i18n("Unnamed Roku device")
    // This PC has no remote: there is nothing on the daemon's own machine to
    // press, so the controls section collapses entirely (rename + ceiling stay).
    readonly property bool isLocalPc: root.serial === "LOCAL-PC"
    // The daemon reports "unknown" whenever it could not determine the screen
    // saver state, so never collapse that into a definite "Inactive" claim.
    readonly property string screensaverText: {
        switch (screensaver.toLowerCase()) {
        case "active":
            return KI18n.i18n("Active");
        case "inactive":
            return KI18n.i18n("Inactive");
        default:
            return KI18n.i18n("Unknown");
        }
    }

    Accessible.name: friendlyName
    Accessible.description: KI18n.i18n("Controls and status for %1", friendlyName)

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaExtras.Heading {
                Layout.fillWidth: true
                level: 2
                elide: Text.ElideRight
                text: root.friendlyName
            }

        RowLayout {
            objectName: "detailStatusRow"

            visible: root.shows("status")
            spacing: Kirigami.Units.smallSpacing

            TvStatusIcon {
                id: tvStatusItem

                objectName: "detailStatusTv"

                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredHeight: width
                health: root.health
                power: root.power
                stale: root.stale
            }

            PlasmaComponents.Label {
                objectName: "detailStatusLabel"

                text: tvStatusItem.statusText
            }
        }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            PlasmaComponents.Label {
                visible: root.shows("activeApp")
                text: KI18n.i18n("Now showing")
                opacity: 0.75
            }
            PlasmaComponents.Label {
                objectName: "activeAppValue"

                Layout.fillWidth: true
                visible: root.shows("activeApp")
                elide: Text.ElideRight
                text: root.activeLabel.length > 0 ? root.activeLabel : KI18n.i18n("Nothing active")
            }

            PlasmaComponents.Label {
                visible: root.shows("volume")
                text: KI18n.i18n("Volume")
                opacity: 0.75
            }
            PlasmaComponents.Label {
                objectName: "volumeValue"

                Layout.fillWidth: true
                visible: root.shows("volume")
                text: root.volume === undefined || root.volume === null
                    ? KI18n.i18n("Unavailable")
                    : (root.muted ? KI18n.i18n("Muted at %1", root.volume)
                                  : KI18n.i18n("%1", root.volume))
            }

            PlasmaComponents.Label {
                visible: root.shows("screensaver")
                text: KI18n.i18n("Screen saver")
                opacity: 0.75
            }
            PlasmaComponents.Label {
                objectName: "screensaverValue"

                Layout.fillWidth: true
                visible: root.shows("screensaver")
                text: root.screensaverText
            }

            PlasmaComponents.Label {
                visible: root.shows("address")
                text: KI18n.i18n("Address")
                opacity: 0.75
            }
            PlasmaComponents.Label {
                objectName: "addressValue"

                Layout.fillWidth: true
                visible: root.shows("address")
                elide: Text.ElideRight
                text: root.address
            }
        }

        RemoteControls {
            objectName: "remoteControls"

            Layout.fillWidth: true
            visible: !root.isLocalPc
            bridge: root.bridge
            visibleControls: root.visibleControls
            serial: root.serial
            deviceName: root.friendlyName
            health: root.health
            power: root.power
            muted: root.muted
            capabilities: root.capabilities
            stale: root.stale
            pendingCommand: root.pendingCommand
        }

        RowLayout {
            objectName: "volumeCeilingRow"
            Layout.fillWidth: true
            visible: root.serial.length > 0
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents.Label {
                text: KI18n.i18n("Volume ceiling")
                opacity: 0.75
            }

            QQC2.SpinBox {
                id: ceilingBox

                objectName: "volumeCeilingSpinbox"
                from: 0
                to: 100
                editable: true
                Accessible.name: KI18n.i18n("Volume ceiling")
                // Programmatic updates never emit valueModified, so echoing
                // the daemon's stored choice cannot loop back into a write.
                Component.onCompleted: value = root.volumeCeiling
                Connections {
                    target: root
                    function onVolumeCeilingChanged() {
                        ceilingBox.value = root.volumeCeiling;
                    }
                }
                onValueModified: root.bridge.updateDevice(root.serial, { volumeCeiling: value })
            }

            PlasmaComponents.Label {
                objectName: "volumeCeilingCaption"
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: KI18n.i18n("Automations never set the volume above this")
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}

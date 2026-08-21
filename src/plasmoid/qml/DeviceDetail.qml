// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
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
    property var visibleInformation: ["status", "activeApp", "volume", "screensaver"]
    property var visibleControls: ["power", "volume", "mute", "navigation", "playback"]

    function shows(key) {
        return (visibleInformation ?? []).indexOf(key) >= 0;
    }

    readonly property string friendlyName: displayName.length > 0
        ? displayName
        : KI18n.i18n("Unnamed Roku device")
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

            StatusBadge {
                objectName: "detailStatusBadge"

                visible: root.shows("status")
                health: root.health
                power: root.power
                stale: root.stale
                showText: true
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
            Layout.fillWidth: true
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

        Item {
            Layout.fillHeight: true
        }
    }
}

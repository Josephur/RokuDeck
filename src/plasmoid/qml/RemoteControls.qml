// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

ColumnLayout {
    id: root

    required property var bridge
    required property string serial
    required property string deviceName
    required property string health
    required property string power
    required property bool muted
    required property var capabilities
    required property bool stale
    required property bool pendingCommand
    property var visibleControls: ["power", "volume", "mute", "navigation", "playback"]

    readonly property bool deviceOnline: bridge
        && bridge.available
        && !stale
        && health.toLowerCase() !== "offline"
        && serial.length > 0
    readonly property bool commandReady: deviceOnline && !pendingCommand
    readonly property bool poweredOn: power.toLowerCase() === "on"
    readonly property bool controlsEnabled: commandReady && poweredOn

    // A control appears only when the user asked for it AND the Roku reports
    // that it supports it, so nothing is ever offered that cannot work.
    function offers(control) {
        return (visibleControls ?? []).indexOf(control) >= 0 && hasCapability(control);
    }

    function hasCapability(capability) {
        if (capabilities === undefined || capabilities === null) {
            return false;
        }
        if (typeof capabilities.includes === "function") {
            return capabilities.includes(capability);
        }
        if (typeof capabilities.get === "function") {
            for (let index = 0; index < capabilities.count; ++index) {
                const entry = capabilities.get(index);
                if (entry === capability || entry.modelData === capability
                        || entry.value === capability || entry.name === capability) {
                    return true;
                }
            }
        }
        return false;
    }

    function dispatch(action, value, allowStandby = false) {
        if (allowStandby ? commandReady : controlsEnabled) {
            bridge.sendCommand(serial, action, value);
        }
    }

    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        Layout.fillWidth: true

        PlasmaComponents.Label {
            Layout.fillWidth: true
            text: KI18n.i18n("Remote controls")
            font.bold: true
        }

        PlasmaComponents.BusyIndicator {
            objectName: "pendingIndicator"

            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
            Layout.preferredHeight: width
            visible: root.pendingCommand
            running: visible
            Accessible.name: KI18n.i18n("Sending command")
            Accessible.description: KI18n.i18n("Waiting for %1 to confirm the command", root.deviceName)
        }
    }

    Flow {
        id: primaryControls

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: powerControls

            objectName: "powerControls"
            width: powerButton.implicitWidth
            height: powerButton.implicitHeight
            visible: root.offers("power")

            PlasmaComponents.ToolButton {
                id: powerButton

                objectName: "powerButton"
                anchors.fill: parent
                activeFocusOnTab: true
                enabled: root.commandReady
                display: QQC2.AbstractButton.IconOnly
                icon.name: root.power.toLowerCase() === "on" ? "system-shutdown" : "system-wake-up"
                text: root.power.toLowerCase() === "on"
                    ? KI18n.i18n("Put %1 in standby", root.deviceName)
                    : KI18n.i18n("Turn on %1", root.deviceName)
                Accessible.name: text
                Accessible.description: KI18n.i18n("Send a power command to %1", root.deviceName)
                onClicked: root.dispatch("power", root.power.toLowerCase() !== "on", true)
            }
        }

        RowLayout {
            id: volumeControls

            objectName: "volumeControls"
            visible: root.offers("volume")
            spacing: 0

            PlasmaComponents.ToolButton {
                objectName: "volumeDownButton"
                activeFocusOnTab: true
                enabled: root.controlsEnabled
                display: QQC2.AbstractButton.IconOnly
                icon.name: "audio-volume-low"
                text: KI18n.i18n("Lower volume on %1", root.deviceName)
                Accessible.name: text
                Accessible.description: KI18n.i18n("Lower the volume by one step")
                onClicked: root.dispatch("volume", -1)
            }

            PlasmaComponents.ToolButton {
                objectName: "volumeUpButton"
                activeFocusOnTab: true
                enabled: root.controlsEnabled
                display: QQC2.AbstractButton.IconOnly
                icon.name: "audio-volume-high"
                text: KI18n.i18n("Raise volume on %1", root.deviceName)
                Accessible.name: text
                Accessible.description: KI18n.i18n("Raise the volume by one step")
                onClicked: root.dispatch("volume", 1)
            }
        }

        PlasmaComponents.ToolButton {
            objectName: "muteButton"
            activeFocusOnTab: true
            visible: root.offers("mute")
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: root.muted ? "audio-volume-high" : "audio-volume-muted"
            text: root.muted ? KI18n.i18n("Unmute %1", root.deviceName)
                             : KI18n.i18n("Mute %1", root.deviceName)
            Accessible.name: text
            Accessible.description: root.muted
                ? KI18n.i18n("Restore audio on %1", root.deviceName)
                : KI18n.i18n("Silence audio on %1", root.deviceName)
            onClicked: root.dispatch("mute", !root.muted)
        }
    }

    GridLayout {
        id: navigationControls

        visible: root.offers("navigation")
        columns: 3
        rows: 3
        columnSpacing: 0
        rowSpacing: 0

        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }
        PlasmaComponents.ToolButton {
            id: upButton
            objectName: "upButton"
            activeFocusOnTab: true
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "arrow-up"
            text: KI18n.i18n("Navigate up on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection up")
            onClicked: root.dispatch("keypress", "Up")
        }
        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }

        PlasmaComponents.ToolButton {
            objectName: "leftButton"
            activeFocusOnTab: true
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "arrow-left"
            text: KI18n.i18n("Navigate left on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection left")
            onClicked: root.dispatch("keypress", "Left")
        }
        PlasmaComponents.ToolButton {
            objectName: "selectButton"
            activeFocusOnTab: true
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "dialog-ok-apply"
            text: KI18n.i18n("Select on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Activate the current selection")
            onClicked: root.dispatch("keypress", "Select")
        }
        PlasmaComponents.ToolButton {
            objectName: "rightButton"
            activeFocusOnTab: true
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "arrow-right"
            text: KI18n.i18n("Navigate right on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection right")
            onClicked: root.dispatch("keypress", "Right")
        }

        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }
        PlasmaComponents.ToolButton {
            objectName: "downButton"
            activeFocusOnTab: true
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "arrow-down"
            text: KI18n.i18n("Navigate down on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection down")
            onClicked: root.dispatch("keypress", "Down")
        }
        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.ToolButton {
            objectName: "backButton"
            activeFocusOnTab: true
            visible: root.offers("navigation")
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "go-previous"
            text: KI18n.i18n("Go back on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Return to the previous Roku screen")
            onClicked: root.dispatch("keypress", "Back")
        }

        PlasmaComponents.ToolButton {
            objectName: "homeButton"
            activeFocusOnTab: true
            visible: root.offers("navigation")
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "go-home"
            text: KI18n.i18n("Go home on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Open the Roku home screen")
            onClicked: root.dispatch("keypress", "Home")
        }

        PlasmaComponents.ToolButton {
            objectName: "playPauseButton"
            activeFocusOnTab: true
            visible: root.offers("playback")
            enabled: root.controlsEnabled
            display: QQC2.AbstractButton.IconOnly
            icon.name: "media-playback-start"
            text: KI18n.i18n("Play or pause on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Toggle media playback")
            onClicked: root.dispatch("keypress", "Play")
        }
    }
}

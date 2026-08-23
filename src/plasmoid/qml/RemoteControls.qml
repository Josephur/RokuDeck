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
    // Deliberately independent of pendingCommand: a command in flight must
    // never lock the panel. Only refreshed device state may disable controls.
    readonly property bool commandReady: deviceOnline
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

    // Icon buttons get a fixed theme-sized footprint: their implicit size
    // otherwise starts at zero until the icon loads, collapsing every row
    // that measures them and bouncing the panel on the first relayout.
    component IconButton: PlasmaComponents.ToolButton {
        id: button

        implicitWidth: root.cell
        implicitHeight: implicitWidth
        activeFocusOnTab: true
        display: QQC2.AbstractButton.IconOnly

        property string keypress: ""
        property bool allowStandby: false

        readonly property bool flash: flashTimer.running

        onClicked: {
            flashTimer.restart();
            root.dispatch("keypress", keypress, allowStandby);
        }

        // Press feedback lives on the pressed control only: a brief spinner
        // fades in over the glyph and everything returns as it was.
        PlasmaComponents.BusyIndicator {
            anchors.centerIn: parent
            width: parent.width * 0.55
            height: width
            visible: opacity > 0
            opacity: parent.flash ? 1 : 0
            running: parent.flash
            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.shortDuration }
            }
        }

        Timer {
            id: flashTimer
            interval: 900
        }
    }
    readonly property real cell: Kirigami.Units.iconSizes.medium

    RowLayout {
        Layout.fillWidth: true

        PlasmaComponents.Label {
            Layout.fillWidth: true
            text: KI18n.i18n("Remote controls")
            font.bold: true
        }

        PlasmaComponents.BusyIndicator {
            objectName: "pendingIndicator"

            // The slot is always reserved: toggling visibility would reflow
            // every control below each time a command is sent, so activity
            // fades into the fixed space instead.
            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
            Layout.preferredHeight: width
            opacity: root.pendingCommand ? 1 : 0
            running: root.pendingCommand
            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.shortDuration }
            }
            Accessible.name: KI18n.i18n("Sending command")
            Accessible.description: KI18n.i18n("Waiting for %1 to confirm the command", root.deviceName)
        }
    }

    // Rows use RowLayout rather than Flow: a Flow measures itself before the
    // buttons' icons resolve, caches a zero implicit height, and then snaps
    // the whole panel around on the first relayout.
    RowLayout {
        id: primaryControls

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: powerControls

            objectName: "powerControls"
            width: powerButton.implicitWidth
            height: powerButton.implicitHeight
            visible: root.offers("power")

            IconButton {
                id: powerButton

                objectName: "powerButton"
                anchors.fill: parent
                enabled: root.commandReady
                // One power glyph for both directions, like a physical remote:
                // themes ship no wake icon, and an unknown name would render
                // as an anonymous file placeholder.
                icon.name: "system-shutdown"
                text: root.power.toLowerCase() === "on"
                    ? KI18n.i18n("Put %1 in standby", root.deviceName)
                    : KI18n.i18n("Turn on %1", root.deviceName)
                Accessible.name: text
                Accessible.description: KI18n.i18n("Send a power command to %1", root.deviceName)
                allowStandby: true
                keypress: root.power.toLowerCase() === "on" ? "Power" : "PowerOn"
            }
        }

        RowLayout {
            id: volumeControls

            objectName: "volumeControls"
            visible: root.offers("volume")
            spacing: 0

            IconButton {
                objectName: "volumeDownButton"
                enabled: root.controlsEnabled
                icon.name: "audio-volume-low"
                text: KI18n.i18n("Lower volume on %1", root.deviceName)
                Accessible.name: text
                Accessible.description: KI18n.i18n("Lower the volume by one step")
                keypress: "VolumeDown"
            }

            IconButton {
                objectName: "volumeUpButton"
                enabled: root.controlsEnabled
                icon.name: "audio-volume-high"
                text: KI18n.i18n("Raise volume on %1", root.deviceName)
                Accessible.name: text
                Accessible.description: KI18n.i18n("Raise the volume by one step")
                keypress: "VolumeUp"
            }
        }

        IconButton {
            objectName: "muteButton"
            visible: root.offers("mute")
            enabled: root.controlsEnabled
            // The glyph mirrors the television's current state, like the
            // system volume applet: struck-through while muted.
            icon.name: root.muted ? "audio-volume-muted" : "audio-volume-high"
            text: root.muted ? KI18n.i18n("Unmute %1", root.deviceName)
                             : KI18n.i18n("Mute %1", root.deviceName)
            Accessible.name: text
            Accessible.description: root.muted
                ? KI18n.i18n("Restore audio on %1", root.deviceName)
                : KI18n.i18n("Silence audio on %1", root.deviceName)
            keypress: "VolumeMute"
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
        IconButton {
            id: upButton
            objectName: "upButton"
            enabled: root.controlsEnabled
            icon.name: "arrow-up"
            text: KI18n.i18n("Navigate up on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection up")
            keypress: "Up"
        }
        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }

        IconButton {
            objectName: "leftButton"
            enabled: root.controlsEnabled
            icon.name: "arrow-left"
            text: KI18n.i18n("Navigate left on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection left")
            keypress: "Left"
        }
        IconButton {
            objectName: "selectButton"
            enabled: root.controlsEnabled
            icon.name: "dialog-ok-apply"
            text: KI18n.i18n("Select on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Activate the current selection")
            keypress: "Select"
        }
        IconButton {
            objectName: "rightButton"
            enabled: root.controlsEnabled
            icon.name: "arrow-right"
            text: KI18n.i18n("Navigate right on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection right")
            keypress: "Right"
        }

        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }
        IconButton {
            objectName: "downButton"
            enabled: root.controlsEnabled
            icon.name: "arrow-down"
            text: KI18n.i18n("Navigate down on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Move the selection down")
            keypress: "Down"
        }
        Item { Layout.preferredWidth: upButton.implicitWidth; Layout.preferredHeight: upButton.implicitHeight }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        IconButton {
            objectName: "backButton"
            visible: root.offers("navigation")
            enabled: root.controlsEnabled
            icon.name: "go-previous"
            text: KI18n.i18n("Go back on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Return to the previous Roku screen")
            keypress: "Back"
        }

        IconButton {
            objectName: "homeButton"
            visible: root.offers("navigation")
            enabled: root.controlsEnabled
            icon.name: "go-home"
            text: KI18n.i18n("Go home on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Open the Roku home screen")
            keypress: "Home"
        }

        IconButton {
            objectName: "playPauseButton"
            visible: root.offers("playback")
            enabled: root.controlsEnabled
            icon.name: "media-playback-start"
            text: KI18n.i18n("Play or pause on %1", root.deviceName)
            Accessible.name: text
            Accessible.description: KI18n.i18n("Toggle media playback")
            keypress: "Play"
        }
    }
}

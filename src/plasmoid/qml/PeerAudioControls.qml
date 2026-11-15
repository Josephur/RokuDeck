// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

// Audio controls for network-peer machines (M9c): explicit absolute
// setVolume and setMute commands — PCs take no remote-control keypresses,
// so this is deliberately not RemoteControls. The daemon clamps against the
// peer's own ceiling; these buttons merely keep their step inside it.
RowLayout {
    id: root

    required property var bridge
    required property string serial
    required property string deviceName
    // The last observed reading: null/undefined until the peer reported one.
    required property var volume
    required property bool muted
    required property bool deviceOnline
    required property int ceiling

    readonly property bool volumeKnown: deviceOnline && volume !== undefined
        && volume !== null
    readonly property int currentVolume: volumeKnown ? Math.round(volume) : 0

    spacing: Kirigami.Units.smallSpacing

    PlasmaComponents.Label {
        objectName: "peerAudioCaption"
        Layout.fillWidth: true
        text: KI18n.i18nc("@label", "Machine audio (volume ceiling %1)", root.ceiling)
        opacity: 0.75
    }

    QQC2.Button {
        objectName: "peerVolumeDownButton"

        icon.name: "audio-volume-low"
        Accessible.name: KI18n.i18n("Lower the volume of %1", root.deviceName)
        enabled: root.volumeKnown && root.currentVolume > 0
        onClicked: root.bridge.sendCommand(root.serial, "setVolume",
                                           Math.max(0, root.currentVolume - 5))
    }
    QQC2.Button {
        objectName: "peerVolumeUpButton"

        icon.name: "audio-volume-high"
        Accessible.name: KI18n.i18n("Raise the volume of %1", root.deviceName)
        enabled: root.volumeKnown && root.currentVolume < root.ceiling
        onClicked: root.bridge.sendCommand(root.serial, "setVolume",
                                           Math.min(root.ceiling, root.currentVolume + 5))
    }
    QQC2.Button {
        objectName: "peerMuteButton"

        icon.name: root.muted ? "audio-volume-muted" : "audio-volume-high"
        Accessible.name: root.muted ? KI18n.i18n("Unmute %1", root.deviceName)
                                    : KI18n.i18n("Mute %1", root.deviceName)
        enabled: root.deviceOnline
        onClicked: root.bridge.sendCommand(root.serial, "setMute", !root.muted)
    }
}

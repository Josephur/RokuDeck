// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid

KCM.SimpleKCM {
    id: root

    property var bridge: Plasmoid

    // The listing is a plain snapshot from the daemon (never a live model):
    // every mutation reloads it explicitly, so the page always mirrors what
    // the daemon stores rather than what the user last clicked.
    property var peers: ({})
    property string operationError: ""
    property string pairingCode: ""

    readonly property var peerOperations: ["pairPeer", "removePeerTarget", "revokePairedController", "setPeerListenerEnabled"]
    readonly property bool listenerEnabled: peers.listener !== undefined
        && peers.listener.enabled === true
    readonly property int boundPort: listenerEnabled && peers.listener.boundPort !== undefined
        ? peers.listener.boundPort : 0
    readonly property var controllers: peers.controllers !== undefined ? peers.controllers : []
    readonly property var targets: peers.targets !== undefined ? peers.targets : []

    function reloadPeers() {
        peers = bridge.listPeers();
    }

    Component.onCompleted: reloadPeers()

    Connections {
        target: root.bridge

        function onDeviceOperationFailed(operation, message) {
            if (root.peerOperations.includes(operation)) {
                root.operationError = message;
            }
        }

        function onDeviceOperationSucceeded(operation) {
            if (root.peerOperations.includes(operation)) {
                root.operationError = "";
                root.reloadPeers();
            }
        }
    }

    header: ColumnLayout {
        PlasmaComponents.Label {
            text: KI18n.i18nc("@title", "Peers")
            font.bold: true
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
        }
        PlasmaComponents.Label {
            Layout.fillWidth: true
            visible: root.operationError.length > 0
            text: root.operationError
            color: Kirigami.Theme.negativeTextColor
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        width: root.width - Kirigami.Units.largeSpacing * 2
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.pairingCode.length > 0
            type: Kirigami.MessageType.Information
            text: KI18n.i18nc("@info", "One-time pairing code (valid for 10 minutes, single use): %1",
                              root.pairingCode)
        }

        // ── This machine as a peer ──────────────────────────────────────────
        PlasmaExtras.Heading {
            Layout.fillWidth: true
            level: 3
            text: KI18n.i18nc("@title", "This machine")
        }
        RowLayout {
            Layout.fillWidth: true
            QQC2.Switch {
                id: listenerSwitch

                checked: root.listenerEnabled
                onToggled: {
                    root.bridge.setPeerListenerEnabled(checked);
                    // A rejected change must not leave the switch lying: the
                    // binding re-arms here and follows the daemon's stored
                    // state once the reload lands.
                    checked = Qt.binding(() => root.listenerEnabled);
                }
            }
            PlasmaComponents.Label {
                Layout.fillWidth: true
                text: root.listenerEnabled
                    ? KI18n.i18nc("@label", "Let paired machines see this PC's audio state (listening on port %1)",
                                  root.boundPort)
                    : KI18n.i18nc("@label", "Let paired machines see this PC's audio state (off)")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.listenerEnabled
            QQC2.Button {
                icon.name: "view-refresh"
                text: KI18n.i18nc("@button", "Show pairing code")
                onClicked: {
                    root.pairingCode = root.bridge.peerPairingCode();
                    if (root.pairingCode.length > 0) {
                        root.reloadPeers();
                    }
                }
            }
            PlasmaComponents.Label {
                Layout.fillWidth: true
                text: KI18n.i18nc("@label", "Give this code to the other machine, once.")
                elide: Text.ElideRight
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── Machines this one controls ──────────────────────────────────────
        PlasmaExtras.Heading {
            Layout.fillWidth: true
            level: 3
            text: KI18n.i18nc("@title", "Paired peers")
        }
        PlasmaExtras.PlaceholderMessage {
            Layout.fillWidth: true
            visible: root.targets.length === 0
            text: KI18n.i18nc("@info", "No paired peers yet")
        }
        Repeater {
            model: root.targets

            delegate: RowLayout {
                required property var modelData

                Layout.fillWidth: true
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    text: modelData.displayName + " (" + modelData.machineId + ")  ·  "
                          + modelData.address + ":" + modelData.port
                    elide: Text.ElideMiddle
                }
                QQC2.Button {
                    icon.name: "list-remove"
                    text: KI18n.i18nc("@button", "Remove")
                    onClicked: root.bridge.removePeerTarget(modelData.machineId)
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        PlasmaExtras.Heading {
            Layout.fillWidth: true
            level: 3
            text: KI18n.i18nc("@title", "Add a peer")
        }
        QQC2.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: addressField

                Layout.fillWidth: true
                placeholderText: KI18n.i18nc("@placeholder", "IP address of the other machine")
            }
            QQC2.TextField {
                id: portField

                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                placeholderText: KI18n.i18nc("@placeholder", "Port (default 49160)")
            }
            QQC2.TextField {
                id: codeField

                Layout.fillWidth: true
                placeholderText: KI18n.i18nc("@placeholder", "One-time pairing code from that machine")
            }
        }
        QQC2.Button {
            icon.name: "network-connect"
            text: KI18n.i18nc("@button", "Pair")
            enabled: addressField.text.length > 0 && portField.text.length > 0
                     && codeField.text.length > 0
            onClicked: {
                root.pairingCode = "";
                root.bridge.pairPeer(addressField.text, parseInt(portField.text, 10),
                                     codeField.text.trim());
                addressField.text = "";
                portField.text = "";
                codeField.text = "";
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── Machines allowed to see this one ────────────────────────────────
        PlasmaExtras.Heading {
            Layout.fillWidth: true
            level: 3
            text: KI18n.i18nc("@title", "Machines allowed here")
        }
        PlasmaExtras.PlaceholderMessage {
            Layout.fillWidth: true
            visible: root.controllers.length === 0
            text: KI18n.i18nc("@info", "No machines have paired with this one")
        }
        Repeater {
            model: root.controllers

            delegate: RowLayout {
                required property var modelData

                Layout.fillWidth: true
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    text: modelData.machineId
                    elide: Text.ElideMiddle
                }
                QQC2.Button {
                    icon.name: "list-remove"
                    text: KI18n.i18nc("@button", "Revoke")
                    onClicked: root.bridge.revokePairedController(modelData.machineId)
                }
            }
        }
    }
}

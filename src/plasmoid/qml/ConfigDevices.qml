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

import "address.js" as RokuDeckAddress

KCM.SimpleKCM {
    id: root

    property var bridge: Plasmoid

    // Per-widget configuration. Everything else on this page belongs to the
    // daemon and is applied immediately, because a second widget or an
    // automation would otherwise disagree about the same device.
    property var cfg_selectedSerials: []
    property string cfg_defaultSerial: ""

    property string pendingAddress: ""
    property var probedDevice: null
    property string probeError: ""
    property string operationError: ""

    readonly property bool addressValid: RokuDeckAddress.isValidIpv4(candidateAddress.text.trim())
    readonly property bool probeMatchesField: probedDevice !== null
        && probedDevice.address === candidateAddress.text.trim()
    readonly property bool daemonReachable: bridge !== null && bridge.available

    function isSelected(serial) {
        return cfg_selectedSerials.indexOf(serial) >= 0;
    }

    function setSelected(serial, selected) {
        const next = cfg_selectedSerials.filter(entry => entry !== serial);
        if (selected) {
            next.push(serial);
        }
        cfg_selectedSerials = next;
        if (!selected && cfg_defaultSerial === serial) {
            cfg_defaultSerial = next.length > 0 ? next[0] : "";
        }
    }

    function serialsInOrder() {
        const serials = [];
        for (let row = 0; row < deviceRepeater.count; ++row) {
            serials.push(deviceRepeater.itemAt(row).serial);
        }
        return serials;
    }

    function move(serial, offset) {
        const serials = serialsInOrder();
        const from = serials.indexOf(serial);
        const to = from + offset;
        if (from < 0 || to < 0 || to >= serials.length) {
            return;
        }
        serials.splice(to, 0, serials.splice(from, 1)[0]);
        root.bridge.setDeviceOrder(serials);
    }

    Connections {
        target: root.bridge

        function onDeviceTestFinished(address, snapshot, error) {
            root.probeError = error;
            root.probedDevice = error.length > 0 ? null : snapshot;
        }

        function onDeviceOperationFailed(operation, message) {
            root.operationError = message;
        }

        function onDeviceOperationSucceeded(operation) {
            root.operationError = "";
            if (operation === "addDevice") {
                candidateAddress.text = "";
                root.probedDevice = null;
                root.probeError = "";
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            objectName: "daemonUnavailableMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: !root.daemonReachable
            text: KI18n.i18nc("@info", "RokuDeck cannot reach its background service, so devices cannot be changed right now.")
        }

        Kirigami.InlineMessage {
            objectName: "operationErrorMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: root.operationError.length > 0
            text: root.operationError
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaExtras.Heading {
                Layout.fillWidth: true
                level: 3
                text: KI18n.i18nc("@title:group", "Add a Roku device")
            }

            QQC2.Button {
                objectName: "discoverButton"
                enabled: root.daemonReachable && !(root.bridge?.busy ?? false)
                icon.name: "search"
                text: KI18n.i18nc("@action:button", "Search the network")
                onClicked: root.bridge.discoverDevices()
            }
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: candidateAddress

                objectName: "candidateAddress"
                Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Network address:")
                placeholderText: KI18n.i18nc("@info:placeholder", "For example 192.0.2.10")
                inputMethodHints: Qt.ImhPreferNumbers
                onTextChanged: {
                    root.probeError = "";
                    root.probedDevice = null;
                }
            }

            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Button {
                    objectName: "testConnectionButton"
                    // Testing first is what makes the address honest: the serial
                    // that identifies a Roku only comes back from the device.
                    enabled: root.daemonReachable && root.addressValid
                        && !(root.bridge?.busy ?? false)
                    icon.name: "network-connect"
                    text: KI18n.i18nc("@action:button", "Test connection")
                    onClicked: root.bridge.testDevice(candidateAddress.text.trim())
                }

                QQC2.Button {
                    objectName: "addDeviceButton"
                    enabled: root.daemonReachable && root.probeMatchesField
                    icon.name: "list-add"
                    text: KI18n.i18nc("@action:button", "Add device")
                    onClicked: root.bridge.addDevice({
                        serial: root.probedDevice.serial,
                        displayName: root.probedDevice.displayName ?? "",
                        address: root.probedDevice.address
                    })
                }
            }

            PlasmaComponents.Label {
                objectName: "addressValidationLabel"
                Kirigami.FormData.label: ""
                visible: candidateAddress.text.trim().length > 0 && !root.addressValid
                text: KI18n.i18nc("@info", "Enter four numbers between 0 and 255, separated by dots.")
            }
        }

        Kirigami.InlineMessage {
            objectName: "probeErrorMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: root.probeError.length > 0
            text: KI18n.i18nc("@info", "That address did not answer: %1", root.probeError)
        }

        Kirigami.FormLayout {
            objectName: "probeResult"

            Layout.fillWidth: true
            visible: root.probedDevice !== null

            PlasmaComponents.Label {
                objectName: "probedName"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Device:")
                text: (root.probedDevice?.displayName ?? "").length > 0
                    ? root.probedDevice.displayName
                    : KI18n.i18nc("@info", "Unnamed Roku device")
            }
            PlasmaComponents.Label {
                objectName: "probedSerial"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Serial number:")
                text: root.probedDevice?.serial ?? ""
            }
            PlasmaComponents.Label {
                objectName: "probedPower"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Power:")
                text: root.probedDevice?.power ?? KI18n.i18nc("@info", "Unknown")
            }
            PlasmaComponents.Label {
                objectName: "probedVolume"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Volume:")
                text: root.probedDevice?.volume !== undefined
                    ? String(root.probedDevice.volume)
                    : KI18n.i18nc("@info", "Unavailable")
            }
            PlasmaComponents.Label {
                objectName: "probedCapabilities"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Supports:")
                text: (root.probedDevice?.capabilities ?? []).join(KI18n.i18nc("@item separator", ", "))
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        PlasmaExtras.Heading {
            level: 3
            text: KI18n.i18nc("@title:group", "Configured devices")
        }

        PlasmaExtras.PlaceholderMessage {
            objectName: "noDevicesMessage"

            Layout.fillWidth: true
            visible: deviceRepeater.count === 0
            iconName: "video-television-off"
            text: KI18n.i18nc("@info", "No Roku devices yet")
            explanation: KI18n.i18nc("@info", "Search the network or type an address above.")
        }

        ColumnLayout {
            id: deviceColumn

            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Repeater {
                id: deviceRepeater

                model: root.bridge ? root.bridge.devices : null

                delegate: ConfigDeviceEntry {
                    required property int index
                    required property var model

                    Layout.fillWidth: true
                    objectName: "deviceEntry-" + model.serial
                    serial: model.serial
                    displayName: model.name
                    address: model.address
                    allowAutomations: model.allowAutomations
                    wakeOnWlan: model.wakeOnWlan
                    refreshPolicy: model.refreshPolicy
                    capabilities: model.capabilities
                    shownInWidget: root.isSelected(model.serial)
                    isDefault: root.cfg_defaultSerial === model.serial
                    canMoveUp: index > 0
                    canMoveDown: index < deviceRepeater.count - 1
                    enabled: root.daemonReachable

                    onShownInWidgetToggled: selected => root.setSelected(serial, selected)
                    onMadeDefault: root.cfg_defaultSerial = serial
                    onRenamed: name => root.bridge.updateDevice(serial, { displayName: name })
                    onAutomationsToggled: allowed =>
                        root.bridge.updateDevice(serial, { allowAutomations: allowed })
                    onWakeToggled: wake => root.bridge.updateDevice(serial, { wakeOnWlan: wake })
                    onRefreshPolicyChosen: policy =>
                        root.bridge.updateDevice(serial, { refreshPolicy: policy })
                    onIdentifyRequested: root.bridge.sendCommand(serial, "keypress", "FindRemote")
                    onRemoveRequested: {
                        root.setSelected(serial, false);
                        root.bridge.removeDevice(serial);
                    }
                    onMoveUpRequested: root.move(serial, -1)
                    onMoveDownRequested: root.move(serial, 1)
                }
            }
        }
    }
}

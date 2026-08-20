// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.core as PlasmaCore

PlasmaComponents.ToolButton {
    id: root

    required property var bridge
    required property int formFactor
    required property int location
    property var appletInterface: null
    property bool automationActive: false

    property var selectedDevice: null

    readonly property bool inPanel: [
        PlasmaCore.Types.TopEdge,
        PlasmaCore.Types.RightEdge,
        PlasmaCore.Types.BottomEdge,
        PlasmaCore.Types.LeftEdge
    ].includes(location)
    readonly property string layoutMode: inPanel
        ? (formFactor === PlasmaCore.Types.Vertical ? "verticalPanel" : "horizontalPanel")
        : "desktop"
    readonly property string selectedName: selectedDevice && selectedDevice.name.length > 0
        ? selectedDevice.name
        : KI18n.i18n("Unnamed Roku device")
    readonly property string primaryStatus: statusBadge.statusText
    readonly property string stateDescription: {
        const parts = [statusBadge.Accessible.description];
        if (selectedDevice?.muted ?? false) {
            parts.push(KI18n.i18n("Muted"));
        }
        if (automationActive) {
            parts.push(KI18n.i18n("An automation is active"));
        }
        return parts.join(KI18n.i18n("; "));
    }

    Layout.minimumWidth: layoutMode === "verticalPanel"
        ? Kirigami.Units.iconSizes.smallMedium
        : Kirigami.Units.gridUnit * 6
    Layout.minimumHeight: Kirigami.Units.iconSizes.smallMedium
    Layout.preferredWidth: layoutMode === "horizontalPanel"
        ? contentLayout.implicitWidth + leftPadding + rightPadding
        : -1
    Layout.preferredHeight: layoutMode === "verticalPanel"
        ? contentLayout.implicitHeight + topPadding + bottomPadding
        : -1

    activeFocusOnTab: true
    hoverEnabled: true
    text: selectedDevice ? KI18n.i18n("Open RokuDeck controls for %1", selectedName)
                         : KI18n.i18n("Open RokuDeck controls")
    Accessible.name: text
    Accessible.description: selectedDevice
        ? KI18n.i18n("%1: %2", selectedName, stateDescription)
        : KI18n.i18n("No Roku device is selected")
    onClicked: {
        if (appletInterface) {
            appletInterface.expanded = !appletInterface.expanded;
        }
    }

    Instantiator {
        model: root.bridge ? root.bridge.devices : null

        delegate: QtObject {
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
                }
            }
            Component.onCompleted: {
                if (selected) {
                    root.selectedDevice = this;
                }
            }
        }
    }

    contentItem: GridLayout {
        id: contentLayout

        columns: root.layoutMode === "horizontalPanel" ? 5 : 1
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
            Layout.preferredHeight: width
            source: root.selectedDevice ? "video-television" : "video-television-off"
        }

        PlasmaComponents.Label {
            objectName: "deviceNameLabel"

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: root.layoutMode !== "verticalPanel"
            Layout.maximumWidth: Kirigami.Units.gridUnit * 9
            visible: root.layoutMode !== "verticalPanel"
            elide: Text.ElideRight
            text: root.selectedDevice ? root.selectedName : KI18n.i18n("No device")
        }

        StatusBadge {
            id: statusBadge

            objectName: "statusBadge"
            Layout.alignment: Qt.AlignCenter
            health: root.selectedDevice?.health ?? "offline"
            power: root.selectedDevice?.power ?? "unknown"
            stale: root.selectedDevice?.stale ?? true
            showText: root.layoutMode !== "verticalPanel"
        }

        Kirigami.Icon {
            objectName: "mutedIndicator"

            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: width
            visible: root.selectedDevice?.muted ?? false
            source: "audio-volume-muted"
            Accessible.name: KI18n.i18n("Muted")
            Accessible.description: KI18n.i18n("The selected Roku device is muted")
        }

        Kirigami.Icon {
            objectName: "automationIndicator"

            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: width
            visible: root.automationActive
            source: "system-run"
            Accessible.name: KI18n.i18n("Automation active")
            Accessible.description: KI18n.i18n("An automation is controlling the selected Roku device")
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    // Task 4 will bind this to the per-instance automation model. Keeping the
    // default false prevents a widget from claiming automation is active before
    // that model exists.
    property bool automationActive: false
    readonly property var bridge: Plasmoid
    readonly property var selectedSerials: Plasmoid.configuration.selectedSerials
    readonly property var visibleInformation: Plasmoid.configuration.visibleInformation
    readonly property var visibleControls: Plasmoid.configuration.visibleControls
    readonly property string compactMode: Plasmoid.configuration.compactMode

    switchWidth: Kirigami.Units.gridUnit * 32
    switchHeight: Kirigami.Units.gridUnit * 22

    toolTipMainText: KI18n.i18n("RokuDeck")
    toolTipSubText: root.bridge.available
        ? KI18n.i18n("Control your selected Roku device")
        : KI18n.i18n("RokuDeck daemon is unavailable")

    Plasmoid.icon: "video-television"
    Plasmoid.status: root.bridge.available
        ? PlasmaCore.Types.ActiveStatus
        : PlasmaCore.Types.NeedsAttentionStatus

    compactRepresentation: CompactRepresentation {
        appletInterface: root
        automationActive: root.automationActive
        bridge: root.bridge
        compactMode: root.compactMode
        selectedSerials: root.selectedSerials
        formFactor: Plasmoid.formFactor
        location: Plasmoid.location
    }

    fullRepresentation: FullRepresentation {
        bridge: root.bridge
        narrow: width < Kirigami.Units.gridUnit * 32
        selectedSerials: root.selectedSerials
        visibleInformation: root.visibleInformation
        visibleControls: root.visibleControls
    }

    PlasmaCore.Action {
        id: refreshAction

        text: KI18n.i18n("Refresh Roku devices")
        icon.name: "view-refresh"
        onTriggered: root.bridge.refresh()
    }

    PlasmaCore.Action {
        id: settingsAction

        readonly property var internalConfigureAction: Plasmoid.internalAction("configure")

        text: KI18n.i18n("Configure RokuDeck…")
        icon.name: "configure"
        enabled: internalConfigureAction !== null
        onTriggered: {
            if (internalConfigureAction) {
                internalConfigureAction.trigger();
            }
        }
    }

    Plasmoid.contextualActions: [refreshAction, settingsAction]

    // The configured device is restored once the daemon has told us the device
    // exists; the bridge rejects a serial it does not know, so applying this on
    // every list change is what makes the choice survive a restart.
    function applyDefaultSerial() {
        const preferred = Plasmoid.configuration.defaultSerial;
        if (preferred.length > 0) {
            root.bridge.selectedSerial = preferred;
        }
    }

    Connections {
        target: root.bridge.devices

        function onModelReset() {
            root.applyDefaultSerial();
        }

        function onRowsInserted() {
            root.applyDefaultSerial();
        }
    }

    Component.onCompleted: root.applyDefaultSerial()
}

// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtTest

import org.kde.plasma.core as PlasmaCore

import "../../src/plasmoid/qml" as RokuDeck

TestCase {
    id: testCase

    width: 480
    height: 320
    name: "RokuDeckCompactRepresentation"
    when: windowShown

    Component {
        id: bridgeComponent

        FixtureBridge {}
    }

    Component {
        id: compactComponent

        RokuDeck.CompactRepresentation {}
    }

    Component {
        id: windowComponent

        Window {
            width: 480
            height: 320
            visible: true
        }
    }

    function createCompact(device, properties) {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const bridge = createTemporaryObject(bridgeComponent, testCase);
        verify(bridge);
        bridge.setFixtureDevices(Array.isArray(device) ? device : [device]);
        const viewProperties = Object.assign({
            bridge: bridge,
            formFactor: PlasmaCore.Types.Horizontal,
            location: PlasmaCore.Types.BottomEdge,
            width: 260,
            height: 40,
            visible: true
        }, properties ?? {});
        const compact = createTemporaryObject(compactComponent, window.contentItem, viewProperties);
        verify(compact);
        tryVerify(() => findChild(compact, "statusBadge") !== null);
        return compact;
    }

    function test_adaptsAcrossPanelAndDesktopForms() {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power"]
        });

        compare(compact.layoutMode, "horizontalPanel");
        const nameLabel = findChild(compact, "deviceNameLabel");
        verify(nameLabel.visible,
               `root=${compact.visible}, content=${nameLabel.parent.visible}, mode=${compact.layoutMode}`);

        compact.formFactor = PlasmaCore.Types.Vertical;
        compact.location = PlasmaCore.Types.LeftEdge;
        compact.width = 40;
        compact.height = 180;
        compare(compact.layoutMode, "verticalPanel");
        verify(!findChild(compact, "deviceNameLabel").visible);

        compact.formFactor = PlasmaCore.Types.Planar;
        compact.location = PlasmaCore.Types.Desktop;
        compact.width = 240;
        compact.height = 160;
        compare(compact.layoutMode, "desktop");
        verify(findChild(compact, "deviceNameLabel").visible);
    }

    function test_primaryStatus_data() {
        return [
            { tag: "online", health: "online", power: "on", stale: false, expected: "online", error: false },
            { tag: "standby", health: "standby", power: "standby", stale: false, expected: "standby", error: false },
            { tag: "degraded", health: "degraded", power: "on", stale: false, expected: "degraded", error: true },
            { tag: "unknown-is-degraded", health: "unknown", power: "unknown", stale: false, expected: "degraded", error: true },
            { tag: "offline", health: "offline", power: "unknown", stale: false, expected: "offline", error: true },
            { tag: "stale-overrides-online", health: "online", power: "on", stale: true, expected: "stale", error: true }
        ];
    }

    function test_primaryStatus(data) {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "Living Room",
            health: data.health,
            power: data.power,
            stale: data.stale
        });
        const badge = findChild(compact, "statusBadge");
        compare(badge.statusKey, data.expected);
        compare(badge.errorState, data.error);
        verify(badge.Accessible.name.length > 0);
        verify(badge.Accessible.description.length > 0);
    }

    function test_mutedAndAutomationStatesAreVisible() {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "Living Room",
            muted: true
        }, { automationActive: true });

        const mutedIndicator = findChild(compact, "mutedIndicator");
        verify(mutedIndicator.visible,
               `root=${compact.visible}, parent=${mutedIndicator.parent.visible}, muted=${compact.selectedDevice?.muted}`);
        verify(findChild(compact, "automationIndicator").visible);
        verify(compact.Accessible.description.includes("Muted"));
        verify(compact.Accessible.description.includes("automation"));
    }

    function test_emptyDisplayNameUsesLocalizedFallback() {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: ""
        });

        const nameLabel = findChild(compact, "deviceNameLabel");
        verify(nameLabel.text.length > 0);
        verify(!nameLabel.text.includes("SERIAL-A"));
    }

    function test_compactModeOverridesTheAdaptiveLayout() {
        const compact = createCompact({ serial: "SERIAL-A", name: "Living Room" });
        compact.location = PlasmaCore.Types.BottomEdge;
        compact.formFactor = PlasmaCore.Types.Horizontal;
        compare(compact.layoutMode, "horizontalPanel");
        verify(findChild(compact, "deviceNameLabel").visible);

        compact.compactMode = "iconOnly";
        compare(compact.layoutMode, "verticalPanel");
        verify(!findChild(compact, "deviceNameLabel").visible);

        compact.compactMode = "iconAndName";
        compact.formFactor = PlasmaCore.Types.Vertical;
        verify(compact.layoutMode !== "verticalPanel",
               "asking for the name must keep it even in a vertical panel");
        verify(findChild(compact, "deviceNameLabel").visible);

        compact.compactMode = "adaptive";
        compare(compact.layoutMode, "verticalPanel");
    }

    function test_onlyTheChosenDeviceDrivesTheCompactWidget() {
        const compact = createCompact([
            { serial: "SERIAL-A", name: "Living Room" },
            { serial: "SERIAL-B", name: "Bedroom" }
        ]);
        tryVerify(() => compact.selectedDevice !== null);
        compare(compact.selectedName, "Living Room");

        compact.selectedSerials = ["SERIAL-B"];
        tryVerify(() => compact.selectedName === "Bedroom");
        verify(compact.bridge.selectedSerial === "SERIAL-B",
               "hiding the selected device must fall through to one still shown");
    }
}

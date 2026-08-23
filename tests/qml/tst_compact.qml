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
        tryVerify(() => findChild(compact, "tvStatusIcon") !== null);
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
        // Even the tall narrow form names its device, in small text.
        verify(findChild(compact, "deviceNameLabel").visible);

        compact.formFactor = PlasmaCore.Types.Planar;
        compact.location = PlasmaCore.Types.Desktop;
        compact.width = 240;
        compact.height = 160;
        compare(compact.layoutMode, "desktop");
        verify(findChild(compact, "deviceNameLabel").visible);
    }

    function test_panelScreensPaintDeviceState_data() {
        return [
            { tag: "online", health: "online", power: "on", stale: false, color: "#26a269" },
            { tag: "standby", health: "standby", power: "standby", stale: false, color: "#5d7285" },
            { tag: "degraded", health: "degraded", power: "on", stale: false, color: "#e9ad0c" },
            { tag: "unknown-is-degraded", health: "unknown", power: "unknown", stale: false, color: "#e9ad0c" },
            { tag: "stale-overrides-online", health: "online", power: "on", stale: true, color: "#e9ad0c" },
            { tag: "offline", health: "offline", power: "unknown", stale: false, color: "#c01c28" }
        ];
    }

    function test_panelScreensPaintDeviceState(data) {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "Living Room",
            health: data.health,
            power: data.power,
            stale: data.stale,
            capabilities: ["power"]
        });
        compare(compact.layoutMode, "horizontalPanel");

        // The painted screen alone carries the state; no separate connection
        // or warning icons exist anywhere anymore.
        const screen = findChild(compact, "tvScreen");
        verify(screen !== null, "the television has no paintable screen");
        compare(String(screen.color), String(data.color));

        compact.formFactor = PlasmaCore.Types.Vertical;
        compact.location = PlasmaCore.Types.LeftEdge;
        compare(compact.layoutMode, "verticalPanel");
        compare(String(findChild(compact, "tvScreen").color), String(data.color));
    }

    function test_desktopUsesThePaintedTelevisionToo() {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "Living Room",
            health: "online",
            capabilities: ["power"]
        }, {
            formFactor: PlasmaCore.Types.Planar,
            location: PlasmaCore.Types.Desktop
        });
        compare(compact.layoutMode, "desktop");
        verify(findChild(compact, "tvStatusIcon").visible);
        compare(String(findChild(compact, "tvScreen").color), String("#26a269"));
        verify(findChild(compact, "deviceNameLabel").visible);
    }

    function test_verticalPanelNamesItsDeviceInSmallText() {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power"]
        });
        compact.formFactor = PlasmaCore.Types.Vertical;
        compact.location = PlasmaCore.Types.LeftEdge;
        compare(compact.layoutMode, "verticalPanel");

        const nameLabel = findChild(compact, "deviceNameLabel");
        verify(nameLabel.visible);
        compare(nameLabel.text, "Living Room");
        // Must genuinely be smaller than the default, not silently fall back
        // to it through a failed binding.
        verify(nameLabel.font.pointSize > 0
               && nameLabel.font.pointSize < Qt.application.font.pointSize,
               "vertical panel name must be small text");
        verify(nameLabel.width <= compact.width + 1,
               "the name must fit the narrow vertical form");
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
        const tv = findChild(compact, "tvStatusIcon");
        compare(tv.statusKey, data.expected);
        verify(tv.Accessible.name.length > 0);
        verify(tv.Accessible.description.length > 0);
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
        verify(!findChild(compact, "deviceNameLabel").visible,
               "icon only must hide the name");

        compact.compactMode = "adaptive";
        compact.formFactor = PlasmaCore.Types.Vertical;
        compact.location = PlasmaCore.Types.LeftEdge;
        compare(compact.layoutMode, "verticalPanel");
        verify(findChild(compact, "deviceNameLabel").visible);

        compact.compactMode = "iconOnly";
        verify(!findChild(compact, "deviceNameLabel").visible);

        compact.compactMode = "iconAndName";
        compare(compact.layoutMode, "verticalPanel",
                "asking for the name must not leave the vertical form");
        verify(findChild(compact, "deviceNameLabel").visible);
    }

    function test_verticalPanelWrapsLongDeviceNames() {
        const compact = createCompact({
            serial: "SERIAL-A",
            name: "A Very Long Living Room Roku Television Name",
            capabilities: ["power"]
        });
        compact.formFactor = PlasmaCore.Types.Vertical;
        compact.location = PlasmaCore.Types.LeftEdge;
        compact.width = 44;
        compare(compact.layoutMode, "verticalPanel");

        const nameLabel = findChild(compact, "deviceNameLabel");
        verify(nameLabel.visible);
        tryVerify(() => nameLabel.lineCount > 1, 5000,
                  "a name wider than the panel must wrap instead of clipping");
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

// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtTest

import "../../src/plasmoid/qml" as RokuDeck

TestCase {
    id: testCase

    width: 720
    height: 560
    name: "RokuDeckFullRepresentation"
    when: windowShown

    Component {
        id: bridgeComponent

        FixtureBridge {}
    }

    Component {
        id: fullComponent

        RokuDeck.FullRepresentation {}
    }

    Component {
        id: windowComponent

        Window {
            width: 720
            height: 560
            visible: true
        }
    }

    function createFull(entries, properties, selectedSerial) {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const bridge = createTemporaryObject(bridgeComponent, testCase);
        verify(bridge);
        bridge.setFixtureDevices(entries);
        if (selectedSerial !== undefined) {
            bridge.selectedSerial = selectedSerial;
        }
        const full = createTemporaryObject(fullComponent, window.contentItem, Object.assign({
            bridge: bridge,
            narrow: false,
            width: 700,
            height: 520,
            visible: true
        }, properties ?? {}));
        verify(full);
        tryVerify(() => findChild(full, "selectedDeviceDetail") !== null);
        return { bridge: bridge, full: full };
    }

    function test_masterDetailStacksAtNarrowWidths() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power"]
        }]);
        const layout = findChild(fixture.full, "masterDetailLayout");
        compare(layout.columns, 2);

        fixture.full.narrow = true;
        compare(layout.columns, 1);
    }

    function test_volumeAndPowerControlsFollowCapabilities() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power"]
        }]);
        verify(findChild(fixture.full, "powerControls").visible);
        verify(!findChild(fixture.full, "volumeControls").visible);

        fixture.bridge.devices.setFixtureProperty(0, "capabilities", ["volume"]);
        tryVerify(() => findChild(fixture.full, "volumeControls").visible);
        verify(!findChild(fixture.full, "powerControls").visible);
    }

    function test_disabledOfflineControlDoesNotDispatch() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            health: "offline",
            power: "unknown",
            capabilities: ["power"]
        }]);
        const powerButton = findChild(fixture.full, "powerButton");
        verify(!powerButton.enabled);
        mouseClick(powerButton);
        compare(fixture.bridge.sentCommands.length, 0);
    }

    function test_standbyAllowsPowerButDisablesPrimaryControls() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            health: "standby",
            power: "standby",
            capabilities: ["power", "navigation"]
        }]);
        const powerButton = findChild(fixture.full, "powerButton");
        const homeButton = findChild(fixture.full, "homeButton");
        verify(powerButton.visible);
        verify(powerButton.enabled);
        verify(homeButton.visible);
        verify(!homeButton.enabled);
    }

    function test_controlDispatchAndPendingConfirmationState() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            health: "online",
            power: "on",
            capabilities: ["power", "volume"]
        }]);
        const powerButton = findChild(fixture.full, "powerButton");
        verify(powerButton.enabled);
        verify(powerButton.Accessible.name.length > 0);
        powerButton.forceActiveFocus();
        tryVerify(() => powerButton.activeFocus);
        keyClick(Qt.Key_Space);
        compare(fixture.bridge.sentCommands.length, 1);
        compare(fixture.bridge.sentCommands[0].serial, "SERIAL-A");
        compare(fixture.bridge.sentCommands[0].action, "power");
        compare(fixture.bridge.sentCommands[0].value, false);

        fixture.bridge.devices.setFixtureProperty(0, "pendingCommand", true);
        const pending = findChild(fixture.full, "pendingIndicator");
        tryVerify(() => pending.running);
        verify(!powerButton.enabled);

        fixture.bridge.devices.setFixtureProperty(0, "pendingCommand", false);
        tryVerify(() => !pending.running);
        verify(powerButton.enabled);
    }

    function test_keyboardSelectsDeviceThenReachesPrimaryControl() {
        const fixture = createFull([
            {
                serial: "SERIAL-A",
                name: "Living Room",
                capabilities: ["power"]
            },
            {
                serial: "SERIAL-B",
                name: "Bedroom",
                capabilities: ["power"]
            }
        ]);
        const deviceList = findChild(fixture.full, "deviceList");
        deviceList.forceActiveFocus();
        tryVerify(() => deviceList.activeFocus);

        keyClick(Qt.Key_Down);
        compare(deviceList.currentIndex, 1);
        compare(fixture.bridge.selectedSerial, "SERIAL-B");

        keyClick(Qt.Key_Tab);
        const powerButton = findChild(fixture.full, "powerButton");
        tryVerify(() => powerButton.activeFocus);
    }

    function test_allPrimaryIconButtonsExposeAccessibleText() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power", "volume", "mute", "navigation", "playback"]
        }]);
        const names = [
            "powerButton", "volumeDownButton", "volumeUpButton", "muteButton",
            "upButton", "leftButton", "selectButton", "rightButton", "downButton",
            "backButton", "homeButton", "playPauseButton"
        ];
        for (const name of names) {
            const button = findChild(fixture.full, name);
            verify(button !== null, `Missing ${name}`);
            verify(button.Accessible.name.length > 0, `${name} has no accessible name`);
            verify(button.Accessible.description.length > 0,
                   `${name} has no accessible description`);
        }
    }

    function test_unknownScreensaverIsNotReportedAsInactive() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            screensaver: "unknown",
            capabilities: ["power"]
        }]);
        const label = findChild(fixture.full, "screensaverValue");
        verify(label !== null);
        verify(label.text !== "Inactive", `unknown screensaver rendered as ${label.text}`);
        verify(label.text.length > 0);

        fixture.bridge.devices.setFixtureProperty(0, "screensaver", "active");
        tryVerify(() => label.text === "Active");

        fixture.bridge.devices.setFixtureProperty(0, "screensaver", "inactive");
        tryVerify(() => label.text === "Inactive");
    }

    function test_deviceListFollowsTheExistingSelectionInsteadOfTheFirstRow() {
        const fixture = createFull([
            {
                serial: "SERIAL-A",
                name: "Living Room",
                capabilities: ["power"]
            },
            {
                serial: "SERIAL-B",
                name: "Bedroom",
                capabilities: ["power"]
            }
        ], {}, "SERIAL-B");

        const deviceList = findChild(fixture.full, "deviceList");
        compare(fixture.bridge.selectedSerial, "SERIAL-B");
        tryCompare(deviceList, "currentIndex", 1);
        compare(fixture.bridge.selectedSerial, "SERIAL-B");
    }

    function test_onlyTheChosenDevicesAppearInTheWidget() {
        const fixture = createFull([
            { serial: "SERIAL-A", name: "Living Room", capabilities: ["power"] },
            { serial: "SERIAL-B", name: "Bedroom", capabilities: ["power"] },
            { serial: "SERIAL-C", name: "Office", capabilities: ["power"] }
        ]);
        const deviceList = findChild(fixture.full, "deviceList");
        compare(deviceList.count, 3, "an empty choice must show every configured device");

        fixture.full.selectedSerials = ["SERIAL-B", "SERIAL-C"];
        tryCompare(deviceList, "count", 2);

        fixture.full.selectedSerials = ["SERIAL-C"];
        tryCompare(deviceList, "count", 1);

        fixture.full.selectedSerials = [];
        tryCompare(deviceList, "count", 3);
    }

    function test_aHiddenControlStaysHiddenEvenWhenTheDeviceSupportsIt() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power", "volume", "mute", "navigation", "playback"]
        }]);
        verify(findChild(fixture.full, "volumeControls").visible);
        verify(findChild(fixture.full, "muteButton").visible);

        fixture.full.visibleControls = ["power"];
        tryVerify(() => !findChild(fixture.full, "volumeControls").visible);
        verify(!findChild(fixture.full, "muteButton").visible);
        verify(findChild(fixture.full, "powerControls").visible);

        fixture.full.visibleControls = ["power", "volume", "mute", "navigation", "playback"];
        tryVerify(() => findChild(fixture.full, "volumeControls").visible);
    }

    function test_hiddenInformationRowsDisappearFromTheDetailPane() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            capabilities: ["power"]
        }]);
        verify(findChild(fixture.full, "screensaverValue").visible);
        verify(findChild(fixture.full, "volumeValue").visible);
        verify(findChild(fixture.full, "detailStatusBadge").visible);
        verify(!findChild(fixture.full, "addressValue").visible,
               "the address is off by default");

        fixture.full.visibleInformation = ["address"];
        tryVerify(() => findChild(fixture.full, "addressValue").visible);
        verify(!findChild(fixture.full, "screensaverValue").visible);
        verify(!findChild(fixture.full, "volumeValue").visible);
        verify(!findChild(fixture.full, "activeAppValue").visible);
        verify(!findChild(fixture.full, "detailStatusBadge").visible);
    }
}

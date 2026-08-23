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
        // The wake action reuses the one power glyph every theme ships; a
        // missing themed icon would render as an anonymous file placeholder.
        compare(powerButton.icon.name, "system-shutdown");
        verify(homeButton.visible);
        verify(!homeButton.enabled);

        // Waking a standby device is a plain ECP keypress too.
        powerButton.forceActiveFocus();
        tryVerify(() => powerButton.activeFocus);
        keyClick(Qt.Key_Space);
        compare(fixture.bridge.sentCommands.length, 1);
        compare(fixture.bridge.sentCommands[0].serial, "SERIAL-A");
        compare(fixture.bridge.sentCommands[0].action, "keypress");
        compare(fixture.bridge.sentCommands[0].value, "PowerOn");
    }

    function test_pendingCommandKeepsControlsUsableAndFlashesThePressedButton() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            health: "online",
            power: "on",
            capabilities: ["power", "volume", "mute"]
        }]);
        const powerButton = findChild(fixture.full, "powerButton");
        verify(powerButton.enabled);
        compare(powerButton.icon.name, "system-shutdown");
        verify(powerButton.Accessible.name.length > 0);
        powerButton.forceActiveFocus();
        tryVerify(() => powerButton.activeFocus);
        keyClick(Qt.Key_Space);
        compare(fixture.bridge.sentCommands.length, 1);
        compare(fixture.bridge.sentCommands[0].serial, "SERIAL-A");
        compare(fixture.bridge.sentCommands[0].action, "keypress");
        compare(fixture.bridge.sentCommands[0].value, "Power");

        function press(buttonName) {
            const button = findChild(fixture.full, buttonName);
            verify(button !== null, `Missing ${buttonName}`);
            verify(button.enabled, `${buttonName} is disabled`);
            button.forceActiveFocus();
            tryVerify(() => button.activeFocus);
            keyClick(Qt.Key_Space);
        }

        press("volumeUpButton");
        compare(fixture.bridge.sentCommands.length, 2);
        compare(fixture.bridge.sentCommands[1].action, "keypress");
        compare(fixture.bridge.sentCommands[1].value, "VolumeUp");

        // Only the pressed control shows activity feedback.
        const volumeUp = findChild(fixture.full, "volumeUpButton");
        const volumeDown = findChild(fixture.full, "volumeDownButton");
        verify(volumeUp.flash);
        verify(!volumeDown.flash);

        press("volumeDownButton");
        compare(fixture.bridge.sentCommands.length, 3);
        compare(fixture.bridge.sentCommands[2].action, "keypress");
        compare(fixture.bridge.sentCommands[2].value, "VolumeDown");

        press("muteButton");
        compare(fixture.bridge.sentCommands.length, 4);
        compare(fixture.bridge.sentCommands[3].action, "keypress");
        compare(fixture.bridge.sentCommands[3].value, "VolumeMute");

        // A command in flight must not lock the panel; only a refreshed
        // snapshot that reports an unusable device may do that.
        fixture.bridge.devices.setFixtureProperty(0, "pendingCommand", true);
        verify(powerButton.enabled);
        verify(volumeUp.enabled);
        fixture.bridge.devices.setFixtureProperty(0, "pendingCommand", false);
        verify(powerButton.enabled);

        // The press animation is transient.
        tryVerify(() => !findChild(fixture.full, "muteButton").flash);
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

    function test_pendingCommandDoesNotShiftSiblingControls() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            health: "online",
            power: "on",
            capabilities: ["power", "volume", "navigation", "playback"]
        }]);
        const down = findChild(fixture.full, "downButton");
        verify(down !== null);
        const indicator = findChild(fixture.full, "pendingIndicator");
        verify(indicator !== null);
        const headerRow = indicator.parent;

        const yOf = (item) => item.mapToItem(fixture.full, 0, 0).y;
        const controls = indicator.parent.parent;
        // Give the initial polish a moment, then require the settled layout:
        // a collapsed start that snaps into place later reads as bouncing.
        wait(100);
        const restY = yOf(down);
        const headerHeight = headerRow.height;
        verify(headerHeight > 0);
        compare(controls.height, controls.implicitHeight);

        fixture.bridge.devices.setFixtureProperty(0, "pendingCommand", true);
        tryVerify(() => indicator.running);
        // Let the layout settle so a real reflow is measured, not a stale frame.
        wait(100);

        compare(yOf(down), restY);
        compare(headerRow.height, headerHeight);

        fixture.bridge.devices.setFixtureProperty(0, "pendingCommand", false);
        tryVerify(() => !indicator.running);
        wait(100);

        compare(yOf(down), restY);
        compare(headerRow.height, headerHeight);
    }

    function test_muteButtonIconShowsTheCurrentState() {
        const fixture = createFull([{
            serial: "SERIAL-A",
            name: "Living Room",
            health: "online",
            power: "on",
            muted: true,
            capabilities: ["power", "volume", "mute"]
        }]);
        const muteButton = findChild(fixture.full, "muteButton");
        compare(muteButton.icon.name, "audio-volume-muted",
                "a muted television must show the struck-through speaker");

        fixture.bridge.devices.setFixtureProperty(0, "muted", false);
        tryVerify(() => muteButton.icon.name === "audio-volume-high",
                  5000, "an unmuted television must show the plain speaker");
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
        verify(findChild(fixture.full, "detailStatusTv").visible);
        verify(!findChild(fixture.full, "addressValue").visible,
               "the address is off by default");

        fixture.full.visibleInformation = ["address"];
        tryVerify(() => findChild(fixture.full, "addressValue").visible);
        verify(!findChild(fixture.full, "screensaverValue").visible);
        verify(!findChild(fixture.full, "volumeValue").visible);
        verify(!findChild(fixture.full, "activeAppValue").visible);
        verify(!findChild(fixture.full, "detailStatusTv").visible);
    }
}

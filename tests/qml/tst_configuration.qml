// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtTest

import "../../src/plasmoid/qml" as RokuDeckConfig
import "../../src/plasmoid/qml/address.js" as RokuDeckAddress

TestCase {
    id: testCase

    width: 720
    height: 640
    name: "RokuDeckConfiguration"
    when: windowShown

    Component {
        id: bridgeComponent

        FixtureBridge {}
    }

    Component {
        id: windowComponent

        Window {
            width: 720
            height: 640
            visible: true
        }
    }

    Component {
        id: devicesComponent

        RokuDeckConfig.ConfigDevices {}
    }

    Component {
        id: displayComponent

        RokuDeckConfig.ConfigDisplay {}
    }

    Component {
        id: controlsComponent

        RokuDeckConfig.ConfigControls {}
    }

    Component {
        id: peersComponent

        RokuDeckConfig.ConfigPeers {}
    }

    Component {
        id: peerControlsComponent

        RokuDeckConfig.PeerAudioControls {}
    }

    function createPage(component, properties) {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const page = createTemporaryObject(component, window.contentItem, Object.assign({
            width: 700,
            height: 620,
            visible: true
        }, properties ?? {}));
        verify(page);
        return page;
    }

    function createDevices(entries, properties) {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const bridge = createTemporaryObject(bridgeComponent, testCase);
        verify(bridge);
        bridge.setFixtureDevices(entries);
        const page = createTemporaryObject(devicesComponent, window.contentItem, Object.assign({
            bridge: bridge,
            width: 700,
            height: 620,
            visible: true
        }, properties ?? {}));
        verify(page);
        return { bridge: bridge, page: page };
    }

    function test_addressValidationMatchesTheDaemonContract_data() {
        return [
            { tag: "typical", address: "192.0.2.20", valid: true },
            { tag: "documentation", address: "198.51.100.10", valid: true },
            { tag: "short", address: "10.1.2", valid: false },
            { tag: "out-of-range", address: "999.1.1.1", valid: false },
            { tag: "leading-zero", address: "010.1.1.1", valid: false },
            { tag: "loopback", address: "127.0.0.1", valid: false },
            { tag: "multicast", address: "224.0.0.1", valid: false },
            { tag: "unspecified", address: "0.0.0.0", valid: false },
            { tag: "broadcast", address: "255.255.255.255", valid: false },
            { tag: "ipv6", address: "::1", valid: false },
            { tag: "hostname", address: "roku-on-the-lan", valid: false },
            { tag: "empty", address: "", valid: false }
        ];
    }

    function test_addressValidationMatchesTheDaemonContract(data) {
        compare(RokuDeckAddress.isValidIpv4(data.address), data.valid);
    }

    function test_aDeviceIsOnlyAddedAfterItAnswers() {
        const fixture = createDevices([]);
        const address = findChild(fixture.page, "candidateAddress");
        const test = findChild(fixture.page, "testConnectionButton");
        const add = findChild(fixture.page, "addDeviceButton");

        verify(!test.enabled, "an empty address must not be testable");
        verify(!add.enabled);

        address.text = "10.1.2";
        verify(!test.enabled, "a malformed address must not be testable");
        verify(findChild(fixture.page, "addressValidationLabel").visible);

        address.text = "198.51.100.44";
        verify(test.enabled);
        verify(!add.enabled, "adding must wait until the device answers");

        mouseClick(test);
        compare(fixture.bridge.testedAddresses, ["198.51.100.44"]);

        fixture.bridge.finishDeviceTest("198.51.100.44", {
            serial: "SERIAL-NEW",
            displayName: "Kitchen",
            address: "198.51.100.44",
            power: "on",
            volume: 18,
            capabilities: ["power", "volume"]
        }, "");
        tryVerify(() => add.enabled);
        compare(findChild(fixture.page, "probedSerial").text, "SERIAL-NEW");
        compare(findChild(fixture.page, "probedName").text, "Kitchen");
        verify(findChild(fixture.page, "probedCapabilities").text.includes("volume"));

        mouseClick(add);
        compare(fixture.bridge.addedDevices.length, 1);
        compare(fixture.bridge.addedDevices[0].serial, "SERIAL-NEW");
        compare(fixture.bridge.addedDevices[0].address, "198.51.100.44");
    }

    function test_aFailedProbeExplainsItselfAndBlocksAdding() {
        const fixture = createDevices([]);
        findChild(fixture.page, "candidateAddress").text = "198.51.100.44";
        mouseClick(findChild(fixture.page, "testConnectionButton"));
        fixture.bridge.finishDeviceTest("198.51.100.44", {}, "no answer from the device");

        const message = findChild(fixture.page, "probeErrorMessage");
        tryVerify(() => message.visible);
        verify(message.text.includes("no answer from the device"));
        verify(!findChild(fixture.page, "addDeviceButton").enabled);
    }

    function test_editingTheAddressInvalidatesAnEarlierProbe() {
        const fixture = createDevices([]);
        const address = findChild(fixture.page, "candidateAddress");
        address.text = "198.51.100.44";
        mouseClick(findChild(fixture.page, "testConnectionButton"));
        fixture.bridge.finishDeviceTest("198.51.100.44", {
            serial: "SERIAL-NEW",
            address: "198.51.100.44"
        }, "");
        const add = findChild(fixture.page, "addDeviceButton");
        tryVerify(() => add.enabled);

        address.text = "198.51.100.45";
        verify(!add.enabled, "a probe of a different address must not authorise this one");
    }

    function test_perWidgetSelectionAndDefaultAreKeptInConfiguration() {
        const fixture = createDevices([
            { serial: "SERIAL-A", name: "Living Room" },
            { serial: "SERIAL-B", name: "Bedroom" }
        ], { cfg_selectedSerials: [], cfg_defaultSerial: "" });

        const first = findChild(fixture.page, "deviceEntry-SERIAL-A");
        const second = findChild(fixture.page, "deviceEntry-SERIAL-B");
        verify(first !== null);
        verify(second !== null);

        mouseClick(findChild(first, "showInWidgetBox"));
        compare(fixture.page.cfg_selectedSerials, ["SERIAL-A"]);
        mouseClick(findChild(second, "showInWidgetBox"));
        compare(fixture.page.cfg_selectedSerials, ["SERIAL-A", "SERIAL-B"]);

        const defaultButton = findChild(second, "defaultDeviceButton");
        tryVerify(() => defaultButton.enabled);
        mouseClick(defaultButton);
        compare(fixture.page.cfg_defaultSerial, "SERIAL-B");

        mouseClick(findChild(second, "showInWidgetBox"));
        compare(fixture.page.cfg_selectedSerials, ["SERIAL-A"]);
        compare(fixture.page.cfg_defaultSerial, "SERIAL-A",
                "a device the widget no longer shows cannot stay the default");
    }

    function test_deviceSettingsGoStraightToTheDaemon() {
        const fixture = createDevices([
            { serial: "SERIAL-A", name: "Living Room", capabilities: ["power"] },
            { serial: "SERIAL-B", name: "Bedroom", capabilities: ["power"] }
        ], { cfg_selectedSerials: ["SERIAL-A"] });

        const entry = findChild(fixture.page, "deviceEntry-SERIAL-A");
        mouseClick(findChild(entry, "allowAutomationsBox"));
        compare(fixture.bridge.updatedDevices.length, 1);
        compare(fixture.bridge.updatedDevices[0].serial, "SERIAL-A");
        compare(fixture.bridge.updatedDevices[0].changes.allowAutomations, false);

        mouseClick(findChild(entry, "wakeOnWlanBox"));
        compare(fixture.bridge.updatedDevices[1].changes.wakeOnWlan, true);

        mouseClick(findChild(entry, "moveDownButton"));
        compare(fixture.bridge.orderedSerials, ["SERIAL-B", "SERIAL-A"]);

        mouseClick(findChild(entry, "removeButton"));
        compare(fixture.bridge.removedDevices, ["SERIAL-A"]);
        compare(fixture.page.cfg_selectedSerials, [],
                "removing a device must drop it from this widget too");
    }

    // A user toggle writes the control's property and kills a plain binding;
    // the control must keep following the record, or a rejected daemon edit
    // leaves the checkbox telling a different story than the stored device.
    function test_controlsKeepFollowingTheDeviceRecordAfterAClick() {
        const fixture = createDevices([
            { serial: "SERIAL-A", name: "Living Room", capabilities: ["power"] }
        ], { cfg_selectedSerials: ["SERIAL-A"] });
        const entry = findChild(fixture.page, "deviceEntry-SERIAL-A");
        const box = findChild(entry, "allowAutomationsBox");
        compare(box.checked, true);
        mouseClick(box);
        // The click asks the daemon for the change; the control keeps showing
        // the stored record until the daemon confirms it.
        compare(fixture.bridge.updatedDevices[0].changes.allowAutomations, false);
        compare(box.checked, true,
                "a control must not drift from the record on an unconfirmed change");

        // The daemon accepted and the refreshed record now says false.
        entry.allowAutomations = false;
        compare(box.checked, false,
                "the control must follow the record once it changes");
    }

    function test_identifyIsOfferedOnlyWhenTheDeviceSupportsIt() {
        const fixture = createDevices([
            { serial: "SERIAL-A", name: "Living Room", capabilities: ["power"] },
            {
                serial: "SERIAL-B",
                name: "Bedroom",
                capabilities: ["power", "supports-find-remote"]
            }
        ]);

        verify(!findChild(findChild(fixture.page, "deviceEntry-SERIAL-A"),
                          "identifyButton").enabled);
        const capable = findChild(findChild(fixture.page, "deviceEntry-SERIAL-B"),
                                  "identifyButton");
        verify(capable.enabled);
        mouseClick(capable);
        compare(fixture.bridge.sentCommands.length, 1);
        compare(fixture.bridge.sentCommands[0].serial, "SERIAL-B");
        compare(fixture.bridge.sentCommands[0].value, "FindRemote");
    }

    function test_theFirstAndLastDeviceCannotBeMovedOffTheList() {
        const fixture = createDevices([
            { serial: "SERIAL-A", name: "Living Room" },
            { serial: "SERIAL-B", name: "Bedroom" }
        ]);
        const first = findChild(fixture.page, "deviceEntry-SERIAL-A");
        const last = findChild(fixture.page, "deviceEntry-SERIAL-B");
        verify(!findChild(first, "moveUpButton").enabled);
        verify(findChild(first, "moveDownButton").enabled);
        verify(findChild(last, "moveUpButton").enabled);
        verify(!findChild(last, "moveDownButton").enabled);
    }

    function test_anUnreachableDaemonDisablesEveryDeviceChange() {
        const fixture = createDevices([{ serial: "SERIAL-A", name: "Living Room" }]);
        fixture.bridge.setFixtureAvailable(false);

        tryVerify(() => findChild(fixture.page, "daemonUnavailableMessage").visible);
        verify(!findChild(fixture.page, "discoverButton").enabled);
        findChild(fixture.page, "candidateAddress").text = "198.51.100.44";
        verify(!findChild(fixture.page, "testConnectionButton").enabled);
        verify(!findChild(fixture.page, "deviceEntry-SERIAL-A").enabled);
    }

    function test_aRejectedChangeIsShownToTheUser() {
        const fixture = createDevices([{ serial: "SERIAL-A", name: "Living Room" }]);
        const message = findChild(fixture.page, "operationErrorMessage");
        verify(!message.visible);

        fixture.bridge.failDeviceOperation("updateDevice", "device SERIAL-A is not configured");
        tryVerify(() => message.visible);
        verify(message.text.includes("device SERIAL-A is not configured"));
    }

    function test_displayChoicesArePersistedPerWidget() {
        const page = createPage(displayComponent, {
            cfg_visibleInformation: ["status", "volume"],
            cfg_compactMode: "adaptive"
        });

        verify(findChild(page, "showStatusBox").checked);
        verify(findChild(page, "showVolumeBox").checked);
        verify(!findChild(page, "showScreensaverBox").checked);

        mouseClick(findChild(page, "showScreensaverBox"));
        verify(page.cfg_visibleInformation.indexOf("screensaver") >= 0);

        mouseClick(findChild(page, "showStatusBox"));
        compare(page.cfg_visibleInformation.indexOf("status"), -1);
    }

    function test_panelTextIsASimpleOnOffChoice() {
        const page = createPage(displayComponent, {
            cfg_compactMode: "adaptive"
        });
        const nameBox = findChild(page, "showPanelNameBox");
        verify(nameBox !== null, "Display must offer an on/off switch for the panel text");
        verify(nameBox.checked);

        mouseClick(nameBox);
        compare(page.cfg_compactMode, "iconOnly",
                "turning the text off must store the icon-only mode");

        const legacy = createPage(displayComponent, { cfg_compactMode: "iconAndName" });
        verify(findChild(legacy, "showPanelNameBox").checked,
               "the older icon-and-name choice counts as text shown");

        const hidden = createPage(displayComponent, { cfg_compactMode: "iconOnly" });
        verify(!findChild(hidden, "showPanelNameBox").checked);

        mouseClick(findChild(page, "showPanelNameBox"));
        compare(page.cfg_compactMode, "adaptive",
                "turning the text back on must restore showing it");
    }

    function test_controlChoicesArePersistedPerWidget() {
        const page = createPage(controlsComponent, {
            cfg_visibleControls: ["power", "volume"]
        });

        verify(findChild(page, "powerControlBox").checked);
        verify(!findChild(page, "navigationControlBox").checked);

        mouseClick(findChild(page, "navigationControlBox"));
        verify(page.cfg_visibleControls.indexOf("navigation") >= 0);

        mouseClick(findChild(page, "powerControlBox"));
        compare(page.cfg_visibleControls.indexOf("power"), -1);
    }

    function createPeers(properties) {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const bridge = createTemporaryObject(bridgeComponent, testCase);
        verify(bridge);
        bridge.setFixturePeers({
            listener: { enabled: true, listening: true, port: 49160, boundPort: 49160 },
            controllers: [{
                machineId: "11111111111111111111111111111111",
                addedAt: "2026-11-15T00:14:58.763"
            }],
            targets: [{
                machineId: "22222222222222222222222222222222",
                displayName: "office-vm",
                address: "192.0.2.112",
                port: 49160
            }]
        });
        const page = createTemporaryObject(peersComponent, window.contentItem, Object.assign({
            bridge: bridge,
            width: 700,
            height: 620,
            visible: true
        }, properties ?? {}));
        verify(page, "the Peers page must instantiate cleanly");
        return { bridge: bridge, page: page };
    }

    function test_thePeersPageMirrorsTheDaemonListing() {
        const fixture = createPeers();

        verify(findChild(fixture.page, "listenerSwitch").checked);
        verify(!findChild(fixture.page, "noTargetsMessage").visible);
        verify(!findChild(fixture.page, "noControllersMessage").visible);

        const target = findChild(fixture.page, "peerTarget-22222222222222222222222222222222");
        verify(target !== null, "the paired peer must render");
        mouseClick(findChild(target, "removePeerButton"));
        compare(fixture.bridge.removedTargets,
                ["22222222222222222222222222222222"]);

        const controller = findChild(fixture.page, "controllerEntry-11111111111111111111111111111111");
        verify(controller !== null);
        mouseClick(findChild(controller, "revokeControllerButton"));
        compare(fixture.bridge.revokedControllers,
                ["11111111111111111111111111111111"]);
    }

    function test_theListenerSwitchWritesAndKeepsFollowingTheListing() {
        const fixture = createPeers();
        const listenerSwitch = findChild(fixture.page, "listenerSwitch");
        verify(listenerSwitch.checked);

        mouseClick(listenerSwitch);
        compare(fixture.bridge.peerListenerToggles, ["off"]);
        // The click asked the daemon; the switch keeps following the stored
        // listing until the reload confirms the change (same honesty rule
        // as every device control).
        compare(listenerSwitch.checked, true,
                "the switch must not drift from the listing on an unconfirmed change");
    }

    function test_pairGathersTheFormAndReachesTheDaemon() {
        const fixture = createPeers();
        const addressField = findChild(fixture.page, "addressField");
        const portField = findChild(fixture.page, "portField");
        const codeField = findChild(fixture.page, "codeField");
        const pairButton = findChild(fixture.page, "pairButton");
        verify(!pairButton.enabled, "pairing needs all three fields");

        addressField.text = "192.0.2.112";
        portField.text = "49160";
        verify(!pairButton.enabled, "a missing code must not pair");
        codeField.text = "ABCD2345";
        verify(pairButton.enabled);

        mouseClick(pairButton);
        compare(fixture.bridge.pairedPeers.length, 1);
        compare(fixture.bridge.pairedPeers[0].address, "192.0.2.112");
        compare(fixture.bridge.pairedPeers[0].port, 49160);
        compare(fixture.bridge.pairedPeers[0].code, "ABCD2345");
    }

    function test_thePairingCodeIsShownWhenMinted() {
        const fixture = createPeers();
        const message = findChild(fixture.page, "pairingCodeMessage");
        verify(!message.visible);

        mouseClick(findChild(fixture.page, "showCodeButton"));
        tryVerify(() => message.visible);
        verify(message.text.includes("CODE1234"),
               "the minted code must be shown to the user");
    }

    function test_peerAudioControlsSendExplicitCommands() {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const bridge = createTemporaryObject(bridgeComponent, testCase);
        verify(bridge);
        const controls = createTemporaryObject(peerControlsComponent, window.contentItem, {
            bridge: bridge,
            serial: "PC-22222222",
            deviceName: "office-vm",
            volume: 40,
            muted: false,
            deviceOnline: true,
            ceiling: 50,
            visible: true
        });
        verify(controls, "the peer audio controls must instantiate cleanly");

        // Peer audio is explicit absolute setVolume/setMute — never keypresses.
        mouseClick(findChild(controls, "peerVolumeUpButton"));
        compare(bridge.sentCommands[0].action, "setVolume");
        compare(bridge.sentCommands[0].value, 45);
        mouseClick(findChild(controls, "peerVolumeDownButton"));
        compare(bridge.sentCommands[1].value, 35);
        mouseClick(findChild(controls, "peerMuteButton"));
        compare(bridge.sentCommands[2].action, "setMute");
        compare(bridge.sentCommands[2].value, true);

        // The ceiling bounds the up button: the control computes from its
        // observed reading, so the model delivers 45 and the next step lands
        // exactly on the ceiling.
        controls.volume = 45;
        mouseClick(findChild(controls, "peerVolumeUpButton"));
        compare(bridge.sentCommands[3].value, 50);
        controls.volume = 50;
        verify(!findChild(controls, "peerVolumeUpButton").enabled,
               "the ceiling stops the up button");

        // An offline peer disables everything; without a reading the volume
        // buttons cannot compute a step, but mute still states its target.
        controls.deviceOnline = false;
        verify(!findChild(controls, "peerMuteButton").enabled);
        controls.deviceOnline = true;
        controls.volume = null;
        verify(!findChild(controls, "peerVolumeUpButton").enabled,
               "no reading, no volume action");
        verify(findChild(controls, "peerMuteButton").enabled);
    }
}

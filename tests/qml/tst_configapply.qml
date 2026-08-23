// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtTest

import "../../src/plasmoid/qml" as RokuDeckConfig

TestCase {
    id: testCase

    width: 720
    height: 640
    name: "RokuDeckConfigApply"
    when: windowShown

    Component {
        id: displayComponent

        RokuDeckConfig.ConfigDisplay {}
    }

    Component {
        id: controlsComponent

        RokuDeckConfig.ConfigControls {}
    }

    Component {
        id: windowComponent

        Window {
            width: 720
            height: 640
            visible: true
        }
    }

    // Verbatim port of plasma-desktop AppletConfiguration.qml's dirty
    // tracking: the fake config stands in for KConfigPropertyMap.
    function createPlasmaEnvironment(page, stored) {
        const env = {
            page: page,
            stored: stored,
            applyEnabled: false,
            wasConfigurationChangedSignalSent: false,

            isConfigurationChanged: () => {
                const keys = Object.keys(env.stored);
                return keys.some(key => {
                    const cfgKey = "cfg_" + key;
                    if (!env.page.hasOwnProperty(cfgKey)) {
                        return false;
                    }
                    return env.stored[key] != env.page[cfgKey]
                        && env.stored[key].toString() != env.page[cfgKey].toString();
                });
            },

            settingValueChanged: () => {
                env.applyEnabled = env.wasConfigurationChangedSignalSent
                    || env.isConfigurationChanged();
            }
        };

        Object.keys(stored).forEach(key => {
            const handler = page["cfg_" + key + "Changed"];
            if (handler !== undefined) {
                handler.connect(() => env.settingValueChanged());
            }
        });
        // Plasma also honors an explicit force-dirty signal from the page.
        if (page.configurationChanged !== undefined) {
            page.configurationChanged.connect(() => {
                env.wasConfigurationChangedSignalSent = true;
                env.settingValueChanged();
            });
        }
        return env;
    }

    function openWith(component, initialProps, stored) {
        const window = createTemporaryObject(windowComponent, testCase);
        verify(window);
        window.requestActivate();
        tryVerify(() => window.active);
        const page = createTemporaryObject(component, window.contentItem,
                                           Object.assign({}, initialProps, {
                                               width: 700,
                                               height: 620,
                                               visible: true
                                           }));
        verify(page);
        const env = createPlasmaEnvironment(page, stored);
        env.settingValueChanged();
        verify(!env.applyEnabled, "a fresh page must not show unsaved changes");
        return {
            page: page,
            env: env,
            click: (name) => {
                const control = findChild(page, name);
                verify(control !== null, "missing " + name);
                mouseClick(control);
            }
        };
    }

    function test_displayPageMarksEveryChangeForApply() {
        const fixture = openWith(displayComponent, {
            cfg_visibleInformation: ["status", "activeApp", "volume", "screensaver"],
            cfg_compactMode: "adaptive"
        }, {
            visibleInformation: ["status", "activeApp", "volume", "screensaver"],
            compactMode: "adaptive"
        });

        fixture.click("showPanelNameBox");
        tryVerify(() => fixture.env.applyEnabled);
        fixture.click("showPanelNameBox");
        tryVerify(() => fixture.env.applyEnabled, 5000,
                  "once changed, Apply stays offered until saved");

        fixture.click("showScreensaverBox");
        tryVerify(() => fixture.env.applyEnabled);
        fixture.click("showVolumeBox");
        tryVerify(() => fixture.env.applyEnabled);
        fixture.click("showVolumeBox");
        tryVerify(() => fixture.env.applyEnabled, 5000, "volume still differs from stored");
        fixture.click("showScreensaverBox");
        tryVerify(() => fixture.env.applyEnabled, 5000, "latched until saved");
    }

    function test_controlsPageMarksEveryChangeForApply() {
        const fixture = openWith(controlsComponent, {
            cfg_visibleControls: ["power", "volume"]
        }, {
            visibleControls: ["power", "volume"]
        });

        fixture.click("navigationControlBox");
        tryVerify(() => fixture.env.applyEnabled);
        fixture.click("volumeControlBox");
        tryVerify(() => fixture.env.applyEnabled);
        fixture.click("navigationControlBox");
        fixture.click("volumeControlBox");
        tryVerify(() => fixture.env.applyEnabled, 5000, "latched until saved");
    }
}

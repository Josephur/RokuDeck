// SPDX-License-Identifier: GPL-3.0-or-later

// Regression pins for the rule editor dialog's sizing and popup behavior.
// Every assertion here guards a bug that reached users once:
//  - preferredWidth is ignored when contentItem is overridden, so the popup
//    must be sized via explicit width/height (it collapsed to ~220px);
//  - clip on the editor Flickable cuts off ComboBox dropdowns, so it must
//    stay false;
//  - the mute combo must keep offering both directions.

import QtQuick
import QtTest

import "../../src/plasmoid/qml" as RokuDeck

TestCase {
    id: testCase

    width: 500
    height: 600
    name: "RuleEditor"
    when: windowShown

    QtObject {
        id: testBridge

        // Minimal mirror of RokuDeckApplet's bridge surface that the editor
        // touches at construction/open time.
        property bool available: true

        function deviceEntries() {
            return [{ serial: "SERIAL-B", name: "Bedroom TV" }];
        }
    }

    Component {
        id: editorComponent

        RokuDeck.ConfigRuleEditor {
            bridge: testBridge
        }
    }

    RokuDeck.ActionStepEditor {
        id: step

        anchors.centerIn: parent
        node: ({ type: "mute", muted: true })
    }

    function findItem(rootItem, name) {
        if (rootItem === null || rootItem === undefined) {
            return null;
        }
        if (rootItem.objectName === name) {
            return rootItem;
        }
        // Kirigami.Dialog is a Popup, not an Item: its visual tree hangs off
        // contentItem, while Items expose children.
        const kids = rootItem.children;
        if (kids !== undefined && kids !== null) {
            for (let i = 0; i < kids.length; ++i) {
                const found = findItem(kids[i], name);
                if (found !== null) {
                    return found;
                }
            }
        }
        const content = rootItem.contentItem;
        if (content !== undefined && content !== null && content !== rootItem) {
            return findItem(content, name);
        }
        return null;
    }

    function test_editorIsSizedExplicitlyAndNotClipped() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();

        // The sizing collapse bug produced a ~220px-wide dialog; anything in
        // that neighborhood means the explicit width/height bindings were
        // disconnected again.
        verify(editor.width > 400,
               "editor width must exceed 400px, got " + editor.width);
        verify(editor.height > 400,
               "editor height must exceed 400px, got " + editor.height);

        const scroll = findItem(editor, "ruleEditorScroll");
        verify(scroll !== null, "the editor scroll area must exist");
        verify(scroll.clip === false,
               "the editor scroll area must never clip: it cuts off combo popups");

        editor.destroy();
    }

    function test_muteComboOffersBothDirections() {
        const combo = findItem(step, "muteChoiceCombo");
        verify(combo !== null, "mute combo must exist");
        compare(combo.count, 2);
        compare(combo.model.length, 2);
        compare(combo.model[0].label, "Muted");
        compare(combo.model[1].label, "Unmuted");
        compare(combo.currentIndex, 0);
        compare(combo.currentText, "Muted");
    }

    // The M8 editor surfaces: a device scope on stateChange triggers, a
    // time-of-day window, and the run-script step. AGENTS.md requires new
    // editor surfaces to be pinned here, not just in the payload tests.
    function test_scopedDeviceWindowAndScriptRoundTripIntoTheEditor() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForEdit({
            id: "rWindowed",
            name: "Overnight movie",
            enabled: true,
            mode: "queued",
            devices: [],
            trigger: {
                type: "stateChange",
                device: "SERIAL-B",
                field: "playing",
                op: "is",
                value: true,
                activeWindow: { start: "22:00", end: "08:00" }
            },
            actions: [{ type: "runScript", script: "movie-night" }]
        });

        const deviceCombo = findItem(editor, "stateChangeDeviceCombo");
        verify(deviceCombo !== null, "the scoped-device picker must exist");
        compare(deviceCombo.model.length, 2, "Any plus the known device");
        compare(deviceCombo.model[0].value, "*");
        compare(deviceCombo.model[1].value, "SERIAL-B");
        compare(deviceCombo.currentIndex, 1,
                "a scoped rule must select its device, not fall back to Any");

        const windowRadio = findItem(editor, "activeWindowRadio");
        verify(windowRadio !== null, "the window radio must exist");
        verify(windowRadio.checked, "an activeWindow rule must open on Between");
        const startField = findItem(editor, "windowStartField");
        const endField = findItem(editor, "windowEndField");
        verify(startField !== null && endField !== null);
        compare(startField.text, "22:00");
        compare(endField.text, "08:00");

        const scriptField = findItem(editor, "scriptNameField");
        verify(scriptField !== null, "the run-script step editor must exist");
        compare(scriptField.text, "movie-night");

        editor.destroy();
    }

    function test_anytimeRuleHasNoWindowFieldsVisible() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();
        const windowRow = findItem(editor, "windowStartField");
        verify(windowRow !== null, "the window fields exist for later rules");
        verify(!windowRow.visible,
               "an Anytime rule must not show window bounds");
        editor.destroy();
    }
}

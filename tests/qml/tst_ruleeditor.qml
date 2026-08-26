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
            return [];
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
}

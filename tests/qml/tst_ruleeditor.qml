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
import "../../src/plasmoid/qml/rulepayload.js" as RulePayload

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
            return [{ serial: "SERIAL-B", name: "Bedroom TV" },
                    { serial: "LOCAL-PC", name: "This PC" }];
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

    function test_reassertControlsRoundTrip() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForEdit({
            id: "rReassert",
            name: "Holding mute",
            enabled: true,
            mode: "queued",
            devices: ["LOCAL-PC"],
            trigger: {
                type: "stateChange",
                device: "LOCAL-PC",
                field: "muted",
                op: "is",
                value: true,
                reassertSeconds: 45
            },
            actions: [{ type: "mute", muted: true }]
        });
        const box = findItem(editor, "reassertCheckBox");
        const spin = findItem(editor, "reassertSecondsBox");
        verify(box !== null && spin !== null);
        verify(box.checked, "a stored reassertSeconds must check the box");
        compare(spin.value, 45);

        // Unchecking zeroes the interval: fire-on-the-crossing only.
        mouseClick(box);
        verify(!box.checked);
        compare(editor.formState.trigger.reassertSeconds, 0);
        verify(!spin.visible);

        // Re-checking arms the default interval.
        mouseClick(box);
        verify(box.checked);
        compare(editor.formState.trigger.reassertSeconds, 30);
        compare(spin.value, 30);
        editor.destroy();
    }

    function test_triggerFieldsFollowTheDevice() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();
        const combo = findItem(editor, "stateChangeFieldCombo");
        verify(combo !== null, "the trigger field combo must exist");
        const deviceCombo = findItem(editor, "stateChangeDeviceCombo");
        verify(deviceCombo !== null);

        // The form starts on the wildcard trigger: Roku fields only. This-PC
        // edges never reach a wildcard trigger (pc-state opt-in contract), so
        // MPRIS playing must not even be offered.
        const wildcardFields = RulePayload.fieldsForTriggerDevice("*");
        compare(combo.count, wildcardFields.length);
        verify(wildcardFields.indexOf("playing") < 0,
               "a wildcard trigger never sees This-PC edges: no MPRIS playing");

        // A real user switches Fires: to device-state changes first — blank
        // rules are time triggers, and only then does the field list exist.
        const typeCombo = findItem(editor, "triggerTypeCombo");
        verify(typeCombo !== null);
        const stateChangeIndex = typeCombo.model.findIndex(
            choice => choice.value === "stateChange");
        verify(stateChangeIndex >= 0);
        typeCombo.activated(stateChangeIndex);
        compare(combo.count, RulePayload.fieldsForTriggerDevice("*").length,
                "the stateChange field list must populate for the wildcard device");

        // Pointing the trigger at This PC swaps the list: the Roku media
        // field must vanish (it is always Unknown on a PC) and playing must
        // appear, with the human label on display.
        const pcIndex = deviceCombo.model.findIndex(
            choice => choice.value === "LOCAL-PC");
        verify(pcIndex >= 0, "This PC must be among the trigger device choices");
        deviceCombo.activated(pcIndex);
        compare(combo.count, RulePayload.fieldsForTriggerDevice("LOCAL-PC").length);
        verify(combo.model.indexOf("playing") >= 0);
        verify(combo.model.indexOf("media") < 0,
               "the Roku media field must not be offered on This PC");
        compare(combo.displayText,
                RulePayload.fieldLabel(RulePayload.fieldsForTriggerDevice("LOCAL-PC")[0]),
                "the display must show the human field label");

        // And back to a Roku device: playing vanishes, media returns.
        const rokuIndex = deviceCombo.model.findIndex(
            choice => choice.value === "SERIAL-B");
        verify(rokuIndex >= 0);
        deviceCombo.activated(rokuIndex);
        verify(combo.model.indexOf("media") >= 0);
        verify(combo.model.indexOf("playing") < 0);
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
                field: "power",
                op: "is",
                value: "on",
                activeWindow: { start: "22:00", end: "08:00" }
            },
            actions: [{ type: "runScript", script: "movie-night" }]
        });

        const deviceCombo = findItem(editor, "stateChangeDeviceCombo");
        verify(deviceCombo !== null, "the scoped-device picker must exist");
        compare(deviceCombo.model.length, 3, "Any plus the two known devices");
        compare(deviceCombo.model[0].value, "*");
        compare(deviceCombo.model[1].value, "SERIAL-B");
        compare(deviceCombo.model[2].value, "LOCAL-PC");
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

    // Plain-JS form state is not observable, so every mutating editor must
    // bump the revision that drives validation. Typing the name last is the
    // common add-rule flow: add an action, then name the rule.
    function test_typingTheNameRerunsValidation() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();
        const list = findItem(editor, "thenActionsEditor");
        verify(list !== null, "the action list must exist");
        list.append("keypress");
        compare(editor.problems.length, 1, "only the missing name may remain");

        const name = findItem(editor, "nameField");
        verify(name !== null);
        // Emitting the edit signal is exactly the user path; assigning .text
        // alone would bypass the handler under test.
        name.text = "N";
        name.textEdited();
        compare(editor.formState.name, "N");
        compare(editor.problems.length, 0,
                "typing the name must re-run validation so Save can enable");
        editor.destroy();
    }

    // Switching the comparison to Between must rewrite the stored value into
    // a range; otherwise the picker shows "From 0 to 100" while validation
    // rejects the unchanged scalar and Save looks broken.
    function test_switchingComparisonToBetweenRepairsTheValue() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();
        editor.applyTriggerChoice("stateChange");

        const fieldCombo = findItem(editor, "stateChangeFieldCombo");
        verify(fieldCombo !== null);
        const volumeIndex = RulePayload.fields.indexOf("volume");
        verify(volumeIndex >= 0);
        fieldCombo.currentIndex = volumeIndex;
        fieldCombo.activated(volumeIndex);
        compare(editor.formState.trigger.field, "volume");
        compare(editor.formState.trigger.value, 0,
                "a volume predicate must hold a number");

        const opCombo = findItem(editor, "stateChangeOpCombo");
        verify(opCombo !== null);
        const betweenIndex = RulePayload.ops.indexOf("between");
        verify(betweenIndex >= 0);
        opCombo.currentIndex = betweenIndex;
        opCombo.activated(betweenIndex);
        compare(editor.formState.trigger.op, "between");
        verify(Array.isArray(editor.formState.trigger.value)
               || typeof editor.formState.trigger.value.length === "number",
               "Between must store a two-number range, got "
               + JSON.stringify(editor.formState.trigger.value));
        compare(Number(editor.formState.trigger.value.length), 2,
                "Between must store exactly two bounds");
        for (let i = 0; i < editor.problems.length; ++i) {
            verify(editor.problems[i].code !== "betweenNeedsTwoNumbers",
                   "the editor must not reject the value it just wrote");
        }
        editor.destroy();
    }

    // A conditional step must be born with a condition node; a null node is
    // not editable at all (the kind picker dereferences it).
    function test_ifThenElseStepStartsWithAnEditableCondition() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();
        const list = findItem(editor, "thenActionsEditor");
        verify(list !== null);
        list.append("ifThenElse");
        compare(editor.formState.actions.length, 1);
        const cond = editor.formState.actions[0].cond;
        verify(cond !== null && cond !== undefined,
               "an ifThenElse step must own a condition node");
        compare(String(cond.kind), "leaf");

        let branch = null;
        tryVerify(function() {
            branch = findItem(editor, "branchConditionEditor");
            return branch !== null;
        }, 5000, "the branch condition editor must load");
        verify(branch.body.showLeaf,
               "the seeded leaf condition must render its fields");
        editor.destroy();
    }

    // Adding the "not" child mutates a plain-JS field; without a real
    // property the child editor never appears and the condition saves hidden.
    function test_invertedConditionChildBecomesVisible() {
        const editor = editorComponent.createObject(testCase);
        verify(editor !== null);
        editor.openForAdd();
        editor.formState.conditions = {
            kind: "not", children: [], child: null, device: "*",
            field: "power", op: "is", value: "on"
        };
        editor.touchForm();
        const nodeEditor = findItem(editor, "conditionsEditor");
        verify(nodeEditor !== null);
        verify(nodeEditor.body.showNot, "the not body must render");
        verify(!nodeEditor.body.hasNotChild);

        const addButton = findItem(editor, "conditionAddNotChildButton");
        verify(addButton !== null && addButton.visible,
               "the add-child affordance must be visible while empty");
        nodeEditor.body.addNotChild();
        verify(nodeEditor.body.hasNotChild,
               "the added child must be tracked by a real property");
        verify(!addButton.visible, "the add affordance must retire once a child exists");

        let loader = null;
        tryVerify(function() {
            loader = findItem(editor, "notChildLoader");
            return loader !== null && loader.active;
        }, 5000, "the inverted child editor must load");
        editor.destroy();
    }
}

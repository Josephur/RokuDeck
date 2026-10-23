// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras

import "rulepayload.js" as RulePayload

// Structured automation editor: every trigger, condition, and action is built
// from pickers, never free-form JSON. Payload assembly lives in
// rulepayload.js so the exact daemon grammar is pinned by unit tests.
Kirigami.Dialog {
    id: root

    required property var bridge

    // Complete rule document when editing, null when adding.
    property var editingRule: null
    property var formState: null
    property bool saveAttempted: false

    // Server rejections must be readable inside this modal; the page's own
    // banner sits behind it.
    property string errorText: ""

    // Structural edits rebuild plain-JS Repeaters: bumping this tracked int
    // re-evaluates their models against the replaced arrays.
    property int formRevision: 0

    readonly property bool editing: editingRule !== null
    readonly property bool daemonReachable: bridge !== null && bridge.available
    // Reads formRevision so structural edits re-run validation even though
    // the underlying form state is plain JS data.
    readonly property var problems: {
        void root.formRevision;
        return formState === null ? [] : RulePayload.validate(formState);
    }

    title: editing
        ? KI18n.i18nc("@title:window", "Edit Automation")
        : KI18n.i18nc("@title:window", "New Automation")

    // Generous by default: this form holds trigger, conditions, and action
    // sections; the content scrolls only when a macro list outgrows it.
    // Kirigami.Dialog's preferredWidth only feeds its DEFAULT contentItem
    // wrapper; this editor overrides contentItem with a Flickable (which has
    // no intrinsic width), so the popup must be sized directly. Bound by the
    // config window so it never overflows the settings dialog.
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 2 : 720,
                    Kirigami.Units.gridUnit * 48)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 2 : 720,
                     Kirigami.Units.gridUnit * 44)

    function openForAdd() {
        root.editingRule = null;
        root.formState = RulePayload.blankRule();
        root.saveAttempted = false;
        root.errorText = "";
        root.syncFromForm();
        root.open();
    }

    function openForEdit(document) {
        root.editingRule = document && typeof document === "object" ? document : null;
        root.formState = root.editing
            ? RulePayload.toFormState(root.editingRule) : RulePayload.blankRule();
        root.saveAttempted = false;
        root.errorText = "";
        root.syncFromForm();
        root.open();
    }

    function problemText(code) {
        switch (code) {
        case "nameEmpty":
            return KI18n.i18nc("@info", "Give the automation a name.");
        case "nameTooLong":
            return KI18n.i18nc("@info", "The name is limited to 80 characters.");
        case "timeMalformed":
            return KI18n.i18nc("@info", "Enter the time of day as HH:MM.");
        case "dateMissing":
            return KI18n.i18nc("@info",
                "Pick the date for a one-time schedule as YYYY-MM-DD.");
        case "intervalRange":
            return KI18n.i18nc("@info", "Repeat every 5 to 1440 minutes.");
        case "actionsEmpty":
            return KI18n.i18nc("@info", "Add at least one action to run.");
        case "betweenNeedsTwoNumbers":
            return KI18n.i18nc("@info", "\"Between\" needs a low and a high number.");
        case "valueRequired":
            return KI18n.i18nc("@info", "Choose a value to compare against.");
        case "volumeRange":
            return KI18n.i18nc("@info", "Volume must stay between 0 and 100.");
        case "keyInvalid":
            return KI18n.i18nc("@info", "Choose a remote button from the list.");
        case "appIdInvalid":
            return KI18n.i18nc("@info",
                "App ids start with a letter or number, then letters, numbers, dots, dashes, or underscores.");
        case "delayRange":
            return KI18n.i18nc("@info", "Delays run between 1 and 600 seconds.");
        case "ruleTargetInvalid":
            return KI18n.i18nc("@info", "Pick which rule this step runs.");
        case "scriptNameInvalid":
            return KI18n.i18nc("@info",
                "Script names use lowercase letters, numbers, dashes, or underscores, starting with a letter or number.");
        case "windowMalformed":
            return KI18n.i18nc("@info",
                "Enter both window bounds as HH:MM.");
        case "windowDegenerate":
            return KI18n.i18nc("@info",
                "The active window needs two different times.");
        case "branchEmpty":
            return KI18n.i18nc("@info", "The \"then\" branch needs at least one action.");
        case "conditionIncomplete":
            return KI18n.i18nc("@info", "Finish or remove the unfinished condition.");
        case "tooDeep":
            return KI18n.i18nc("@info", "Conditions nest at most 16 levels deep.");
        case "tooManyActions":
            return KI18n.i18nc("@info", "An automation can hold at most 32 actions.");
        case "unsupportedContent":
            return KI18n.i18nc("@info",
                "This rule contains parts the editor cannot change; editing it here would drop them.");
        default:
            return KI18n.i18nc("@info", "Fix the highlighted entries.");
        }
    }

    function touchForm() {
        root.formRevision++;
        conditionsEditor.refreshGates();
    }

    function syncFromForm() {
        if (root.formState === null) {
            return;
        }
        nameField.text = String(root.formState.name ?? "");
        enabledSwitch.checked = root.formState.enabled === true;
        for (let mode = 0; mode < modeCombo.model.length; ++mode) {
            if (modeCombo.model[mode].value === root.formState.mode) {
                modeCombo.currentIndex = mode;
            }
        }
        for (let choice = 0; choice < triggerTypeCombo.model.length; ++choice) {
            const candidate = triggerTypeCombo.model[choice].value;
            const actual = root.formState.trigger.type === "time"
                ? root.formState.trigger.kind : root.formState.trigger.type;
            if (candidate === actual) {
                triggerTypeCombo.currentIndex = choice;
            }
        }
        timeField.text = String(root.formState.trigger.time ?? "");
        dateField.text = String(root.formState.trigger.date ?? "");
        intervalMinutesSpin.value = Number(root.formState.trigger.minutes);
        for (let field = 0; field < stateChangeFieldCombo.model.length; ++field) {
            if (stateChangeFieldCombo.model[field] === root.formState.trigger.field) {
                stateChangeFieldCombo.currentIndex = field;
            }
        }
        for (let op = 0; op < stateChangeOpCombo.model.length; ++op) {
            if (stateChangeOpCombo.model[op] === root.formState.trigger.op) {
                stateChangeOpCombo.currentIndex = op;
            }
        }
        const deviceChoices = stateChangeDeviceCombo.model;
        for (let scope = 0; scope < deviceChoices.length; ++scope) {
            if (deviceChoices[scope].value
                === (root.formState.trigger.device ?? "*")) {
                stateChangeDeviceCombo.currentIndex = scope;
            }
        }
        windowRadio.checked = root.formState.trigger.windowEnabled === true;
        anytimeRadio.checked = !windowRadio.checked;
        windowStartField.text = String(root.formState.trigger.windowStart ?? "");
        windowEndField.text = String(root.formState.trigger.windowEnd ?? "");
        stateChangeValue.syncing = true;
        stateChangeValue.field = root.formState.trigger.field;
        stateChangeValue.op = root.formState.trigger.op;
        stateChangeValue.value = root.formState.trigger.value;
        stateChangeValue.syncing = false;
        for (let row = 0; row < deviceChecks.count; ++row) {
            const entry = deviceChecks.itemAt(row);
            entry.checked = root.formState.devices.indexOf(entry.serial) >= 0;
        }
        if (root.formState.conditions !== null) {
            conditionsEditor.syncFromNode();
        }
        thenActionsEditor.storeObject = root.formState;
        thenActionsEditor.storeKey = "actions";
        thenActionsEditor.actions = root.formState.actions;
    }

    function save() {
        root.saveAttempted = true;
        root.errorText = "";
        if (RulePayload.validate(root.formState).length > 0 || !root.daemonReachable) {
            return;
        }
        const payload = RulePayload.buildPayload(root.formState, root.editingRule);
        if (root.editing) {
            root.bridge.updateRule(payload);
        } else {
            root.bridge.addRule(payload);
        }
    }

    readonly property var modeChoices: [
        { value: "queued", label: KI18n.i18nc("@option", "Queue while busy") },
        { value: "restart", label: KI18n.i18nc("@option", "Restart while busy") },
        { value: "single", label: KI18n.i18nc("@option", "Skip while busy") }
    ]

    readonly property var triggerChoices: [
        { value: "daily", label: KI18n.i18nc("@option", "Every day at…") },
        { value: "weekdays", label: KI18n.i18nc("@option", "Weekdays at…") },
        { value: "once", label: KI18n.i18nc("@option", "Once on a date…") },
        { value: "interval", label: KI18n.i18nc("@option", "Every N minutes") },
        { value: "stateChange", label: KI18n.i18nc("@option", "When a device state changes") },
        { value: "manual", label: KI18n.i18nc("@option", "Only when run by hand") }
    ]

    function applyTriggerChoice(choice) {
        const trigger = root.formState.trigger;
        if (choice === "interval") {
            trigger.type = "interval";
        } else if (choice === "stateChange") {
            trigger.type = "stateChange";
        } else if (choice === "manual") {
            trigger.type = "manual";
        } else {
            trigger.type = "time";
            trigger.kind = choice;
            if (choice === "once" && !RulePayload.dateIsValid(trigger.date)) {
                trigger.date = "";
            }
        }
        root.syncFromForm();
        root.touchForm();
    }

    footer: QQC2.DialogButtonBox {
        QQC2.Button {
            objectName: "ruleEditorCancelButton"

            icon.name: "dialog-cancel"
            text: KI18n.i18nc("@action:button", "Cancel")
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole
        }

        QQC2.Button {
            objectName: "ruleEditorSaveButton"

            icon.name: "dialog-ok-apply"
            text: KI18n.i18nc("@action:button", "Save")
            enabled: root.daemonReachable && root.problems.length === 0
                     && !(root.bridge?.busy ?? false)
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
            QQC2.ToolTip.text: root.problems.length > 0
                ? root.problemText(root.problems[0].code)
                : ""
            QQC2.ToolTip.visible: !enabled && root.problems.length > 0 && hovered
        }

        onAccepted: root.save()
        onRejected: root.close()
    }

    contentItem: Flickable {
        id: editorScroll

        objectName: "ruleEditorScroll"

        // Deliberately NOT clipped: a clip here would also cut off every
        // ComboBox dropdown (popups render inside the control's subtree),
        // which is exactly the clipped-dropdown bug this dialog shipped once.
        // Scroll overshoot lands in the dialog's padding, which is invisible.
        clip: false
        contentHeight: editorColumn.implicitHeight
        // Generous viewport; long macro lists scroll instead of squeezing.
        implicitHeight: Math.min(editorColumn.implicitHeight,
                                 Kirigami.Units.gridUnit * 40)
        interactive: contentHeight > height

        ColumnLayout {
            id: editorColumn

            width: editorScroll.width
            spacing: Kirigami.Units.largeSpacing

            Kirigami.InlineMessage {
                objectName: "editorErrorMessage"

                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                visible: root.errorText.length > 0
                text: root.errorText
            }

            Kirigami.InlineMessage {
                objectName: "editorValidationMessage"

                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                visible: root.saveAttempted && root.problems.length > 0
                text: {
                    const lines = [];
                    for (let i = 0; i < root.problems.length; ++i) {
                        lines.push("• " + root.problemText(root.problems[i].code));
                    }
                    return lines.join("\n");
                }
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                QQC2.TextField {
                    id: nameField

                    objectName: "nameField"

                    Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Name:")
                    placeholderText: KI18n.i18nc("@info:placeholder", "Night volume")
                    onTextEdited: {
                        root.formState.name = text;
                        root.touchForm();
                    }
                }

                QQC2.Switch {
                    id: enabledSwitch

                    objectName: "enabledSwitch"

                    visible: root.editing
                    Kirigami.FormData.label: KI18n.i18nc("@label", "Active:")
                    onToggled: root.formState.enabled = checked
                }

                PlasmaComponents.ComboBox {
                    id: modeCombo

                    objectName: "modeCombo"

                    Kirigami.FormData.label: KI18n.i18nc("@label", "If already running:")
                    textRole: "label"
                    model: root.modeChoices
                    onActivated: index => root.formState.mode = model[index].value
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            PlasmaComponents.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
                text: root.formState !== null && root.formState.devices.length === 0
                    ? KI18n.i18nc("@info", "No devices selected, so this rule watches every device.")
                    : ""
            }

            Repeater {
                id: deviceChecks

                objectName: "deviceChecksRepeater"

                model: root.bridge !== null ? root.bridge.devices : null

                delegate: QQC2.CheckBox {
                    id: deviceCheckDelegate

                    required property int index
                    required property string serial
                    required property string name

                    objectName: "deviceCheck-" + serial

                    Layout.fillWidth: true
                    text: name
                    onToggled: {
                        const next = root.formState.devices.filter(
                            entry => entry !== deviceCheckDelegate.serial);
                        if (checked) {
                            next.push(deviceCheckDelegate.serial);
                        }
                        root.formState.devices = next;
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            PlasmaExtras.Heading {
                level: 4
                text: KI18n.i18nc("@title:group", "When")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                PlasmaComponents.ComboBox {
                    id: triggerTypeCombo

                    objectName: "triggerTypeCombo"

                    Kirigami.FormData.label: KI18n.i18nc("@label", "Fires:")
                    textRole: "label"
                    model: root.triggerChoices
                    onActivated: index => root.applyTriggerChoice(model[index].value)
                }

                QQC2.TextField {
                    id: timeField

                    objectName: "timeField"

                    visible: root.formState !== null
                             && root.formState.trigger.type === "time"
                    inputMask: "99:99"
                    inputMethodHints: Qt.ImhPreferNumbers
                    Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Time of day:")
                    placeholderText: "23:00"
                    onTextEdited: {
                        root.formState.trigger.time = text;
                        root.touchForm();
                    }
                }

                QQC2.TextField {
                    id: dateField

                    objectName: "dateField"

                    visible: root.formState !== null && root.formState.trigger.type === "time"
                             && root.formState.trigger.kind === "once"
                    inputMask: "9999-99-99"
                    inputMethodHints: Qt.ImhPreferNumbers
                    Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Date:")
                    placeholderText: "2026-12-31"
                    onTextEdited: {
                        root.formState.trigger.date = text;
                        root.touchForm();
                    }
                }

                QQC2.SpinBox {
                    id: intervalMinutesSpin

                    objectName: "intervalMinutesSpin"

                    visible: root.formState !== null
                             && root.formState.trigger.type === "interval"
                    from: 5
                    to: 1440
                    editable: true
                    Kirigami.FormData.label:
                        KI18n.i18nc("@label spinbox minutes", "Every (minutes):")
                    onValueModified: {
                        root.formState.trigger.minutes = value;
                        root.touchForm();
                    }
                }

                PlasmaComponents.ComboBox {
                    id: stateChangeDeviceCombo

                    objectName: "stateChangeDeviceCombo"

                    visible: root.formState !== null
                             && root.formState.trigger.type === "stateChange"
                    Kirigami.FormData.label: KI18n.i18nc("@label", "On:")
                    textRole: "label"
                    model: {
                        const choices = [{
                            label: KI18n.i18nc("@option any-device", "Any device"),
                            value: "*"
                        }];
                        const entries = root.bridge !== null
                                        && root.bridge.deviceEntries !== undefined
                            ? root.bridge.deviceEntries() : [];
                        for (let i = 0; i < entries.length; ++i) {
                            choices.push({ label: String(entries[i].name),
                                           value: String(entries[i].serial) });
                        }
                        return choices;
                    }
                    onActivated: index => {
                        root.formState.trigger.device = model[index].value;
                        root.touchForm();
                    }
                }

                PlasmaComponents.ComboBox {
                    id: stateChangeFieldCombo

                    objectName: "stateChangeFieldCombo"

                    visible: root.formState !== null
                             && root.formState.trigger.type === "stateChange"
                    Kirigami.FormData.label: KI18n.i18nc("@label", "Watch:")
                    model: RulePayload.fields
                    onActivated: index => {
                        root.formState.trigger.field = model[index];
                        // RuleValueInput canonicalizes for the new field and
                        // reports the value back through onValueEdited.
                        stateChangeValue.field = model[index];
                        root.touchForm();
                    }
                }

                PlasmaComponents.ComboBox {
                    id: stateChangeOpCombo

                    objectName: "stateChangeOpCombo"

                    visible: root.formState !== null
                             && root.formState.trigger.type === "stateChange"
                    Kirigami.FormData.label: KI18n.i18nc("@label", "Changes:")
                    model: RulePayload.ops
                    onActivated: index => {
                        root.formState.trigger.op = model[index];
                        stateChangeValue.op = model[index];
                        root.touchForm();
                    }
                }

                RuleValueInput {
                    id: stateChangeValue

                    objectName: "stateChangeValue"

                    Kirigami.FormData.label: KI18n.i18nc("@label", "To:")
                    visible: root.formState !== null
                             && root.formState.trigger.type === "stateChange"
                    onValueEdited: value => {
                        root.formState.trigger.value = value;
                        root.touchForm();
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            PlasmaExtras.Heading {
                level: 4
                text: KI18n.i18nc("@title:group", "Active")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                QQC2.RadioButton {
                    id: anytimeRadio

                    objectName: "activeAnytimeRadio"

                    Kirigami.FormData.label: KI18n.i18nc("@label", "Fires:")
                    text: KI18n.i18nc("@option", "Anytime")
                    onToggled: {
                        if (checked && root.formState !== null) {
                            root.formState.trigger.windowEnabled = false;
                            root.touchForm();
                        }
                    }
                }

                QQC2.RadioButton {
                    id: windowRadio

                    objectName: "activeWindowRadio"

                    text: KI18n.i18nc("@option", "Between")
                    onToggled: {
                        if (checked && root.formState !== null) {
                            root.formState.trigger.windowEnabled = true;
                            root.touchForm();
                        }
                    }
                }

                RowLayout {
                    visible: root.formRevision >= 0 && root.formState !== null
                             && root.formState.trigger.windowEnabled === true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        id: windowStartField

                        objectName: "windowStartField"

                        inputMask: "99:99"
                        inputMethodHints: Qt.ImhPreferNumbers
                        placeholderText: "22:00"
                        onTextEdited: {
                            root.formState.trigger.windowStart = text;
                            root.touchForm();
                        }
                    }

                    QQC2.Label {
                        text: KI18n.i18nc("@label window separator", "to")
                    }

                    QQC2.TextField {
                        id: windowEndField

                        objectName: "windowEndField"

                        inputMask: "99:99"
                        inputMethodHints: Qt.ImhPreferNumbers
                        placeholderText: "08:00"
                        onTextEdited: {
                            root.formState.trigger.windowEnd = text;
                            root.touchForm();
                        }
                    }
                }
            }

            PlasmaComponents.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.formState !== null
                         && root.formState.trigger.windowEnabled === true
                         && (root.formState.trigger.windowStart ?? "").length === 4
                         && (root.formState.trigger.windowEnd ?? "").length === 4
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                text: KI18n.i18nc("@info",
                    "A window past midnight wraps around, e.g. 22:00 to 08:00.")
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            PlasmaExtras.Heading {
                level: 4
                text: KI18n.i18nc("@title:group", "Extra conditions")
            }

            QQC2.Button {
                objectName: "addConditionsButton"

                visible: root.formRevision >= 0 && root.formState !== null
                         && root.formState.conditions === null
                icon.name: "list-add"
                text: KI18n.i18nc("@action:button", "Require device conditions")
                onClicked: {
                    root.formState.conditions = RulePayload.blankLeaf();
                    root.touchForm();
                }
            }

            ConditionNodeEditor {
                id: conditionsEditor

                objectName: "conditionsEditor"

                visible: root.formRevision >= 0 && root.formState !== null
                         && root.formState.conditions !== null
                Layout.fillWidth: true
                bridge: root.bridge
                node: root.formRevision >= 0 && root.formState !== null
                      ? root.formState.conditions : null
                depth: 0
                revision: root.formRevision
                onChanged: root.touchForm()
                onStructureChanged: root.touchForm()
            }

            QQC2.Button {
                objectName: "removeConditionsButton"

                visible: root.formRevision >= 0 && root.formState !== null
                         && root.formState.conditions !== null
                icon.name: "list-remove"
                text: KI18n.i18nc("@action:button", "Drop extra conditions")
                onClicked: {
                    root.formState.conditions = null;
                    root.touchForm();
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            PlasmaExtras.Heading {
                level: 4
                text: KI18n.i18nc("@title:group", "Then")
            }

            PlasmaComponents.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
                text: KI18n.i18nc("@info",
                    "What the automation does when it fires.")
            }

            ActionListEditor {
                id: thenActionsEditor

                objectName: "thenActionsEditor"

                Layout.fillWidth: true
                bridge: root.bridge
                depth: 0
                revision: root.formRevision
                editingRuleId: root.editing ? String(root.editingRule.id ?? "") : ""
                onChanged: root.touchForm()
                onStructureChanged: root.touchForm()
            }
        }
    }
}

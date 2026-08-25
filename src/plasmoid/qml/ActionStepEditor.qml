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

// One action step: typed editors per action kind, and for ifThenElse a
// condition builder with nested Then/Else lists. The branch lists load
// ActionListEditor.qml by URL because that file loads this one.
ColumnLayout {
    id: stepRoot

    signal changed()
    signal structureChanged()

    property var bridge: null
    property var node: null
    property int depth: 0
    property int revision: 0
    property string editingRuleId: ""

    readonly property string kind: node !== null ? String(node.type) : ""
    readonly property bool showBranches: kind === "ifThenElse" && revision >= 0
    readonly property var ruleChoices: {
        void revision;
        const entries = bridge !== null && bridge.ruleEntries !== undefined
            ? bridge.ruleEntries(editingRuleId) : [];
        return entries.map(entry => ({ label: String(entry.name),
                                       value: String(entry.id) }));
    }

    spacing: Kirigami.Units.smallSpacing

    function bubble() {
        stepRoot.changed();
    }

    function syncFromNode() {
        if (stepRoot.node === null) {
            return;
        }
        keyCombo.currentIndex = Math.max(0,
            RulePayload.keypressKeys.indexOf(String(node.key)));
        appIdField.text = String(node.appId ?? "");
        scriptNameField.text = String(node.script ?? "");
        volumeSlider.value = Number(node.volume);
        muteCombo.currentIndex = node.muted === true ? 0 : 1;
        delaySpin.value = Number(node.seconds);
        runRuleIdField.text = String(node.id ?? "");
        for (let choice = 0; choice < ruleChoices.length; ++choice) {
            if (ruleChoices[choice].value === runRuleIdField.text) {
                ruleCombo.currentIndex = choice;
            }
        }
    }

    Component.onCompleted: {
        if (stepRoot.kind === "keypress" || stepRoot.kind === "launchApp"
            || stepRoot.kind === "setVolume" || stepRoot.kind === "mute"
            || stepRoot.kind === "delay" || stepRoot.kind === "runRule"
            || stepRoot.kind === "runScript") {
            stepRoot.syncFromNode();
        }
    }

    // ActionListEditor assigns node right after loading this editor, long
    // after construction; reseed so loaded values reach every control
    // instead of defaults overwriting them on save.
    onNodeChanged: stepRoot.syncFromNode()

    PlasmaComponents.Label {
        Layout.fillWidth: true
        font.bold: true
        text: {
            switch (stepRoot.kind) {
            case "keypress":
                return KI18n.i18nc("@title step", "Press a remote button");
            case "launchApp":
                return KI18n.i18nc("@title step", "Launch an app");
            case "setVolume":
                return KI18n.i18nc("@title step", "Set the volume");
            case "mute":
                return KI18n.i18nc("@title step", "Mute or unmute");
            case "delay":
                return KI18n.i18nc("@title step", "Wait a while");
            case "runRule":
                return KI18n.i18nc("@title step", "Run another automation");
            case "runScript":
                return KI18n.i18nc("@title step", "Run one of your scripts");
            case "ifThenElse":
                return KI18n.i18nc("@title step", "Decide between two branches");
            default:
                return KI18n.i18nc("@title step", "Custom action");
            }
        }
    }

    Kirigami.FormLayout {
        visible: stepRoot.kind === "keypress"
        Layout.fillWidth: true

        PlasmaComponents.ComboBox {
            popup.parent: typeof applicationWindow !== "undefined" ? applicationWindow().overlay : undefined
            id: keyCombo

            objectName: "keyChoiceCombo"

            Kirigami.FormData.label: KI18n.i18nc("@label", "Button:")
            model: RulePayload.keypressKeys
            onActivated: index => {
                stepRoot.node.key = model[index];
                stepRoot.bubble();
            }
        }
    }

    Kirigami.FormLayout {
        visible: stepRoot.kind === "launchApp"
        Layout.fillWidth: true

        QQC2.TextField {
            id: appIdField

            objectName: "appIdField"

            Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "App id:")
            placeholderText: KI18n.i18nc("@info:placeholder", "tv.netflix")
            onTextEdited: {
                stepRoot.node.appId = text;
                stepRoot.bubble();
            }
        }

        PlasmaComponents.Label {
            objectName: "appIdValidation"

            visible: !RulePayload.appIdIsValid(stepRoot.node?.appId ?? "")
            color: Kirigami.Theme.neutralTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            text: KI18n.i18nc("@info",
                "App ids start with a letter or number, then letters, numbers, dots, dashes, or underscores.")
        }
    }

    RowLayout {
        visible: stepRoot.kind === "setVolume"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label", "Volume")
        }

        QQC2.Slider {
            id: volumeSlider

            objectName: "volumeSlider"

            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            onMoved: {
                stepRoot.node.volume = value;
                stepRoot.bubble();
            }
        }

        QQC2.Label {
            objectName: "volumeValueLabel"

            text: Number(volumeSlider.value).toString()
        }
    }

    Kirigami.FormLayout {
        visible: stepRoot.kind === "mute"
        Layout.fillWidth: true

        PlasmaComponents.ComboBox {
            popup.parent: typeof applicationWindow !== "undefined" ? applicationWindow().overlay : undefined
            id: muteCombo

            objectName: "muteChoiceCombo"

            Kirigami.FormData.label: KI18n.i18nc("@label", "Sound:")
            textRole: "label"
            model: [
                { label: KI18n.i18nc("@option", "Muted"), flag: true },
                { label: KI18n.i18nc("@option", "Unmuted"), flag: false }
            ]
            onActivated: index => {
                stepRoot.node.muted = model[index].flag;
                stepRoot.bubble();
            }
        }
    }

    Kirigami.FormLayout {
        visible: stepRoot.kind === "delay"
        Layout.fillWidth: true

        QQC2.SpinBox {
            id: delaySpin

            objectName: "delaySecondsSpin"

            Kirigami.FormData.label:
                KI18n.i18nc("@label spinbox seconds", "Wait (seconds):")
            from: 1
            to: 600
            editable: true
            onValueModified: {
                stepRoot.node.seconds = value;
                stepRoot.bubble();
            }
        }
    }

    ColumnLayout {
        visible: stepRoot.kind === "runRule"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.ComboBox {
            popup.parent: typeof applicationWindow !== "undefined" ? applicationWindow().overlay : undefined
            id: ruleCombo

            objectName: "runRuleChoiceCombo"

            visible: stepRoot.ruleChoices.length > 0
            Layout.fillWidth: true
            textRole: "label"
            model: stepRoot.ruleChoices
            onActivated: index => {
                stepRoot.node.id = model[index].value;
                runRuleIdField.text = model[index].value;
                stepRoot.bubble();
            }
        }

        QQC2.TextField {
            id: runRuleIdField

            objectName: "runRuleIdField"

            Layout.fillWidth: true
            placeholderText: KI18n.i18nc("@info:placeholder",
                "Automation id, e.g. rAbc123def")
            onTextEdited: {
                stepRoot.node.id = text;
                stepRoot.bubble();
            }
        }
    }

    ColumnLayout {
        visible: stepRoot.kind === "runScript"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: scriptNameField

                objectName: "scriptNameField"

                Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Script:")
                placeholderText: KI18n.i18nc("@info:placeholder", "movie-night")
                onTextEdited: {
                    stepRoot.node.script = text;
                    stepRoot.bubble();
                }
            }
        }

        PlasmaComponents.Label {
            objectName: "scriptNameValidation"

            visible: stepRoot.kind === "runScript"
                     && !RulePayload.scriptIsValid(stepRoot.node?.script ?? "")
            color: Kirigami.Theme.neutralTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            wrapMode: Text.WordWrap
            text: KI18n.i18nc("@info",
                "Script names use lowercase letters, numbers, dashes, or underscores, starting with a letter or number. The daemon runs scripts it finds in its own scripts folder.")
        }
    }

    ColumnLayout {
        visible: stepRoot.showBranches
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing

        ConditionNodeEditor {
            objectName: "branchConditionEditor"

            Layout.fillWidth: true
            bridge: stepRoot.bridge
            node: stepRoot.showBranches ? stepRoot.node.cond : null
            depth: stepRoot.depth + 1
            revision: stepRoot.revision
            onChanged: stepRoot.bubble()
            onStructureChanged: stepRoot.structureChanged()
        }

        PlasmaExtras.Heading {
            level: 5
            text: KI18n.i18nc("@title:group", "Then")
        }

        Loader {
            id: thenLoader

            objectName: "thenActionsLoader"

            active: stepRoot.showBranches
            Layout.fillWidth: true
            source: "ActionListEditor.qml"
            onLoaded: {
                thenLoader.item.bridge = stepRoot.bridge;
                thenLoader.item.actions = stepRoot.node.thenActions;
                thenLoader.item.depth = stepRoot.depth + 1;
                thenLoader.item.revision = stepRoot.revision;
                thenLoader.item.editingRuleId = stepRoot.editingRuleId;
                thenLoader.item.storeObject = stepRoot.node;
                thenLoader.item.storeKey = "thenActions";
                thenLoader.item.changed.connect(stepRoot.bubble);
                thenLoader.item.structureChanged.connect(stepRoot.structureChanged);
            }
        }

        PlasmaExtras.Heading {
            level: 5
            text: KI18n.i18nc("@title:group", "Optional Else")
        }

        Loader {
            id: elseLoader

            objectName: "elseActionsLoader"

            active: stepRoot.showBranches
            Layout.fillWidth: true
            source: "ActionListEditor.qml"
            onLoaded: {
                elseLoader.item.bridge = stepRoot.bridge;
                elseLoader.item.actions = stepRoot.node.elseActions ?? [];
                elseLoader.item.depth = stepRoot.depth + 1;
                elseLoader.item.revision = stepRoot.revision;
                elseLoader.item.editingRuleId = stepRoot.editingRuleId;
                elseLoader.item.storeObject = stepRoot.node;
                elseLoader.item.storeKey = "elseActions";
                elseLoader.item.changed.connect(stepRoot.bubble);
                elseLoader.item.structureChanged.connect(stepRoot.structureChanged);
            }
        }
    }
}

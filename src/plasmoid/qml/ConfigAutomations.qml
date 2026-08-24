// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid

KCM.SimpleKCM {
    id: root

    property var bridge: Plasmoid

    readonly property bool daemonReachable: bridge !== null && bridge.available

    // Rules belong to the daemon and change live through RulesChanged; this
    // page only displays and commands them.
    function outcomeColor(outcome) {
        if (outcome === "ok") {
            return Kirigami.Theme.positiveTextColor;
        }
        if (outcome === "partial" || outcome === "failed") {
            return Kirigami.Theme.negativeTextColor;
        }
        return Kirigami.Theme.disabledTextColor;
    }

    Connections {
        target: root.bridge

        function onDeviceOperationFailed(operation, message) {
            root.operationError = message;
            // The editor is modal: its own banner is the one the user sees.
            if ((operation === "addRule" || operation === "updateRule")
                && ruleEditor.opened) {
                ruleEditor.errorText = message;
            }
        }

        function onDeviceOperationSucceeded(operation) {
            root.operationError = "";
            ruleEditor.errorText = "";
            // The editor stays open through the round trip so a rejection
            // never loses the user's edits; success closes it.
            if (operation === "addRule" || operation === "updateRule") {
                ruleEditor.close();
            }
        }

        function onRuleRunFinished(ruleId, outcome) {
            root.runFeedback = outcome.length > 0
                ? KI18n.i18nc("@info", "Rule finished: %1", outcome)
                : "";
        }
    }

    function openRuleEditor(ruleId) {
        ruleEditor.openForEdit(root.bridge !== null ? root.bridge.ruleDocument(ruleId) : {});
    }

    property string operationError: ""
    property string runFeedback: ""

    ConfigRuleEditor {
        id: ruleEditor

        bridge: root.bridge
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaExtras.Heading {
                Layout.fillWidth: true
                level: 3
                text: KI18n.i18nc("@title:group", "Automation rules")
            }

            PlasmaExtras.ToolButton {
                objectName: "addRuleButton"

                enabled: root.daemonReachable && !(root.bridge?.busy ?? false)
                icon.name: "list-add"
                text: KI18n.i18nc("@action:button", "Add rule")
                onClicked: ruleEditor.openForAdd()
            }
        }

        Kirigami.InlineMessage {
            objectName: "daemonUnavailableMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: !root.daemonReachable
            text: KI18n.i18nc("@info", "RokuDeck cannot reach its background service, so automations cannot be shown right now.")
        }

        Kirigami.InlineMessage {
            objectName: "operationErrorMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: root.operationError.length > 0
            text: root.operationError
        }

        Kirigami.InlineMessage {
            objectName: "runFeedbackMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: root.runFeedback.length > 0
            text: root.runFeedback
        }

        PlasmaExtras.PlaceholderMessage {
            objectName: "noRulesPlaceholder"

            visible: root.daemonReachable && automationsList.count === 0
            iconName: "chronometer"
            text: KI18n.i18nc("@title", "No Automations Yet")
            explanation: KI18n.i18nc("@info", "Rules live in the RokuDeck background service. Create one from the command line and it appears here immediately.")
        }

        QQC2.Frame {
            objectName: "addRuleHintFrame"

            visible: root.daemonReachable && automationsList.count === 0
            Layout.fillWidth: true

            ColumnLayout {
                width: parent.width
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    text: KI18n.i18nc("@info", "Create your first automation with this command:")
                }

                QQC2.TextArea {
                    id: addRuleHintArea

                    objectName: "addRuleHintArea"

                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "monospace"
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    text: KI18n.i18nc("@info", "busctl --user call io.github.josephur.RokuDeck1 /io/github/josephur/RokuDeck1 io.github.josephur.RokuDeck1 AddRule s '{\"schemaVersion\":1,\"name\":\"Movie time\",\"trigger\":{\"type\":\"manual\"},\"actions\":[{\"type\":\"keypress\",\"key\":\"Home\"}]}'")
                }

                QQC2.Button {
                    objectName: "copyRuleHintButton"

                    icon.name: root.hintCopied ? "dialog-ok-apply" : "edit-copy"
                    text: root.hintCopied
                        ? KI18n.i18nc("@action:button", "Copied")
                        : KI18n.i18nc("@action:button", "Copy command")
                    onClicked: {
                        addRuleHintArea.selectAll();
                        addRuleHintArea.copy();
                        addRuleHintArea.deselect();
                        root.hintCopied = true;
                        hintCopiedTimer.restart();
                    }
                }
            }
        }

        ListView {
            id: automationsList

            objectName: "automationsList"

            Layout.fillWidth: true
            Layout.minimumHeight: count > 0 ? contentHeight : 0
            interactive: false
            spacing: Kirigami.Units.smallSpacing
            model: root.bridge !== null ? root.bridge.automations : null

            delegate: QQC2.Frame {
                id: ruleCard

                width: automationsList.width

                required property string ruleId
                required property string name
                required property bool enabled
                required property string summary
                required property string outcome
                required property string lastFired
                required property int index

                // Deleting is irreversible, so the button asks twice: first
                // press arms it, second press within a few seconds confirms.
                property bool deleteArmed: false

                Timer {
                    interval: 4000
                    running: ruleCard.deleteArmed
                    onTriggered: ruleCard.deleteArmed = false
                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Switch {
                        objectName: "ruleEnabledSwitch"

                        checked: ruleCard.enabled
                        onToggled: root.bridge.setRuleEnabled(ruleCard.ruleId, checked)
                        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Enable or disable this automation")
                        QQC2.ToolTip.visible: hovered
                    }

                    ColumnLayout {
                        id: ruleEditArea

                        objectName: "ruleEditArea"

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        TapHandler {
                            cursorShape: Qt.PointingHandCursor
                            onTapped: root.openRuleEditor(ruleCard.ruleId)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                objectName: "ruleNameLabel"

                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.bold: true
                                text: ruleCard.name
                            }

                            Rectangle {
                                objectName: "ruleOutcomeChip"

                                radius: height / 2
                                implicitWidth: outcomeLabel.implicitWidth + Kirigami.Units.largeSpacing
                                implicitHeight: Kirigami.Units.smallSpacing * 2 + outcomeLabel.implicitHeight
                                color: Qt.rgba(root.outcomeColor(ruleCard.outcome).r,
                                               root.outcomeColor(ruleCard.outcome).g,
                                               root.outcomeColor(ruleCard.outcome).b,
                                               0.15)
                                border.color: root.outcomeColor(ruleCard.outcome)
                                visible: ruleCard.outcome.length > 0

                                QQC2.Label {
                                    id: outcomeLabel

                                    anchors.centerIn: parent
                                    text: ruleCard.outcome
                                    color: root.outcomeColor(ruleCard.outcome)
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                }
                            }
                        }

                        QQC2.Label {
                            objectName: "ruleSummaryLabel"

                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Kirigami.Theme.disabledTextColor
                            text: ruleCard.summary
                        }

                        QQC2.Label {
                            objectName: "ruleLastFiredLabel"

                            visible: ruleCard.lastFired.length > 0
                            wrapMode: Text.WordWrap
                            font.pointSize: Kirigami.Theme.smallestFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                            text: KI18n.i18nc("@info", "Last ran %1", ruleCard.lastFired)
                        }
                    }

                    QQC2.ToolButton {
                        objectName: "ruleRunButton"

                        icon.name: "media-playback-start"
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: root.bridge.runRule(ruleCard.ruleId)
                        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Run now")
                        QQC2.ToolTip.visible: hovered
                    }

                    QQC2.ToolButton {
                        objectName: "ruleDeleteButton"

                        icon.name: ruleCard.deleteArmed ? "dialog-ok-apply" : "edit-delete-remove"
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: {
                            if (ruleCard.deleteArmed) {
                                root.bridge.deleteRule(ruleCard.ruleId);
                                ruleCard.deleteArmed = false;
                            } else {
                                ruleCard.deleteArmed = true;
                            }
                        }
                        QQC2.ToolTip.text: ruleCard.deleteArmed
                            ? KI18n.i18nc("@info:tooltip", "Press again to delete permanently")
                            : KI18n.i18nc("@info:tooltip", "Delete rule")
                        QQC2.ToolTip.visible: hovered
                    }
                }
            }
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

// One ordered action list: the rule's Then, or a branch inside an
// ifThenElse. Steps are plain JS objects from rulepayload.js; structural
// edits replace the array and raise structureChanged so the host revision
// rebuilds this list. Step editors load ActionStepEditor.qml by URL because
// that file loads this one for ifThenElse branches.
ColumnLayout {
    id: listRoot

    signal changed()
    signal structureChanged()

    property var bridge: null
    property var actions: []
    property int depth: 0
    property int revision: 0
    property string editingRuleId: ""

    // When set, every structural edit writes the replacement array back into
    // storeObject[storeKey], keeping the owning form state authoritative.
    property var storeObject: null
    property string storeKey: ""

    // The parser allows depth 16; input is capped lower to match conditions.
    readonly property bool canNestDeeper: listRoot.depth < 8
    readonly property bool showSteps: listRoot.revision >= 0
                                      && Array.isArray(listRoot.actions)

    spacing: Kirigami.Units.smallSpacing

    function bubble() {
        listRoot.changed();
    }

    function replaceAll(next) {
        if (listRoot.storeObject !== null && listRoot.storeKey.length > 0) {
            listRoot.storeObject[listRoot.storeKey] = next;
        }
        listRoot.actions = next;
        listRoot.structureChanged();
    }

    function append(type) {
        const blank = { type: type, key: "Home", appId: "", volume: 50,
                        muted: false, seconds: 10, id: "", cond: null,
                        thenActions: [], elseActions: [] };
        const next = listRoot.actions.slice();
        next.push(blank);
        listRoot.replaceAll(next);
    }

    function removeAt(index) {
        const next = listRoot.actions.filter((_, i) => i !== index);
        listRoot.replaceAll(next);
    }

    function move(from, offset) {
        const to = from + offset;
        if (to < 0 || to >= listRoot.actions.length) {
            return;
        }
        const next = listRoot.actions.slice();
        next.splice(to, 0, next.splice(from, 1)[0]);
        listRoot.replaceAll(next);
    }

    Repeater {
        id: stepRepeater

        model: listRoot.showSteps ? listRoot.actions : []

        delegate: ColumnLayout {
            id: stepSlot

            required property int index

            objectName: "actionStep-" + index

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    objectName: "actionMoveUp"

                    enabled: stepSlot.index > 0 && stepSlot.index < stepRepeater.count
                    icon.name: "arrow-up"
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: listRoot.move(stepSlot.index, -1)
                    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Move step earlier")
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    objectName: "actionMoveDown"

                    enabled: stepSlot.index >= 0
                             && stepSlot.index < stepRepeater.count - 1
                    icon.name: "arrow-down"
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: listRoot.move(stepSlot.index, 1)
                    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Move step later")
                    QQC2.ToolTip.visible: hovered
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.ToolButton {
                    objectName: "actionRemoveButton"

                    icon.name: "list-remove"
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: listRoot.removeAt(stepSlot.index)
                    QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Remove this action")
                    QQC2.ToolTip.visible: hovered
                }
            }

            Loader {
                id: stepLoader

                objectName: "stepLoader"

                active: true
                Layout.fillWidth: true
                source: "ActionStepEditor.qml"
                onLoaded: {
                    const editor = stepLoader.item;
                    editor.bridge = listRoot.bridge;
                    editor.node = listRoot.actions[stepSlot.index];
                    editor.depth = listRoot.depth;
                    editor.revision = listRoot.revision;
                    editor.editingRuleId = listRoot.editingRuleId;
                    editor.changed.connect(listRoot.bubble);
                    editor.structureChanged.connect(listRoot.structureChanged);
                }
            }

            Binding {
                target: stepLoader.item
                property: "revision"
                value: listRoot.revision
            }

            Kirigami.Separator {
                visible: stepSlot.index < stepRepeater.count - 1
                Layout.fillWidth: true
            }
        }
    }

    PlasmaComponents.Label {
        visible: listRoot.showSteps && listRoot.actions.length === 0
        wrapMode: Text.WordWrap
        color: Kirigami.Theme.disabledTextColor
        text: KI18n.i18nc("@info", "No actions yet; add at least one.")
    }

    QQC2.Button {
        id: addAction

        objectName: "addActionStepButton"

        enabled: listRoot.showSteps
        icon.name: "list-add"
        text: KI18n.i18nc("@action:button", "Add action")
        onClicked: addMenu.popup(addAction, 0, addAction.height)

        QQC2.Menu {
            id: addMenu

            QQC2.MenuItem {
                objectName: "addKeypressStep"

                text: KI18n.i18nc("@item:inmenu", "Press a remote button")
                onTriggered: listRoot.append("keypress")
            }

            QQC2.MenuItem {
                objectName: "addLaunchAppStep"

                text: KI18n.i18nc("@item:inmenu", "Launch an app")
                onTriggered: listRoot.append("launchApp")
            }

            QQC2.MenuItem {
                objectName: "addSetVolumeStep"

                text: KI18n.i18nc("@item:inmenu", "Set the volume")
                onTriggered: listRoot.append("setVolume")
            }

            QQC2.MenuItem {
                objectName: "addMuteStep"

                text: KI18n.i18nc("@item:inmenu", "Mute or unmute")
                onTriggered: listRoot.append("mute")
            }

            QQC2.MenuItem {
                objectName: "addDelayStep"

                text: KI18n.i18nc("@item:inmenu", "Wait a while")
                onTriggered: listRoot.append("delay")
            }

            QQC2.MenuItem {
                objectName: "addRunRuleStep"

                text: KI18n.i18nc("@item:inmenu", "Run another automation")
                onTriggered: listRoot.append("runRule")
            }

            QQC2.MenuItem {
                objectName: "addIfThenElseStep"

                enabled: listRoot.canNestDeeper
                text: KI18n.i18nc("@item:inmenu", "Decide between two branches")
                onTriggered: listRoot.append("ifThenElse")
            }
        }
    }

    PlasmaComponents.Label {
        visible: !listRoot.canNestDeeper
        wrapMode: Text.WordWrap
        color: Kirigami.Theme.disabledTextColor
        font.pointSize: Kirigami.Theme.smallFont.pointSize
        text: KI18n.i18nc("@info", "Branches nest at most 8 levels deep.")
    }
}

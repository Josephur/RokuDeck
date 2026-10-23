// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

// Public node of the extra-conditions tree: kind combo, remove affordance,
// and the node body from ConditionNodeInner. The inner file recurses back
// into this type through URL-loaded Loaders, which keeps the type graph from
// becoming a compile-time cycle.
ColumnLayout {
    id: nodeRoot

    signal changed()
    signal removed()
    signal structureChanged()

    property var bridge: null
    property var node: null
    property int depth: 0
    property int revision: 0
    property bool removable: false

    // Structural entry points, exposed for hosts and tests alike.
    readonly property alias body: nodeBody

    spacing: Kirigami.Units.smallSpacing

    function chooseKind(nextKind) {
        if (nodeRoot.node === null || nodeRoot.node === undefined) {
            return;  // no live node to edit; hosts seed one before showing this
        }
        if (nodeRoot.node.kind === nextKind) {
            return;
        }
        nodeRoot.node.kind = nextKind;
        if ((nextKind === "all" || nextKind === "any")
            && !Array.isArray(nodeRoot.node.children)) {
            nodeRoot.node.children = [];
            nodeRoot.node.child = null;
        } else if (nextKind === "not" && !Array.isArray(nodeRoot.node.children)) {
            nodeRoot.node.children = [];
        }
        // Update this node's own gates immediately: branch editors live
        // outside the page-level condition tree, so the host's refreshGates()
        // never reaches them.
        nodeRoot.refreshGates();
        nodeRoot.structureChanged();
    }

    function syncFromNode() {
        nodeBody.syncFromNode();
    }

    // The dialog assigns node after construction when editing; keep the kind
    // combo honest (the body reseeds its own gates and editors).
    onNodeChanged: {
        if (nodeRoot.node !== null) {
            for (let kind = 0; kind < kindCombo.model.length; ++kind) {
                if (kindCombo.model[kind].value === nodeRoot.node.kind) {
                    kindCombo.currentIndex = kind;
                }
            }
        }
    }

    function refreshGates() {
        nodeBody.refreshGates();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.ComboBox {
            id: kindCombo

            objectName: "conditionKindCombo"

            textRole: "label"
            model: [
                { label: KI18n.i18nc("@option condition leaf", "When…"), value: "leaf" },
                { label: KI18n.i18nc("@option condition all", "All of these"), value: "all" },
                { label: KI18n.i18nc("@option condition any", "Any of these"), value: "any" },
                { label: KI18n.i18nc("@option condition not", "None of these"), value: "not" }
            ]
            onActivated: index => nodeRoot.chooseKind(model[index].value)

            Component.onCompleted: {
                if (nodeRoot.node !== null) {
                    for (let kind = 0; kind < model.length; ++kind) {
                        if (model[kind].value === nodeRoot.node.kind) {
                            currentIndex = kind;
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.ToolButton {
            objectName: "conditionRemoveButton"

            visible: nodeRoot.removable
            icon.name: "list-remove"
            display: QQC2.AbstractButton.IconOnly
            onClicked: nodeRoot.removed()
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Remove this condition")
            QQC2.ToolTip.visible: hovered
        }
    }

    ConditionNodeInner {
        id: nodeBody

        Layout.fillWidth: true
        bridge: nodeRoot.bridge
        node: nodeRoot.node
        depth: nodeRoot.depth
        revision: nodeRoot.revision
        onChanged: nodeRoot.changed()
        onStructureChanged: nodeRoot.structureChanged()
    }
}

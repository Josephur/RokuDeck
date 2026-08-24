// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

import "rulepayload.js" as RulePayload

// Body of one conditions node (leaf form, not slot, or group list), owned by
// ConditionNodeEditor. Recursive child editors load that file back by URL:
// a static reference here would make the two files a compile-time cycle.
ColumnLayout {
    id: nodeRoot

    signal changed()
    signal structureChanged()

    property var bridge: null
    property var node: null
    property int depth: 0
    property int revision: 0

    // The parser allows depth 16; input is capped lower so group headroom
    // never turns a legitimate tree into a rejected document.
    readonly property bool canNestDeeper: depth < 12

    // Gates are plain flags driven by refreshGates(): the underlying form
    // nodes are mutable JS objects, so declarative bindings cannot observe
    // their kind changes reliably.
    property bool showLeaf: false
    property bool showNot: false
    property bool showGroup: false

    property var _childLoaders: []

    readonly property alias groupCount: childRepeater.count

    function groupEntry(index) {
        return childRepeater.itemAt(index);
    }

    // Loaded child editors of the group repeater, by index.
    function groupChildEditor(index) {
        const slot = childRepeater.itemAt(index);
        if (slot === null) {
            return null;
        }
        for (let i = 0; i < slot.children.length; ++i) {
            if (slot.children[i].objectName === "childLoader") {
                return slot.children[i].item;
            }
        }
        return null;
    }

    function refreshGates() {
        const active = node !== null && revision >= 0;
        showLeaf = active && node.kind === "leaf";
        showNot = active && node.kind === "not";
        showGroup = active && (node.kind === "all" || node.kind === "any");
        for (let i = 0; i < _childLoaders.length; ++i) {
            const loader = _childLoaders[i];
            if (loader.item !== null && loader.item.body !== undefined
                && loader.item.body !== null) {
                loader.item.revision = revision;
                loader.item.body.refreshGates();
            }
        }
    }

    function trackChild(loader) {
        _childLoaders.push(loader);
        return loader;
    }

    spacing: Kirigami.Units.smallSpacing

    function bubble() {
        nodeRoot.changed();
    }

    function addNotChild() {
        nodeRoot.node.child = RulePayload.blankLeaf();
        nodeRoot.structureChanged();
    }

    function addChild() {
        const next = nodeRoot.node.children.slice();
        next.push(RulePayload.blankLeaf());
        nodeRoot.node.children = next;
        nodeRoot.structureChanged();
    }

    function addGroup() {
        const next = nodeRoot.node.children.slice();
        next.push({ kind: "all", children: [], child: null,
                    device: "*", field: "power", op: "is", value: "on" });
        nodeRoot.node.children = next;
        nodeRoot.structureChanged();
    }

    function removeChild(childNode) {
        const next = nodeRoot.node.children.filter(child => child !== childNode);
        nodeRoot.node.children = next;
        nodeRoot.structureChanged();
    }

    function bindChild(loader, childNode, childRemovable) {
        if (loader.item === null) {
            return;
        }
        trackChild(loader);
        loader.item.bridge = nodeRoot.bridge;
        loader.item.node = childNode;
        loader.item.depth = nodeRoot.depth + 1;
        loader.item.revision = nodeRoot.revision;
        loader.item.removable = childRemovable;
        loader.item.body.refreshGates();
        loader.item.changed.connect(nodeRoot.bubble);
        loader.item.structureChanged.connect(nodeRoot.structureChanged);
        if (childRemovable) {
            loader.item.removed.connect(() => {
                const next = nodeRoot.node.children.filter(
                    child => child !== childNode);
                nodeRoot.node.children = next;
                nodeRoot.structureChanged();
            });
        }
    }

    function syncFromNode() {
        if (!nodeRoot.showLeaf) {
            return;
        }
        const choices = leafBody.deviceChoices();
        for (let choice = 0; choice < choices.length; ++choice) {
            if (choices[choice].value === nodeRoot.node.device) {
                deviceCombo.currentIndex = choice;
            }
        }
        for (let field = 0; field < RulePayload.fields.length; ++field) {
            if (RulePayload.fields[field] === nodeRoot.node.field) {
                fieldCombo.currentIndex = field;
            }
        }
        for (let op = 0; op < RulePayload.ops.length; ++op) {
            if (RulePayload.ops[op] === nodeRoot.node.op) {
                opCombo.currentIndex = op;
            }
        }
        leafValue.field = nodeRoot.node.field;
        leafValue.op = nodeRoot.node.op;
        leafValue.value = nodeRoot.node.value;
    }

    Component.onCompleted: {
        nodeRoot.refreshGates();
        if (nodeRoot.showLeaf) {
            nodeRoot.syncFromNode();
        }
    }

    Kirigami.FormLayout {
        id: leafBody

        visible: nodeRoot.showLeaf
        Layout.fillWidth: true

        function deviceChoices() {
            const choices = [{ label: KI18n.i18nc("@option any-device", "Any device"),
                               value: "*" }];
            const entries = nodeRoot.bridge !== null ? nodeRoot.bridge.deviceEntries() : [];
            for (let i = 0; i < entries.length; ++i) {
                choices.push({ label: String(entries[i].name),
                               value: String(entries[i].serial) });
            }
            return choices;
        }

        PlasmaComponents.ComboBox {
            id: deviceCombo

            objectName: "conditionDeviceCombo"

            Kirigami.FormData.label: KI18n.i18nc("@label", "On:")
            textRole: "label"
            model: leafBody.deviceChoices()
            onActivated: index => {
                nodeRoot.node.device = model[index].value;
                nodeRoot.bubble();
            }
        }

        PlasmaComponents.ComboBox {
            id: fieldCombo

            objectName: "conditionFieldCombo"

            Kirigami.FormData.label: KI18n.i18nc("@label", "Field:")
            model: RulePayload.fields
            onActivated: index => {
                nodeRoot.node.field = model[index];
                nodeRoot.node.value =
                    RulePayload.canonicalValue(model[index], nodeRoot.node.op,
                                               nodeRoot.node.value);
                leafValue.field = model[index];
                leafValue.value = nodeRoot.node.value;
                nodeRoot.bubble();
            }
        }

        PlasmaComponents.ComboBox {
            id: opCombo

            objectName: "conditionOpCombo"

            Kirigami.FormData.label: KI18n.i18nc("@label", "Comparison:")
            model: RulePayload.ops
            onActivated: index => {
                nodeRoot.node.op = model[index];
                leafValue.op = model[index];
                nodeRoot.bubble();
            }
        }

        RuleValueInput {
            id: leafValue

            objectName: "conditionValue"

            onValueEdited: value => {
                nodeRoot.node.value = value;
                nodeRoot.bubble();
            }
        }
    }

    ColumnLayout {
        visible: nodeRoot.showNot
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        ColumnLayout {
            objectName: "conditionNotChild"

            visible: nodeRoot.showNot && nodeRoot.node.child !== null
            Layout.fillWidth: true
            spacing: 0

            Loader {
                id: notChildLoader

                objectName: "notChildLoader"

                active: nodeRoot.showNot && nodeRoot.node.child !== null
                Layout.fillWidth: true
                source: "ConditionNodeEditor.qml"
                onLoaded: nodeRoot.bindChild(notChildLoader, nodeRoot.node.child, false)
            }

            Binding {
                target: notChildLoader.item
                property: "revision"
                value: nodeRoot.revision
            }
        }

        QQC2.Button {
            objectName: "conditionAddNotChildButton"

            visible: nodeRoot.showNot && nodeRoot.node.child === null
            enabled: nodeRoot.canNestDeeper
            icon.name: "list-add"
            text: KI18n.i18nc("@action:button", "Add inverted condition")
            onClicked: nodeRoot.addNotChild()
        }

        QQC2.Label {
            visible: nodeRoot.showNot && nodeRoot.node.child === null
                     && !nodeRoot.canNestDeeper
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            text: KI18n.i18nc("@info", "Conditions nest at most 12 levels deep.")
        }
    }

    ColumnLayout {
        visible: nodeRoot.showGroup
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            id: childRepeater

            model: nodeRoot.showGroup && nodeRoot.revision >= 0
                   && Array.isArray(nodeRoot.node.children)
                   ? nodeRoot.node.children : []

            delegate: ColumnLayout {
                id: childSlot

                // Index-only on purpose: injected modelData for plain JS
                // arrays crosses a QVariantMap boundary and would hand this
                // editor a copy instead of the live node.
                required property int index

                objectName: "conditionChild-" + index

                Layout.fillWidth: true
                spacing: 0

                Loader {
                    id: childLoader

                    objectName: "childLoader"

                    active: true
                    Layout.fillWidth: true
                    source: "ConditionNodeEditor.qml"
                    onLoaded: nodeRoot.bindChild(
                                  childLoader,
                                  nodeRoot.node.children[childSlot.index], true)
                }

                Binding {
                    target: childLoader.item
                    property: "revision"
                    value: nodeRoot.revision
                }
            }
        }

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                objectName: "conditionAddChildButton"

                enabled: nodeRoot.canNestDeeper
                icon.name: "list-add"
                text: KI18n.i18nc("@action:button", "Add condition")
                onClicked: {
                    const next = nodeRoot.node.children.slice();
                    next.push(RulePayload.blankLeaf());
                    nodeRoot.node.children = next;
                    nodeRoot.structureChanged();
                }
            }

            QQC2.Button {
                objectName: "conditionAddGroupButton"

                enabled: nodeRoot.canNestDeeper
                icon.name: "folder-new"
                text: KI18n.i18nc("@action:button", "Add group")
                onClicked: {
                    const next = nodeRoot.node.children.slice();
                    next.push({ kind: "all", children: [], child: null,
                                device: "*", field: "power", op: "is", value: "on" });
                    nodeRoot.node.children = next;
                    nodeRoot.structureChanged();
                }
            }
        }

        QQC2.Label {
            visible: !nodeRoot.canNestDeeper
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            text: KI18n.i18nc("@info", "Conditions nest at most 12 levels deep.")
        }
    }
}

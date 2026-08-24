// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

import "rulepayload.js" as RulePayload

// Typed editor for one predicate value, per field/op vocabulary. The daemon
// compares values opaquely; these editors simply keep users inside the
// canonical shapes (enums, booleans, 0..100 volume, two-number between).
// Edits are reported through valueEdited; owners write them back into their
// form state and re-assign value when reloading.
RowLayout {
    id: root

    signal valueEdited(var value)

    property string field: "power"
    property string op: "is"
    property var value: "on"

    readonly property bool isBetween: root.op === "between"
    readonly property var canonicalValues: root.field === "power"
        ? RulePayload.powerValues
        : root.field === "screensaver" ? RulePayload.screensaverValues
        : RulePayload.healthValues
    readonly property bool enumField: root.field === "power"
        || root.field === "screensaver" || root.field === "health"

    spacing: Kirigami.Units.smallSpacing

    onFieldChanged: {
        if (!root.isBetween) {
            root.value = RulePayload.canonicalValue(root.field, root.op, root.value);
        }
        root.refreshEditors();
    }

    onOpChanged: {
        if (root.isBetween || root.field === "volume") {
            root.value = RulePayload.canonicalValue(root.field, root.op, root.value);
        }
        root.refreshEditors();
    }

    // Re-seeds every editor from value; declarative bindings would die on the
    // first user interaction, so reloads are imperative.
    function refreshEditors() {
        if (enumFieldCombo.visible) {
            enumFieldCombo.currentIndex =
                Math.max(0, enumFieldCombo.model.indexOf(String(root.value)));
        }
        if (mutedCombo.visible) {
            mutedCombo.currentIndex = root.value === true ? 0 : 1;
        }
        if (volumeSpin.visible) {
            const number = Number(root.value);
            volumeSpin.value = isNaN(number) ? 0
                : Math.min(100, Math.max(0, Math.round(number)));
        }
        if (appText.visible) {
            appText.text = String(root.value === undefined || root.value === null
                                  ? "" : root.value);
        }
        if (betweenRow.visible) {
            const pair = Array.isArray(root.value) && root.value.length === 2
                ? root.value : [0, 100];
            betweenLowSpin.value = Number(pair[0]) || 0;
            betweenHighSpin.value = Number(pair[1]) || 0;
        }
    }

    PlasmaComponents.ComboBox {
        id: enumFieldCombo

        objectName: root.objectName.length > 0 ? root.objectName + "Combo" : ""

        visible: !root.isBetween && root.enumField
        Layout.fillWidth: true
        model: root.canonicalValues
        onActivated: index => {
            root.value = model[index];
            root.valueEdited(root.value);
        }
    }

    PlasmaComponents.ComboBox {
        id: mutedCombo

        objectName: root.objectName.length > 0 ? root.objectName + "Combo" : ""

        visible: !root.isBetween && root.field === "muted"
        Layout.fillWidth: true
        textRole: "label"
        model: [
            { label: KI18n.i18nc("@option", "Muted"), flag: true },
            { label: KI18n.i18nc("@option", "Unmuted"), flag: false }
        ]
        onActivated: index => {
            root.value = model[index].flag;
            root.valueEdited(root.value);
        }
    }

    QQC2.SpinBox {
        id: volumeSpin

        objectName: root.objectName.length > 0 ? root.objectName + "Spin" : ""

        visible: !root.isBetween && root.field === "volume"
        Layout.fillWidth: true
        from: 0
        to: 100
        editable: true
        onValueModified: {
            root.value = value;
            root.valueEdited(value);
        }
    }

    QQC2.TextField {
        id: appText

        objectName: root.objectName.length > 0 ? root.objectName + "Text" : ""

        visible: !root.isBetween && root.field === "app"
        Layout.fillWidth: true
        placeholderText: KI18n.i18nc("@info:placeholder", "App id to compare against")
        selectByMouse: true
        onTextEdited: {
            root.value = text;
            root.valueEdited(text);
        }
    }

    RowLayout {
        id: betweenRow

        objectName: root.objectName.length > 0 ? root.objectName + "BetweenPair" : ""

        visible: root.isBetween
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label spinbox range low bound", "From")
        }

        QQC2.SpinBox {
            id: betweenLowSpin

            objectName: root.objectName.length > 0 ? root.objectName + "LowSpin" : ""

            from: 0
            to: 10000
            editable: true
            onValueModified: {
                const pair = Array.isArray(root.value) ? root.value.slice() : [0, 100];
                pair[0] = value;
                root.value = pair;
                root.valueEdited(pair);
            }
        }

        QQC2.Label {
            text: KI18n.i18nc("@label spinbox range high bound", "to")
        }

        QQC2.SpinBox {
            id: betweenHighSpin

            objectName: root.objectName.length > 0 ? root.objectName + "HighSpin" : ""

            from: 0
            to: 10000
            editable: true
            onValueModified: {
                const pair = Array.isArray(root.value) ? root.value.slice() : [0, 100];
                pair[1] = value;
                root.value = pair;
                root.valueEdited(pair);
            }
        }
    }

    Component.onCompleted: root.refreshEditors()
}

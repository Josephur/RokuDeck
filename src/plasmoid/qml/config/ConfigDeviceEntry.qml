// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

ColumnLayout {
    id: root

    required property string serial
    required property string displayName
    required property string address
    required property bool allowAutomations
    required property bool wakeOnWlan
    required property string refreshPolicy
    required property var capabilities
    required property bool shownInWidget
    required property bool isDefault
    required property bool canMoveUp
    required property bool canMoveDown

    signal shownInWidgetToggled(bool selected)
    signal madeDefault
    signal renamed(string name)
    signal automationsToggled(bool allowed)
    signal wakeToggled(bool wake)
    signal refreshPolicyChosen(string policy)
    signal identifyRequested
    signal removeRequested
    signal moveUpRequested
    signal moveDownRequested

    readonly property var refreshPolicies: ["responsive", "balanced", "battery"]
    readonly property bool supportsIdentify: (capabilities ?? []).indexOf("supports-find-remote") >= 0

    function policyLabel(policy) {
        switch (policy) {
        case "responsive":
            return KI18n.i18nc("@item:inlistbox refresh policy", "Responsive");
        case "battery":
            return KI18n.i18nc("@item:inlistbox refresh policy", "Battery conscious");
        default:
            return KI18n.i18nc("@item:inlistbox refresh policy", "Balanced");
        }
    }

    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.CheckBox {
            objectName: "showInWidgetBox"
            checked: root.shownInWidget
            text: KI18n.i18nc("@option:check", "Show in this widget")
            onToggled: root.shownInWidgetToggled(checked)
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.ToolButton {
            objectName: "moveUpButton"
            enabled: root.canMoveUp
            display: QQC2.AbstractButton.IconOnly
            icon.name: "arrow-up"
            text: KI18n.i18nc("@action:button", "Move %1 up", root.displayName)
            Accessible.name: text
            onClicked: root.moveUpRequested()
        }

        QQC2.ToolButton {
            objectName: "moveDownButton"
            enabled: root.canMoveDown
            display: QQC2.AbstractButton.IconOnly
            icon.name: "arrow-down"
            text: KI18n.i18nc("@action:button", "Move %1 down", root.displayName)
            Accessible.name: text
            onClicked: root.moveDownRequested()
        }

        QQC2.ToolButton {
            objectName: "identifyButton"
            // Only offered when the device told us it can do it, so the button
            // never promises something the Roku will refuse.
            enabled: root.supportsIdentify
            display: QQC2.AbstractButton.IconOnly
            icon.name: "find-location"
            text: KI18n.i18nc("@action:button", "Identify %1", root.displayName)
            Accessible.name: text
            QQC2.ToolTip.text: root.supportsIdentify
                ? text
                : KI18n.i18nc("@info:tooltip", "This Roku cannot chime for its remote.")
            QQC2.ToolTip.visible: hovered
            onClicked: root.identifyRequested()
        }

        QQC2.ToolButton {
            objectName: "removeButton"
            display: QQC2.AbstractButton.IconOnly
            icon.name: "list-remove"
            text: KI18n.i18nc("@action:button", "Remove %1", root.displayName)
            Accessible.name: text
            onClicked: root.removeRequested()
        }
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.TextField {
            id: nameField

            objectName: "nameField"
            Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Name:")
            text: root.displayName
            placeholderText: KI18n.i18nc("@info:placeholder", "Unnamed Roku device")
            onEditingFinished: {
                if (text !== root.displayName) {
                    root.renamed(text);
                }
            }
        }

        PlasmaComponents.Label {
            objectName: "addressLabel"
            Kirigami.FormData.label: KI18n.i18nc("@label", "Address:")
            text: root.address
        }

        PlasmaComponents.Label {
            objectName: "serialLabel"
            Kirigami.FormData.label: KI18n.i18nc("@label", "Serial number:")
            text: root.serial
        }

        QQC2.RadioButton {
            objectName: "defaultDeviceButton"
            Kirigami.FormData.label: KI18n.i18nc("@label", "Opens by default:")
            enabled: root.shownInWidget
            checked: root.isDefault
            text: KI18n.i18nc("@option:radio", "Select this device when the widget opens")
            onToggled: {
                if (checked) {
                    root.madeDefault();
                }
            }
        }

        QQC2.CheckBox {
            objectName: "allowAutomationsBox"
            Kirigami.FormData.label: KI18n.i18nc("@label", "Automations:")
            checked: root.allowAutomations
            text: KI18n.i18nc("@option:check", "Let automations control this device")
            onToggled: root.automationsToggled(checked)
        }

        QQC2.CheckBox {
            objectName: "wakeOnWlanBox"
            checked: root.wakeOnWlan
            text: KI18n.i18nc("@option:check", "May be woken over the network")
            onToggled: root.wakeToggled(checked)
        }

        QQC2.ComboBox {
            objectName: "refreshPolicyBox"
            Kirigami.FormData.label: KI18n.i18nc("@label:listbox", "Refresh policy:")
            model: root.refreshPolicies.map(policy => root.policyLabel(policy))
            currentIndex: Math.max(0, root.refreshPolicies.indexOf(root.refreshPolicy))
            onActivated: index => root.refreshPolicyChosen(root.refreshPolicies[index])
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }
}

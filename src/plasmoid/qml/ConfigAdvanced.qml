// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.plasmoid

KCM.SimpleKCM {
    id: root

    property var bridge: Plasmoid

    readonly property bool daemonReachable: bridge !== null && bridge.available

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            PlasmaComponents.Label {
                objectName: "daemonStatusLabel"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Background service:")
                text: root.daemonReachable
                    ? KI18n.i18nc("@info", "Running")
                    : KI18n.i18nc("@info", "Not reachable")
            }

            PlasmaComponents.Label {
                objectName: "lastErrorLabel"
                Kirigami.FormData.label: KI18n.i18nc("@label", "Last problem:")
                visible: (root.bridge?.lastError ?? "").length > 0
                wrapMode: Text.WordWrap
                text: root.bridge?.lastError ?? ""
            }

            QQC2.Button {
                objectName: "refreshButton"
                enabled: root.daemonReachable
                icon.name: "view-refresh"
                text: KI18n.i18nc("@action:button", "Refresh device information now")
                onClicked: root.bridge.refresh()
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Automation rules and the device-wide volume ceiling arrive with the
        // automation backend. Saying so is better than showing settings that
        // nothing would enforce yet.
        Kirigami.InlineMessage {
            objectName: "automationPendingMessage"

            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: KI18n.i18nc("@info", "Automation rules and the per-device volume limit are not available yet. RokuDeck is in alpha and will not act on rules until that backend ships.")
        }
    }
}

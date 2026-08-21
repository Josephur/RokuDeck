// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

PlasmaComponents.ItemDelegate {
    id: root

    required property string serial
    required property string displayName
    required property string health
    required property string power
    required property bool stale
    required property bool current

    signal selected

    readonly property string friendlyName: displayName.length > 0
        ? displayName
        : KI18n.i18n("Unnamed Roku device")

    activeFocusOnTab: false
    checked: current
    checkable: true
    hoverEnabled: true
    text: friendlyName
    Accessible.name: friendlyName
    Accessible.description: KI18n.i18n("%1, %2", friendlyName, statusBadge.Accessible.description)
    onClicked: selected()

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
            Layout.preferredHeight: width
            source: "video-television"
        }

        PlasmaComponents.Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: root.friendlyName
        }

        StatusBadge {
            id: statusBadge

            health: root.health
            power: root.power
            stale: root.stale
            showText: false
        }
    }
}

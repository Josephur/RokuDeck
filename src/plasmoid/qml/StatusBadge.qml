// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.ki18n
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents

Item {
    id: root

    required property string health
    required property string power
    required property bool stale
    property bool showText: true

    readonly property string normalizedHealth: health.toLowerCase()
    readonly property string normalizedPower: power.toLowerCase()
    readonly property string statusKey: {
        if (stale) {
            return "stale";
        }
        if (normalizedHealth === "offline") {
            return "offline";
        }
        if (normalizedHealth === "degraded") {
            return "degraded";
        }
        if (normalizedHealth === "standby" || normalizedPower === "standby") {
            return "standby";
        }
        return normalizedHealth === "online" ? "online" : "degraded";
    }
    readonly property bool errorState: ["stale", "offline", "degraded"].includes(statusKey)
    readonly property string statusText: {
        switch (statusKey) {
        case "standby":
            return KI18n.i18n("Standby");
        case "degraded":
            return KI18n.i18n("Degraded");
        case "offline":
            return KI18n.i18n("Offline");
        case "stale":
            return KI18n.i18n("Status out of date");
        default:
            return KI18n.i18n("Online");
        }
    }
    readonly property string statusDescription: {
        switch (statusKey) {
        case "standby":
            return KI18n.i18n("The Roku device is reachable and in standby");
        case "degraded":
            return KI18n.i18n("Some Roku status information is unavailable");
        case "offline":
            return KI18n.i18n("The Roku device is offline");
        case "stale":
            return KI18n.i18n("The last Roku status update is out of date");
        default:
            return KI18n.i18n("The Roku device is online");
        }
    }
    readonly property string iconName: {
        switch (statusKey) {
        case "standby":
            return "system-suspend";
        case "degraded":
            return "data-warning";
        case "offline":
            return "network-disconnect";
        case "stale":
            return "view-refresh";
        default:
            return "network-connect";
        }
    }

    implicitWidth: badgeLayout.implicitWidth
    implicitHeight: badgeLayout.implicitHeight
    Accessible.role: Accessible.StaticText
    Accessible.name: statusText
    Accessible.description: statusDescription

    RowLayout {
        id: badgeLayout

        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: width
            source: root.iconName
        }

        PlasmaComponents.Label {
            visible: root.showText
            text: root.statusText
            font.pointSize: Kirigami.Theme.smallFont.pointSize
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.ki18n
import org.kde.kirigami as Kirigami

// A tiny hand-painted television whose screen carries the device state:
// green online, amber unknown/degraded/stale, red offline, and a dimmed
// blue-gray for standby. Painting it ourselves keeps the screen exactly
// where a real screen would be, independent of any icon theme.
Item {
    id: root

    property string health: "offline"
    property string power: "unknown"
    property bool stale: false

    readonly property string statusKey: {
        if (stale) {
            return "stale";
        }
        const normalized = health.toLowerCase();
        if (normalized === "offline") {
            return "offline";
        }
        if (normalized === "degraded" || normalized === "unknown") {
            return "degraded";
        }
        if (normalized === "standby" || power.toLowerCase() === "standby") {
            return "standby";
        }
        return normalized === "online" ? "online" : "degraded";
    }
    readonly property color statusColor: {
        switch (statusKey) {
        case "online":
            return "#26a269";
        case "standby":
            return "#5d7285";
        case "offline":
            return "#c01c28";
        default:
            return "#e9ad0c";
        }
    }
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

    implicitWidth: Kirigami.Units.iconSizes.smallMedium
    implicitHeight: implicitWidth
    Accessible.role: Accessible.Indicator
    Accessible.name: statusText
    Accessible.description: statusText

    // A mid gray that stays visible on both light and dark panels.
    readonly property color bezelColor: "#6e7073"

    Rectangle {
        id: casing

        anchors {
            top: parent.top
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width
        height: parent.height * 0.78
        radius: width * 0.12
        color: root.bezelColor

        Rectangle {
            objectName: "tvScreen"

            anchors.fill: parent
            anchors.margins: parent.width * 0.09
            radius: width * 0.08
            color: root.statusColor
        }
    }

    Rectangle {
        anchors {
            top: casing.bottom
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width * 0.3
        height: parent.height * 0.1
        color: root.bezelColor
    }

    Rectangle {
        anchors {
            top: casing.bottom
            topMargin: parent.height * 0.1
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width * 0.52
        height: parent.height * 0.07
        radius: height / 2
        color: root.bezelColor
    }
}

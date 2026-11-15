// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18nc("@title", "Devices")
        icon: "video-television"
        source: "ConfigDevices.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Display")
        icon: "preferences-desktop-display"
        source: "ConfigDisplay.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Controls")
        icon: "input-gamepad"
        source: "ConfigControls.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Automations")
        icon: "chronometer"
        source: "ConfigAutomations.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Peers")
        icon: "network-wireless"
        source: "ConfigPeers.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Advanced")
        icon: "configure"
        source: "ConfigAdvanced.qml"
    }
}

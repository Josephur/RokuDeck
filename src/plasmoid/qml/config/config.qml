// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18nc("@title", "Devices")
        icon: "video-television"
        source: "config/ConfigDevices.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Display")
        icon: "preferences-desktop-display"
        source: "config/ConfigDisplay.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Controls")
        icon: "input-gamepad"
        source: "config/ConfigControls.qml"
    }
    ConfigCategory {
        name: i18nc("@title", "Advanced")
        icon: "configure"
        source: "config/ConfigAdvanced.qml"
    }
}

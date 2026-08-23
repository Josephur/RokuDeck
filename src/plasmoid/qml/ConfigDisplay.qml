// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.ki18n
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: root

    // Plasma lights the Apply button from cfg_ property diffs; emitting this
    // documented signal additionally forces it on for every user mutation, so
    // a missed diff can never hide unsaved changes.
    signal configurationChanged

    property var cfg_visibleInformation: []
    property string cfg_compactMode: "adaptive"

    function shows(key) {
        return cfg_visibleInformation.indexOf(key) >= 0;
    }

    function setShown(key, shown) {
        const next = cfg_visibleInformation.filter(entry => entry !== key);
        if (shown) {
            next.push(key);
        }
        cfg_visibleInformation = next;
        configurationChanged();
    }

    Kirigami.FormLayout {
        QQC2.CheckBox {
            objectName: "showPanelNameBox"
            Kirigami.FormData.label: KI18n.i18nc("@title:group", "In the panel show:")
            checked: root.cfg_compactMode !== "iconOnly"
            text: KI18n.i18nc("@option:check", "Device name")
            onToggled: {
                root.cfg_compactMode = checked ? "adaptive" : "iconOnly";
                root.configurationChanged();
            }
        }

        Item {
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            objectName: "showStatusBox"
            Kirigami.FormData.label: KI18n.i18nc("@title:group", "When expanded show:")
            checked: root.shows("status")
            text: KI18n.i18nc("@option:check", "Connection status")
            onToggled: root.setShown("status", checked)
        }

        QQC2.CheckBox {
            objectName: "showActiveAppBox"
            checked: root.shows("activeApp")
            text: KI18n.i18nc("@option:check", "What is playing")
            onToggled: root.setShown("activeApp", checked)
        }

        QQC2.CheckBox {
            objectName: "showVolumeBox"
            checked: root.shows("volume")
            text: KI18n.i18nc("@option:check", "Volume level")
            onToggled: root.setShown("volume", checked)
        }

        QQC2.CheckBox {
            objectName: "showScreensaverBox"
            checked: root.shows("screensaver")
            text: KI18n.i18nc("@option:check", "Screen saver state")
            onToggled: root.setShown("screensaver", checked)
        }

        QQC2.CheckBox {
            objectName: "showAddressBox"
            checked: root.shows("address")
            text: KI18n.i18nc("@option:check", "Network address")
            onToggled: root.setShown("address", checked)
        }
    }
}

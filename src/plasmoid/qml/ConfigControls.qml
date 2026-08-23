// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kcmutils as KCM
import org.kde.ki18n
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    id: root

    // See ConfigDisplay: force the Apply button on every mutation.
    signal configurationChanged

    property var cfg_visibleControls: []

    function offers(key) {
        return cfg_visibleControls.indexOf(key) >= 0;
    }

    function setOffered(key, offered) {
        const next = cfg_visibleControls.filter(entry => entry !== key);
        if (offered) {
            next.push(key);
        }
        cfg_visibleControls = next;
        configurationChanged();
    }

    Kirigami.FormLayout {
        QQC2.CheckBox {
            objectName: "powerControlBox"
            Kirigami.FormData.label: KI18n.i18nc("@title:group", "Offer these controls:")
            checked: root.offers("power")
            text: KI18n.i18nc("@option:check", "Power and standby")
            onToggled: root.setOffered("power", checked)
        }

        QQC2.CheckBox {
            objectName: "volumeControlBox"
            checked: root.offers("volume")
            text: KI18n.i18nc("@option:check", "Volume up and down")
            onToggled: root.setOffered("volume", checked)
        }

        QQC2.CheckBox {
            objectName: "muteControlBox"
            checked: root.offers("mute")
            text: KI18n.i18nc("@option:check", "Mute")
            onToggled: root.setOffered("mute", checked)
        }

        QQC2.CheckBox {
            objectName: "navigationControlBox"
            checked: root.offers("navigation")
            text: KI18n.i18nc("@option:check", "Direction pad, back and home")
            onToggled: root.setOffered("navigation", checked)
        }

        QQC2.CheckBox {
            objectName: "playbackControlBox"
            checked: root.offers("playback")
            text: KI18n.i18nc("@option:check", "Play and pause")
            onToggled: root.setOffered("playback", checked)
        }

        // A control the Roku itself does not report as supported stays hidden
        // whatever is ticked here, so the widget never offers a button that
        // cannot work on the selected device.
        QQC2.Label {
            objectName: "capabilityNote"
            Kirigami.FormData.label: ""
            wrapMode: Text.WordWrap
            text: KI18n.i18nc("@info", "A control only appears when the selected Roku reports that it supports it.")
        }
    }
}

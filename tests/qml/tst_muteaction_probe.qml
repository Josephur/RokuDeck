// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtTest

import "../../src/plasmoid/qml" as RokuDeck

TestCase {
    id: testCase

    width: 500
    height: 600
    name: "MuteActionProbe"
    when: windowShown

    RokuDeck.ActionStepEditor {
        id: step

        anchors.centerIn: parent
        node: ({ type: "mute", muted: true })
    }

    function findItem(rootItem, name) {
        if (rootItem.objectName === name) {
            return rootItem;
        }
        for (let i = 0; i < rootItem.children.length; ++i) {
            const found = findItem(rootItem.children[i], name);
            if (found !== null) {
                return found;
            }
        }
        return null;
    }

    function test_muteComboOffersBothDirections() {
        const combo = findItem(step, "muteChoiceCombo");
        verify(combo !== null, "mute combo must exist");
        compare(combo.count, 2);
        compare(combo.model.length, 2);
        compare(combo.model[0].label, "Muted");
        compare(combo.model[1].label, "Unmuted");
        compare(combo.currentIndex, 0);
        compare(combo.currentText, "Muted");
    }
}

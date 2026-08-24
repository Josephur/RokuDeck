// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace RokuDeck {

struct DeviceRecord {
    QString serial;
    QString displayName;
    QString address;
    // Daemon-owned user preferences. They belong here rather than in per-widget
    // configuration because every widget and automation shares one device.
    // Set once the user types a name of their own. Discovery and polling both
    // report the name the Roku advertises, and would otherwise undo a rename
    // on the next poll.
    bool nameIsUserChosen = false;
    bool allowAutomations = true;
    bool wakeOnWlan = false;
    QString refreshPolicy = QStringLiteral("balanced");
    // Always-on automation policy: relative walks and absolute setVolume
    // targets never push the volume above this. 100 behaves as unrestricted.
    int volumeCeiling = 100;
    // Presentation order the user chose; ties fall back to serial so the list
    // is always deterministic.
    int sortIndex = 0;
};

class DeviceRegistry final {
public:
    void upsert(const DeviceRecord &device);
    bool remove(const QString &serial);
    [[nodiscard]] QList<DeviceRecord> devices() const;

private:
    QMap<QString, DeviceRecord> m_devicesBySerial;
};

} // namespace RokuDeck

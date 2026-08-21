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
    bool allowAutomations = true;
    bool wakeOnWlan = false;
    QString refreshPolicy = QStringLiteral("balanced");
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

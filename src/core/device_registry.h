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

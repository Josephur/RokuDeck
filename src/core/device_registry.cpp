// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_registry.h"

#include <algorithm>

namespace RokuDeck {

void DeviceRegistry::upsert(const DeviceRecord &device)
{
    if (!device.serial.isEmpty()) {
        m_devicesBySerial.insert(device.serial, device);
    }
}

bool DeviceRegistry::remove(const QString &serial)
{
    return m_devicesBySerial.remove(serial) > 0;
}

QList<DeviceRecord> DeviceRegistry::devices() const
{
    QList<DeviceRecord> ordered = m_devicesBySerial.values();
    std::ranges::stable_sort(ordered, [](const DeviceRecord &left, const DeviceRecord &right) {
        if (left.sortIndex != right.sortIndex) {
            return left.sortIndex < right.sortIndex;
        }
        return left.serial < right.serial;
    });
    return ordered;
}

} // namespace RokuDeck

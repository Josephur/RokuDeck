// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_registry.h"

namespace RokuDeck {

void DeviceRegistry::upsert(const DeviceRecord &device)
{
    if (!device.serial.isEmpty()) {
        m_devicesBySerial.insert(device.serial, device);
    }
}

QList<DeviceRecord> DeviceRegistry::devices() const
{
    return m_devicesBySerial.values();
}

} // namespace RokuDeck

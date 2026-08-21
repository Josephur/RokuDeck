// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_store.h"

#include <KConfigGroup>

#include <algorithm>
#include <utility>

namespace RokuDeck {

ConfigStore::ConfigStore(KSharedConfig::Ptr config) : m_config(std::move(config)) {
    Q_ASSERT(m_config);
}

QList<DeviceRecord> ConfigStore::loadDevices() const {
    const KConfigGroup devicesGroup(m_config, QStringLiteral("Devices"));
    QList<DeviceRecord> devices;
    const QStringList groups = devicesGroup.groupList();
    devices.reserve(groups.size());
    for (const QString& groupName : groups) {
        const KConfigGroup deviceGroup(&devicesGroup, groupName);
        DeviceRecord device;
        device.serial = deviceGroup.readEntry("Serial", QString());
        device.displayName = deviceGroup.readEntry("DisplayName", QString());
        device.address = deviceGroup.readEntry("Address", QString());
        device.allowAutomations = deviceGroup.readEntry("AllowAutomations", true);
        device.wakeOnWlan = deviceGroup.readEntry("WakeOnWlan", false);
        device.refreshPolicy =
            deviceGroup.readEntry("RefreshPolicy", QStringLiteral("balanced"));
        // A file written before ordering existed has no SortIndex; falling back
        // to the group position keeps those devices in their recorded order.
        device.sortIndex = deviceGroup.readEntry("SortIndex", static_cast<int>(devices.size()));
        if (!device.serial.isEmpty()) {
            devices.append(std::move(device));
        }
    }
    std::ranges::stable_sort(devices, [](const DeviceRecord& left, const DeviceRecord& right) {
        if (left.sortIndex != right.sortIndex) {
            return left.sortIndex < right.sortIndex;
        }
        return left.serial < right.serial;
    });
    return devices;
}

void ConfigStore::saveDevices(const QList<DeviceRecord>& devices) {
    m_config->deleteGroup(QStringLiteral("Devices"));
    KConfigGroup devicesGroup(m_config, QStringLiteral("Devices"));
    for (qsizetype index = 0; index < devices.size(); ++index) {
        const DeviceRecord& device = devices.at(index);
        KConfigGroup deviceGroup(&devicesGroup, QStringLiteral("Device%1").arg(index));
        deviceGroup.writeEntry("Serial", device.serial);
        deviceGroup.writeEntry("DisplayName", device.displayName);
        deviceGroup.writeEntry("Address", device.address);
        deviceGroup.writeEntry("AllowAutomations", device.allowAutomations);
        deviceGroup.writeEntry("WakeOnWlan", device.wakeOnWlan);
        deviceGroup.writeEntry("RefreshPolicy", device.refreshPolicy);
        deviceGroup.writeEntry("SortIndex", device.sortIndex);
    }
    m_config->sync();
}

} // namespace RokuDeck

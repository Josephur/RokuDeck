// SPDX-License-Identifier: GPL-3.0-or-later

#include "devicesmodel.h"

#include <QVariant>

#include <utility>

namespace RokuDeck {
namespace {

template <typename T>
void updateRole(T& current, const T& replacement, int role, QList<int>& changedRoles) {
    if (current == replacement) {
        return;
    }
    current = replacement;
    changedRoles.append(role);
}

QList<DeviceDescription> normalizedDevices(const QList<DeviceDescription>& devices) {
    QList<DeviceDescription> normalized;
    QSet<QString> serials;
    normalized.reserve(devices.size());
    for (const DeviceDescription& device : devices) {
        if (device.serial.isEmpty() || serials.contains(device.serial)) {
            continue;
        }
        normalized.append(device);
        serials.insert(device.serial);
    }
    return normalized;
}

} // namespace

DevicesModel::DevicesModel(QObject* parent) : QAbstractListModel(parent) {}

int DevicesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_devices.size());
}

QVariant DevicesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.column() != 0 || index.row() < 0 ||
        index.row() >= m_devices.size()) {
        return {};
    }

    const DeviceItem& device = m_devices.at(index.row());
    switch (role) {
    case SerialRole:
        return device.serial;
    case NameRole:
        return device.name;
    case AddressRole:
        return device.address;
    case HealthRole:
        return device.health;
    case PowerRole:
        return device.power;
    case ScreensaverRole:
        return device.screensaver;
    case ActiveLabelRole:
        return device.activeLabel;
    case VolumeRole:
        return device.volume ? QVariant(*device.volume) : QVariant();
    case MutedRole:
        return device.muted ? QVariant(*device.muted) : QVariant();
    case CapabilitiesRole:
        return device.capabilities;
    case StaleRole:
        return device.stale;
    case AllowAutomationsRole:
        return device.allowAutomations;
    case WakeOnWlanRole:
        return device.wakeOnWlan;
    case RefreshPolicyRole:
        return device.refreshPolicy;
    case VolumeCeilingRole:
        return device.volumeCeiling;
    case PendingCommandRole:
        return !device.pendingRequests.isEmpty();
    default:
        return {};
    }
}

QHash<int, QByteArray> DevicesModel::roleNames() const {
    return {{SerialRole, "serial"},
            {NameRole, "name"},
            {AddressRole, "address"},
            {HealthRole, "health"},
            {PowerRole, "power"},
            {ScreensaverRole, "screensaver"},
            {ActiveLabelRole, "activeLabel"},
            {VolumeRole, "volume"},
            {MutedRole, "muted"},
            {CapabilitiesRole, "capabilities"},
            {StaleRole, "stale"},
            {AllowAutomationsRole, "allowAutomations"},
            {WakeOnWlanRole, "wakeOnWlan"},
            {RefreshPolicyRole, "refreshPolicy"},
            {VolumeCeilingRole, "volumeCeiling"},
            {PendingCommandRole, "pendingCommand"}};
}

void DevicesModel::replaceDevices(const QList<DeviceDescription>& devices) {
    const QList<DeviceDescription> normalized = normalizedDevices(devices);

    for (qsizetype destination = 0; destination < normalized.size(); ++destination) {
        const DeviceDescription& description = normalized.at(destination);
        qsizetype source = indexOfSerial(description.serial);
        if (source < 0) {
            beginInsertRows({}, static_cast<int>(destination), static_cast<int>(destination));
            DeviceItem item;
            item.serial = description.serial;
            item.name = description.name;
            item.address = description.address;
            item.allowAutomations = description.allowAutomations;
            item.wakeOnWlan = description.wakeOnWlan;
            item.refreshPolicy = description.refreshPolicy;
            item.volumeCeiling = description.volumeCeiling;
            m_devices.insert(destination, item);
            endInsertRows();
            source = destination;
        } else if (source != destination) {
            const int destinationChild =
                static_cast<int>(source < destination ? destination + 1 : destination);
            beginMoveRows({}, static_cast<int>(source), static_cast<int>(source), {},
                          destinationChild);
            const DeviceItem item = m_devices.takeAt(source);
            m_devices.insert(destination, item);
            endMoveRows();
            source = destination;
        }

        DeviceItem& current = m_devices[source];
        QList<int> changedRoles;
        updateRole(current.name, description.name, NameRole, changedRoles);
        updateRole(current.address, description.address, AddressRole, changedRoles);
        updateRole(current.allowAutomations, description.allowAutomations, AllowAutomationsRole,
                   changedRoles);
        updateRole(current.wakeOnWlan, description.wakeOnWlan, WakeOnWlanRole, changedRoles);
        updateRole(current.refreshPolicy, description.refreshPolicy, RefreshPolicyRole,
                   changedRoles);
        updateRole(current.volumeCeiling, description.volumeCeiling, VolumeCeilingRole,
                   changedRoles);
        if (!changedRoles.isEmpty()) {
            const QModelIndex changedIndex = index(static_cast<int>(source));
            Q_EMIT dataChanged(changedIndex, changedIndex, changedRoles);
        }
    }

    while (m_devices.size() > normalized.size()) {
        const int row = static_cast<int>(m_devices.size() - 1);
        beginRemoveRows({}, row, row);
        m_devices.removeLast();
        endRemoveRows();
    }
}

void DevicesModel::applySnapshot(const DeviceStateSnapshot& snapshot) {
    const qsizetype row = indexOfSerial(snapshot.serial);
    if (row < 0) {
        return;
    }

    DeviceItem& current = m_devices[row];
    QList<int> changedRoles;
    if (snapshot.name) {
        updateRole(current.name, *snapshot.name, NameRole, changedRoles);
    }
    if (snapshot.address) {
        updateRole(current.address, *snapshot.address, AddressRole, changedRoles);
    }
    if (snapshot.health) {
        updateRole(current.health, *snapshot.health, HealthRole, changedRoles);
    }
    if (snapshot.power) {
        updateRole(current.power, *snapshot.power, PowerRole, changedRoles);
    }
    if (snapshot.screensaver) {
        updateRole(current.screensaver, *snapshot.screensaver, ScreensaverRole, changedRoles);
    }
    if (snapshot.activeLabel) {
        updateRole(current.activeLabel, *snapshot.activeLabel, ActiveLabelRole, changedRoles);
    }
    if (snapshot.volume) {
        updateRole(current.volume, *snapshot.volume, VolumeRole, changedRoles);
    }
    if (snapshot.muted) {
        updateRole(current.muted, *snapshot.muted, MutedRole, changedRoles);
    }
    if (snapshot.capabilities) {
        updateRole(current.capabilities, *snapshot.capabilities, CapabilitiesRole, changedRoles);
    }
    updateRole(current.stale, snapshot.stale, StaleRole, changedRoles);

    if (!changedRoles.isEmpty()) {
        const QModelIndex changedIndex = index(static_cast<int>(row));
        Q_EMIT dataChanged(changedIndex, changedIndex, changedRoles);
    }
}

void DevicesModel::markStale(const QString& serial) {
    const qsizetype row = indexOfSerial(serial);
    if (row < 0 || m_devices[row].stale) {
        return;
    }
    m_devices[row].stale = true;
    const QModelIndex changedIndex = index(static_cast<int>(row));
    Q_EMIT dataChanged(changedIndex, changedIndex, {StaleRole});
}

void DevicesModel::markAllStale() {
    for (const DeviceItem& device : std::as_const(m_devices)) {
        markStale(device.serial);
    }
}

void DevicesModel::markCommandPending(const QString& serial, const QString& requestId) {
    const qsizetype row = indexOfSerial(serial);
    if (row < 0 || requestId.isEmpty()) {
        return;
    }
    DeviceItem& device = m_devices[row];
    const bool wasPending = !device.pendingRequests.isEmpty();
    device.pendingRequests.insert(requestId);
    if (!wasPending) {
        const QModelIndex changedIndex = index(static_cast<int>(row));
        Q_EMIT dataChanged(changedIndex, changedIndex, {PendingCommandRole});
    }
}

void DevicesModel::finishPendingCommand(const QString& serial, const QString& requestId) {
    const qsizetype row = indexOfSerial(serial);
    if (row < 0 || requestId.isEmpty()) {
        return;
    }
    DeviceItem& device = m_devices[row];
    if (!device.pendingRequests.remove(requestId) || !device.pendingRequests.isEmpty()) {
        return;
    }
    const QModelIndex changedIndex = index(static_cast<int>(row));
    Q_EMIT dataChanged(changedIndex, changedIndex, {PendingCommandRole});
}

QStringList DevicesModel::serials() const {
    QStringList result;
    result.reserve(m_devices.size());
    for (const DeviceItem& device : m_devices) {
        result.append(device.serial);
    }
    return result;
}

bool DevicesModel::containsSerial(const QString& serial) const {
    return indexOfSerial(serial) >= 0;
}

qsizetype DevicesModel::indexOfSerial(const QString& serial) const {
    for (qsizetype row = 0; row < m_devices.size(); ++row) {
        if (m_devices.at(row).serial == serial) {
            return row;
        }
    }
    return -1;
}

} // namespace RokuDeck

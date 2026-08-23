// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemonproxy.h"

#include "devicesmodel.h"
#include "rokudeck1_interface.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace RokuDeck {
namespace {

constexpr auto serviceName = "io.github.josephur.RokuDeck1";
constexpr auto objectPath = "/io/github/josephur/RokuDeck1";
constexpr quint32 pollIntervalMs = 5000;

std::optional<QJsonObject> parseEnvelope(const QString& json) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    const QJsonValue schemaVersion = object.value(QStringLiteral("schemaVersion"));
    if (!schemaVersion.isDouble() || schemaVersion.toInt() != 1 ||
        schemaVersion.toDouble() != 1.0) {
        return std::nullopt;
    }
    return object;
}

[[nodiscard]] QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<QList<DeviceDescription>> parseDevices(const QString& json) {
    const std::optional<QJsonObject> envelope = parseEnvelope(json);
    if (!envelope || !envelope->value(QStringLiteral("devices")).isArray()) {
        return std::nullopt;
    }

    QList<DeviceDescription> devices;
    QSet<QString> serials;
    const QJsonArray array = envelope->value(QStringLiteral("devices")).toArray();
    devices.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        const QJsonObject object = value.toObject();
        const QJsonValue serial = object.value(QStringLiteral("serial"));
        const QJsonValue name = object.value(QStringLiteral("displayName"));
        const QJsonValue address = object.value(QStringLiteral("address"));
        if (!serial.isString() || serial.toString().isEmpty() || !name.isString() ||
            !address.isString() || serials.contains(serial.toString())) {
            return std::nullopt;
        }
        const QJsonValue allowAutomations = object.value(QStringLiteral("allowAutomations"));
        const QJsonValue wakeOnWlan = object.value(QStringLiteral("wakeOnWlan"));
        const QJsonValue refreshPolicy = object.value(QStringLiteral("refreshPolicy"));
        if ((!allowAutomations.isUndefined() && !allowAutomations.isBool()) ||
            (!wakeOnWlan.isUndefined() && !wakeOnWlan.isBool()) ||
            (!refreshPolicy.isUndefined() && !refreshPolicy.isString())) {
            return std::nullopt;
        }
        DeviceDescription device{serial.toString(), name.toString(), address.toString()};
        // An older daemon omits these keys entirely; its devices behave as they
        // did before the preferences existed rather than being rejected.
        device.allowAutomations = allowAutomations.toBool(true);
        device.wakeOnWlan = wakeOnWlan.toBool(false);
        device.refreshPolicy = refreshPolicy.toString(QStringLiteral("balanced"));
        devices.append(device);
        serials.insert(device.serial);
    }
    return devices;
}

bool parseOptionalString(const QJsonObject& object, const QString& key,
                         std::optional<QString>& destination, bool allowEmpty) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isString() || (!allowEmpty && value.toString().isEmpty())) {
        return false;
    }
    destination = value.toString();
    return true;
}

bool parseActiveLabel(const QJsonObject& object, std::optional<QString>& destination) {
    const QJsonValue activeAppValue = object.value(QStringLiteral("activeApp"));
    if (activeAppValue.isUndefined()) {
        return true;
    }
    if (activeAppValue.isNull()) {
        destination = QString();
        return true;
    }
    if (!activeAppValue.isObject()) {
        return false;
    }
    const QJsonObject activeApp = activeAppValue.toObject();
    destination = QString();
    for (const auto* key : {"name", "input", "id"}) {
        const QJsonValue value = activeApp.value(QString::fromLatin1(key));
        if (!value.isUndefined() && !value.isNull() && !value.isString()) {
            return false;
        }
        if (value.isString() && !value.toString().isEmpty() && destination->isEmpty()) {
            destination = value.toString();
        }
    }
    return true;
}

std::optional<DeviceStateSnapshot> parseSnapshot(const QString& json) {
    const std::optional<QJsonObject> envelope = parseEnvelope(json);
    if (!envelope) {
        return std::nullopt;
    }

    DeviceStateSnapshot snapshot;
    snapshot.serial = envelope->value(QStringLiteral("serial")).toString();
    if (snapshot.serial.isEmpty()) {
        return std::nullopt;
    }
    if (!parseOptionalString(*envelope, QStringLiteral("displayName"), snapshot.name, true) ||
        !parseOptionalString(*envelope, QStringLiteral("address"), snapshot.address, false) ||
        !parseOptionalString(*envelope, QStringLiteral("health"), snapshot.health, false) ||
        !parseOptionalString(*envelope, QStringLiteral("power"), snapshot.power, false) ||
        !parseOptionalString(*envelope, QStringLiteral("screensaver"), snapshot.screensaver,
                             false) ||
        !parseActiveLabel(*envelope, snapshot.activeLabel)) {
        return std::nullopt;
    }

    const QJsonValue volume = envelope->value(QStringLiteral("volume"));
    if (volume.isNull()) {
        snapshot.volume = std::optional<int>();
    } else if (!volume.isUndefined()) {
        if (!volume.isDouble() || !std::isfinite(volume.toDouble()) ||
            volume.toDouble() < std::numeric_limits<int>::min() ||
            volume.toDouble() > std::numeric_limits<int>::max() ||
            volume.toDouble() != std::trunc(volume.toDouble())) {
            return std::nullopt;
        }
        snapshot.volume = std::optional<int>{volume.toInt()};
    }
    const QJsonValue muted = envelope->value(QStringLiteral("muted"));
    if (muted.isNull()) {
        snapshot.muted = std::optional<bool>();
    } else if (!muted.isUndefined()) {
        if (!muted.isBool()) {
            return std::nullopt;
        }
        snapshot.muted = std::optional<bool>{muted.toBool()};
    }

    const QJsonValue capabilitiesValue = envelope->value(QStringLiteral("capabilities"));
    if (capabilitiesValue.isNull()) {
        snapshot.capabilities = QStringList();
    } else if (!capabilitiesValue.isUndefined()) {
        if (!capabilitiesValue.isArray()) {
            return std::nullopt;
        }
        QSet<QString> capabilities;
        for (const QJsonValue& capability : capabilitiesValue.toArray()) {
            if (!capability.isString() || capability.toString().isEmpty()) {
                return std::nullopt;
            }
            capabilities.insert(capability.toString());
        }
        QStringList sortedCapabilities(capabilities.cbegin(), capabilities.cend());
        std::ranges::sort(sortedCapabilities);
        snapshot.capabilities = sortedCapabilities;
    }

    const QJsonValue capturedAtJson = envelope->value(QStringLiteral("capturedAt"));
    if (!capturedAtJson.isUndefined()) {
        if (!capturedAtJson.isString()) {
            return std::nullopt;
        }
        const QString capturedAtValue = capturedAtJson.toString();
        QDateTime capturedAt = QDateTime::fromString(capturedAtValue, Qt::ISODateWithMs);
        if (!capturedAt.isValid()) {
            capturedAt = QDateTime::fromString(capturedAtValue, Qt::ISODate);
        }
        if (!capturedAt.isValid()) {
            return std::nullopt;
        }
        snapshot.capturedAt = capturedAt;
        snapshot.stale = false;
    }
    return snapshot;
}

QJsonObject commandObject(const QString& action, const QVariant& value) {
    if (action == QStringLiteral("keypress")) {
        return {{QStringLiteral("operation"), action},
                {QStringLiteral("key"), QJsonValue::fromVariant(value)}};
    }
    if (action == QStringLiteral("launch")) {
        return {{QStringLiteral("operation"), action},
                {QStringLiteral("appId"), QJsonValue::fromVariant(value)}};
    }
    return {{QStringLiteral("operation"), action},
            {QStringLiteral("value"), QJsonValue::fromVariant(value)}};
}

} // namespace

DaemonProxy::DaemonProxy(DevicesModel* devices, QString clientId, QObject* parent)
    : QObject(parent), m_devices(devices), m_clientId(std::move(clientId)),
      m_interface(std::make_unique<IoGithubJosephurRokuDeck1Interface>(
          QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
          QDBusConnection::sessionBus())),
      m_ownerWatcher(std::make_unique<QDBusServiceWatcher>(
          QString::fromLatin1(serviceName), QDBusConnection::sessionBus(),
          QDBusServiceWatcher::WatchForOwnerChange)) {
    Q_ASSERT(m_devices != nullptr);
    connect(m_ownerWatcher.get(), &QDBusServiceWatcher::serviceOwnerChanged, this,
            &DaemonProxy::handleServiceOwnerChanged);
    connect(m_interface.get(), &IoGithubJosephurRokuDeck1Interface::DeviceListChanged, this,
            &DaemonProxy::applyDeviceListJson);
    connect(m_interface.get(), &IoGithubJosephurRokuDeck1Interface::DeviceSnapshotChanged, this,
            &DaemonProxy::applySnapshotJson);
    connect(m_interface.get(), &IoGithubJosephurRokuDeck1Interface::CommandFinished, this,
            &DaemonProxy::handleCommandFinished);
}

DaemonProxy::~DaemonProxy() {
    stop();
}

bool DaemonProxy::isAvailable() const {
    return m_available;
}

void DaemonProxy::start() {
    if (m_active) {
        return;
    }
    m_active = true;
    ++m_generation;
    m_lastSnapshotAt.clear();
    refresh();
}

void DaemonProxy::stop() {
    if (!m_active) {
        return;
    }
    m_active = false;
    ++m_generation;
    m_lastSnapshotAt.clear();
    if (m_interface) {
        static_cast<void>(m_interface->UnregisterClient(m_clientId));
    }
    m_registrationPending = false;
    m_registered = false;
    m_registeredSerials.clear();
    clearPendingCommands();
    setAvailable(false);
}

void DaemonProxy::refresh() {
    if (!m_active) {
        return;
    }
    const quint64 generation = m_generation;
    auto* watcher = new QDBusPendingCallWatcher(m_interface->ListDevices(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!m_active || generation != m_generation) {
            return;
        }
        if (reply.isError()) {
            setAvailable(false);
            Q_EMIT errorOccurred(i18nc("@info", "RokuDeck daemon is unavailable"));
            return;
        }
        applyDeviceListJson(reply.value());
        setAvailable(true);
    });
}

void DaemonProxy::sendCommand(const QString& serial, const QString& action, const QVariant& value) {
    if (!m_active || serial.isEmpty() || action.isEmpty() || !m_devices->containsSerial(serial)) {
        return;
    }
    const QString json = QString::fromUtf8(
        QJsonDocument(commandObject(action, value)).toJson(QJsonDocument::Compact));
    const quint64 generation = m_generation;
    auto* watcher = new QDBusPendingCallWatcher(m_interface->SendCommand(serial, json), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation, serial]() {
                const QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                if (!m_active || generation != m_generation) {
                    return;
                }
                if (reply.isError() || reply.value().isEmpty()) {
                    Q_EMIT errorOccurred(i18nc("@info", "The Roku command was rejected"));
                    return;
                }
                const QString requestId = reply.value();
                m_requestSerials.insert(requestId, serial);
                m_devices->markCommandPending(serial, requestId);
            });
}

bool DaemonProxy::isBusy() const {
    return m_busyCount > 0;
}

void DaemonProxy::setBusyCount(int count) {
    const bool wasBusy = isBusy();
    m_busyCount = std::max(0, count);
    if (wasBusy != isBusy()) {
        Q_EMIT busyChanged();
    }
}

void DaemonProxy::watchDeviceCall(const QString& operation, const QDBusPendingCall& call) {
    const quint64 generation = m_generation;
    setBusyCount(m_busyCount + 1);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation, operation]() {
                const QDBusPendingReply<void> reply = *watcher;
                watcher->deleteLater();
                if (!m_active || generation != m_generation) {
                    setBusyCount(m_busyCount - 1);
                    return;
                }
                setBusyCount(m_busyCount - 1);
                if (reply.isError()) {
                    Q_EMIT deviceOperationFailed(operation, reply.error().message());
                    return;
                }
                Q_EMIT deviceOperationSucceeded(operation);
                // The daemon announces its own list change, but refreshing here
                // keeps a widget correct even if that signal is missed.
                refresh();
            });
}

void DaemonProxy::addDevice(const QVariantMap& device) {
    if (!m_active) {
        return;
    }
    watchDeviceCall(QStringLiteral("addDevice"),
                    m_interface->AddDevice(compactJson(QJsonObject::fromVariantMap(device))));
}

void DaemonProxy::updateDevice(const QString& serial, const QVariantMap& changes) {
    if (!m_active || serial.isEmpty()) {
        return;
    }
    watchDeviceCall(
        QStringLiteral("updateDevice"),
        m_interface->UpdateDevice(serial, compactJson(QJsonObject::fromVariantMap(changes))));
}

void DaemonProxy::removeDevice(const QString& serial) {
    if (!m_active || serial.isEmpty()) {
        return;
    }
    watchDeviceCall(QStringLiteral("removeDevice"), m_interface->RemoveDevice(serial));
}

void DaemonProxy::setDeviceOrder(const QStringList& serials) {
    if (!m_active || serials.isEmpty()) {
        return;
    }
    watchDeviceCall(QStringLiteral("setDeviceOrder"), m_interface->SetDeviceOrder(serials));
}

void DaemonProxy::testDevice(const QString& address) {
    if (!m_active || address.isEmpty()) {
        return;
    }
    const quint64 generation = m_generation;
    setBusyCount(m_busyCount + 1);
    auto* watcher = new QDBusPendingCallWatcher(m_interface->TestDevice(address), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation, address]() {
                const QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                setBusyCount(m_busyCount - 1);
                if (!m_active || generation != m_generation) {
                    return;
                }
                if (reply.isError()) {
                    Q_EMIT deviceTestFinished(address, {}, reply.error().message());
                    return;
                }
                const std::optional<DeviceStateSnapshot> snapshot = parseSnapshot(reply.value());
                if (!snapshot) {
                    Q_EMIT deviceTestFinished(
                        address, {},
                        i18nc("@info", "The Roku device answered with an unreadable status"));
                    return;
                }
                QVariantMap result{{QStringLiteral("serial"), snapshot->serial},
                                   {QStringLiteral("address"), address}};
                if (snapshot->name) {
                    result.insert(QStringLiteral("displayName"), *snapshot->name);
                }
                if (snapshot->health) {
                    result.insert(QStringLiteral("health"), *snapshot->health);
                }
                if (snapshot->power) {
                    result.insert(QStringLiteral("power"), *snapshot->power);
                }
                if (snapshot->screensaver) {
                    result.insert(QStringLiteral("screensaver"), *snapshot->screensaver);
                }
                if (snapshot->activeLabel) {
                    result.insert(QStringLiteral("activeLabel"), *snapshot->activeLabel);
                }
                if (snapshot->volume && snapshot->volume->has_value()) {
                    result.insert(QStringLiteral("volume"), **snapshot->volume);
                }
                if (snapshot->muted && snapshot->muted->has_value()) {
                    result.insert(QStringLiteral("muted"), **snapshot->muted);
                }
                if (snapshot->capabilities) {
                    result.insert(QStringLiteral("capabilities"), *snapshot->capabilities);
                }
                Q_EMIT deviceTestFinished(address, result, {});
            });
}

void DaemonProxy::discoverDevices() {
    if (!m_active) {
        return;
    }
    const quint64 generation = m_generation;
    setBusyCount(m_busyCount + 1);
    auto* watcher = new QDBusPendingCallWatcher(m_interface->DiscoverDevices(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation]() {
                const QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                setBusyCount(m_busyCount - 1);
                if (!m_active || generation != m_generation) {
                    return;
                }
                if (reply.isError()) {
                    Q_EMIT discoveryFinished(reply.error().message());
                    return;
                }
                applyDeviceListJson(reply.value());
                Q_EMIT discoveryFinished({});
            });
}

void DaemonProxy::handleServiceOwnerChanged(const QString& service, const QString& oldOwner,
                                            const QString& newOwner) {
    Q_UNUSED(service)
    Q_UNUSED(oldOwner)
    ++m_generation;
    m_lastSnapshotAt.clear();
    m_registrationPending = false;
    m_registered = false;
    m_registeredSerials.clear();
    clearPendingCommands();
    if (newOwner.isEmpty()) {
        m_devices->markAllStale();
        setAvailable(false);
        return;
    }
    if (m_active) {
        refresh();
    }
}

void DaemonProxy::applyDeviceListJson(const QString& devicesJson) {
    if (!m_active) {
        return;
    }
    const std::optional<QList<DeviceDescription>> devices = parseDevices(devicesJson);
    if (!devices) {
        m_devices->markAllStale();
        Q_EMIT errorOccurred(i18nc("@info", "The RokuDeck device list is invalid"));
        return;
    }
    m_devices->replaceDevices(*devices);
    updateRegistration();
    for (const DeviceDescription& device : *devices) {
        requestSnapshot(device.serial);
    }
}

void DaemonProxy::applySnapshotJson(const QString& expectedSerial, const QString& snapshotJson) {
    if (!m_active) {
        return;
    }
    const std::optional<DeviceStateSnapshot> snapshot = parseSnapshot(snapshotJson);
    if (!snapshot || snapshot->serial != expectedSerial) {
        m_devices->markStale(expectedSerial);
        Q_EMIT errorOccurred(i18nc("@info", "The RokuDeck device snapshot is invalid"));
        return;
    }
    if (snapshot->stale) {
        // Already applied nothing new; keep whatever freshness we had.
    } else {
        // Post-command pulls race the periodic push: an older capture can
        // finish after a newer one. Applying it out of order would, for
        // example, flip the mute icon back while the television really is
        // muted - so strictly older readings are dropped.
        const QDateTime appliedAt = m_lastSnapshotAt.value(expectedSerial);
        if (snapshot->capturedAt.isValid() && appliedAt.isValid()
            && snapshot->capturedAt < appliedAt) {
            return;
        }
    }
    m_devices->applySnapshot(*snapshot);
    if (snapshot->capturedAt.isValid()) {
        m_lastSnapshotAt[expectedSerial] = snapshot->capturedAt;
    }
}

void DaemonProxy::requestSnapshot(const QString& serial) {
    const quint64 generation = m_generation;
    auto* watcher = new QDBusPendingCallWatcher(m_interface->GetSnapshot(serial), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation, serial]() {
                const QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                if (!m_active || generation != m_generation || reply.isError()) {
                    return;
                }
                applySnapshotJson(serial, reply.value());
            });
}

void DaemonProxy::updateRegistration() {
    if (!m_active || m_registrationPending) {
        return;
    }
    const QStringList desiredSerials = m_devices->serials();
    if (m_registered && desiredSerials == m_registeredSerials) {
        return;
    }

    m_registrationPending = true;
    const bool updating = m_registered;
    const quint64 generation = m_generation;
    const QDBusPendingCall call =
        updating ? m_interface->UpdateClientInterest(m_clientId, desiredSerials, pollIntervalMs)
                 : m_interface->RegisterClient(m_clientId, desiredSerials, pollIntervalMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation, desiredSerials]() {
                const QDBusPendingReply<> reply = *watcher;
                watcher->deleteLater();
                if (!m_active || generation != m_generation) {
                    return;
                }
                m_registrationPending = false;
                if (reply.isError()) {
                    m_registered = false;
                    m_registeredSerials.clear();
                    Q_EMIT errorOccurred(i18nc("@info", "RokuDeck widget registration failed"));
                    return;
                }
                m_registered = true;
                m_registeredSerials = desiredSerials;
                if (m_devices->serials() != m_registeredSerials) {
                    updateRegistration();
                }
            });
}

void DaemonProxy::setAvailable(bool available) {
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged(m_available);
}

void DaemonProxy::clearPendingCommands() {
    for (auto it = m_requestSerials.cbegin(); it != m_requestSerials.cend(); ++it) {
        m_devices->finishPendingCommand(it.value(), it.key());
    }
    m_requestSerials.clear();
}

void DaemonProxy::handleCommandFinished(const QString& requestId, const QString& resultJson) {
    const auto request = m_requestSerials.constFind(requestId);
    if (request == m_requestSerials.cend()) {
        return;
    }
    const QString serial = request.value();
    const std::optional<QJsonObject> result = parseEnvelope(resultJson);
    if (!result || result->value(QStringLiteral("requestId")).toString() != requestId ||
        result->value(QStringLiteral("serial")).toString() != serial ||
        !result->value(QStringLiteral("success")).isBool()) {
        m_devices->finishPendingCommand(serial, requestId);
        m_requestSerials.remove(requestId);
        Q_EMIT errorOccurred(i18nc("@info", "The Roku command result is invalid"));
        return;
    }
    if (!result->value(QStringLiteral("success")).toBool()) {
        m_devices->finishPendingCommand(serial, requestId);
        m_requestSerials.remove(requestId);
        Q_EMIT errorOccurred(i18nc("@info", "The Roku command failed"));
        return;
    }
    // The command is done, so re-enable input right away: holding buttons
    // until the next poll lands would stall repeated keypresses by seconds.
    // Pull a fresh snapshot immediately so any state change the command
    // caused (e.g. standby after power off) gates the controls without
    // waiting for the next periodic poll.
    m_devices->finishPendingCommand(serial, requestId);
    m_requestSerials.remove(requestId);
    requestSnapshot(serial);
}

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_service.h"

#include "config_store.h"
#include "core/qprocess_runner.h"
#include "rokudeck1_adaptor.h"

#include <QDBusError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

namespace RokuDeck {
namespace {

constexpr auto serviceName = "io.github.josephur.RokuDeck1";
constexpr auto objectPath = "/io/github/josephur/RokuDeck1";

[[nodiscard]] QString powerName(PowerState power) {
    switch (power) {
    case PowerState::On:
        return QStringLiteral("on");
    case PowerState::Standby:
        return QStringLiteral("standby");
    case PowerState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString triStateName(TriState state) {
    switch (state) {
    case TriState::Active:
        return QStringLiteral("active");
    case TriState::Inactive:
        return QStringLiteral("inactive");
    case TriState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString mediaName(MediaPlaybackState state) {
    switch (state) {
    case MediaPlaybackState::Playing:
        return QStringLiteral("playing");
    case MediaPlaybackState::Paused:
        return QStringLiteral("paused");
    case MediaPlaybackState::Stopped:
        return QStringLiteral("stopped");
    case MediaPlaybackState::Idle:
        return QStringLiteral("idle");
    case MediaPlaybackState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString healthName(DeviceHealth health) {
    switch (health) {
    case DeviceHealth::Online:
        return QStringLiteral("online");
    case DeviceHealth::Standby:
        return QStringLiteral("standby");
    case DeviceHealth::Degraded:
        return QStringLiteral("degraded");
    case DeviceHealth::Offline:
        return QStringLiteral("offline");
    case DeviceHealth::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QList<DeviceRecord> parseDiscoveredDevices(const ProcessResult& result) {
    if (result.timedOut || result.exitCode != 0) {
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }

    QList<DeviceRecord> devices;
    const QJsonArray values = document.isArray()
                                  ? document.array()
                                  : document.object().value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& value : values) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        DeviceRecord device;
        device.serial = object.value(QStringLiteral("serialNumber")).toString().trimmed();
        device.displayName = object.value(QStringLiteral("deviceName")).toString().trimmed();
        device.address = object.value(QStringLiteral("ip")).toString().trimmed();
        if (!device.serial.isEmpty() && !device.address.isEmpty() && device.serial.size() <= 256 &&
            device.displayName.size() <= 256 && device.address.size() <= 256) {
            devices.append(std::move(device));
        }
    }
    return devices;
}

} // namespace

DaemonService::DaemonService(QDBusConnection connection,
                             std::function<bool()> hasEnabledAutomations,
                             std::unique_ptr<ProcessRunner> processRunner,
                             std::unique_ptr<ConfigStore> configStore,
                             std::chrono::milliseconds idleGrace, QObject* parent)
    : QObject(parent), m_connection(std::move(connection)),
      m_hasEnabledAutomations(hasEnabledAutomations ? std::move(hasEnabledAutomations)
                                                    : [] { return false; }),
      m_idleGrace(std::max(idleGrace, std::chrono::milliseconds(1))),
      m_configStore(configStore ? std::move(configStore) : std::make_unique<ConfigStore>()),
      m_processRunner(processRunner ? std::move(processRunner)
                                    : std::make_unique<QProcessRunner>()),
      m_rdsClient(m_processRunner.get()), m_ownerWatcher(this) {
    new RokuDeck1Adaptor(this);
    m_ownerWatcher.setConnection(m_connection);
    m_ownerWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    for (const DeviceRecord& device : m_configStore->loadDevices()) {
        m_deviceRegistry.upsert(device);
        m_addressGenerations.insert(device.serial, 1);
    }

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(static_cast<int>(std::min<std::chrono::milliseconds::rep>(
        m_idleGrace.count(), std::numeric_limits<int>::max())));
    connect(&m_idleTimer, &QTimer::timeout, this, &DaemonService::handleIdleTimeout);

    m_pollTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &DaemonService::pollDueDevices);
    m_pollTimer.start();
    connect(&m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &DaemonService::removeClientsForOwner);
    updateIdleTimer();
}

DaemonService::~DaemonService() {
    m_shuttingDown = true;
    m_pollTimer.stop();
    m_idleTimer.stop();
    m_commandQueues.clear();
    if (m_registeredOnBus) {
        m_connection.unregisterService(QString::fromLatin1(serviceName));
        m_connection.unregisterObject(QString::fromLatin1(objectPath));
    }
}

bool DaemonService::registerOnBus() {
    m_lastRegistrationError.clear();
    if (m_registeredOnBus) {
        return true;
    }
    if (!m_connection.isConnected()) {
        m_lastRegistrationError = QStringLiteral("the session bus is not connected");
        return false;
    }
    if (!m_connection.registerObject(QString::fromLatin1(objectPath), this,
                                     QDBusConnection::ExportAdaptors)) {
        m_lastRegistrationError = m_connection.lastError().message();
        if (m_lastRegistrationError.isEmpty()) {
            m_lastRegistrationError = QStringLiteral("failed to register the D-Bus object");
        }
        return false;
    }
    if (!m_connection.registerService(QString::fromLatin1(serviceName))) {
        m_lastRegistrationError = m_connection.lastError().message();
        if (m_lastRegistrationError.isEmpty()) {
            m_lastRegistrationError = QStringLiteral("failed to own the D-Bus service name");
        }
        m_connection.unregisterObject(QString::fromLatin1(objectPath));
        return false;
    }
    m_registeredOnBus = true;
    return true;
}

void DaemonService::upsertDevice(const DeviceRecord& device) {
    if (device.serial.isEmpty()) {
        return;
    }
    if (mergeDevice(device)) {
        m_configStore->saveDevices(m_deviceRegistry.devices());
        Q_EMIT DeviceListChanged(ListDevices());
    }
}

bool DaemonService::mergeDevice(const DeviceRecord& device) {
    if (device.serial.isEmpty()) {
        return false;
    }
    const auto previous = findDevice(device.serial);
    const bool changed = !previous || previous->displayName != device.displayName ||
                         previous->address != device.address;
    if (!changed) {
        return false;
    }

    const bool addressChanged = previous && previous->address != device.address;
    m_deviceRegistry.upsert(device);
    if (!previous) {
        m_addressGenerations.insert(device.serial, 1);
    } else if (addressChanged) {
        m_addressGenerations[device.serial] = m_addressGenerations.value(device.serial, 1) + 1;
        m_snapshots.remove(device.serial);
    } else {
        auto snapshotIt = m_snapshots.find(device.serial);
        if (snapshotIt != m_snapshots.end()) {
            snapshotIt->displayName = device.displayName;
            snapshotIt->address = device.address;
        }
    }
    return true;
}

QString DaemonService::devicesJson(const QList<DeviceRecord>& devices) {
    QJsonArray entries;
    for (const DeviceRecord& device : devices) {
        entries.append(QJsonObject{{QStringLiteral("serial"), device.serial},
                                   {QStringLiteral("displayName"), device.displayName},
                                   {QStringLiteral("address"), device.address}});
    }
    return compactJson(
        QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("devices"), entries}});
}

QString DaemonService::snapshotJson(const DeviceSnapshot& snapshot) {
    QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("serial"), snapshot.serial},
                       {QStringLiteral("displayName"), snapshot.displayName},
                       {QStringLiteral("address"), snapshot.address},
                       {QStringLiteral("power"), powerName(snapshot.power)},
                       {QStringLiteral("screensaver"), triStateName(snapshot.screensaver)},
                       {QStringLiteral("media"), mediaName(snapshot.media)},
                       {QStringLiteral("health"), healthName(snapshot.health)}};
    if (snapshot.activeApp) {
        object.insert(QStringLiteral("activeApp"),
                      QJsonObject{{QStringLiteral("id"), snapshot.activeApp->id},
                                  {QStringLiteral("name"), snapshot.activeApp->name},
                                  {QStringLiteral("type"), snapshot.activeApp->type},
                                  {QStringLiteral("input"), snapshot.activeApp->input}});
    } else {
        object.insert(QStringLiteral("activeApp"), QJsonValue::Null);
    }
    object.insert(QStringLiteral("volume"),
                  snapshot.volume ? QJsonValue(*snapshot.volume) : QJsonValue::Null);
    object.insert(QStringLiteral("muted"),
                  snapshot.muted ? QJsonValue(*snapshot.muted) : QJsonValue::Null);

    QStringList capabilities(snapshot.capabilities.cbegin(), snapshot.capabilities.cend());
    capabilities.sort();
    QJsonArray capabilityArray;
    for (const QString& capability : capabilities) {
        capabilityArray.append(capability);
    }
    object.insert(QStringLiteral("capabilities"), capabilityArray);
    if (snapshot.capturedAt.isValid()) {
        object.insert(QStringLiteral("capturedAt"),
                      snapshot.capturedAt.toString(Qt::ISODateWithMs));
    }
    return compactJson(object);
}

QString DaemonService::ListDevices() const {
    return devicesJson(m_deviceRegistry.devices());
}

QString DaemonService::GetSnapshot(const QString& serial) const {
    const auto snapshotIt = m_snapshots.constFind(serial);
    if (snapshotIt != m_snapshots.cend()) {
        return snapshotJson(snapshotIt.value());
    }

    const auto device = findDevice(serial);
    if (!device) {
        return compactJson(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                       {QStringLiteral("error"), QStringLiteral("unknownDevice")}});
    }
    DeviceSnapshot snapshot;
    snapshot.serial = device->serial;
    snapshot.displayName = device->displayName;
    snapshot.address = device->address;
    return snapshotJson(snapshot);
}

QString DaemonService::callerOwner() const {
    if (calledFromDBus() && !message().service().isEmpty()) {
        return message().service();
    }
    return m_connection.baseService();
}

void DaemonService::removePollInterests(const ClientRegistration& registration) {
    const QString schedulerClientId = registration.owner + QChar(u'\x1f') + registration.clientId;
    for (const QString& serial : registration.serials) {
        static_cast<void>(m_pollScheduler.removeInterest(schedulerClientId, serial));
    }
}

void DaemonService::applyPollInterests(const ClientRegistration& registration) {
    const QString schedulerClientId = registration.owner + QChar(u'\x1f') + registration.clientId;
    for (const QString& serial : registration.serials) {
        m_pollScheduler.setInterest(
            {schedulerClientId,
             serial,
             {QStringLiteral("power"), QStringLiteral("screensaver"), QStringLiteral("activeApp"),
              QStringLiteral("media"), QStringLiteral("volume"), QStringLiteral("muted")},
             std::chrono::milliseconds(registration.intervalMs)});
    }
}

void DaemonService::watchOwner(const QString& owner) {
    if (owner.startsWith(QChar(u':')) && !m_ownerWatcher.watchedServices().contains(owner)) {
        m_ownerWatcher.addWatchedService(owner);
    }
}

void DaemonService::RegisterClient(const QString& clientId, const QStringList& serials,
                                   uint intervalMs) {
    const QString owner = callerOwner();
    const auto previous = m_clients.registration(owner, clientId);
    if (!m_clients.registerClient(owner, clientId, serials, intervalMs)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("clientId and intervalMs must be valid"));
        }
        return;
    }
    if (previous) {
        removePollInterests(*previous);
    }
    applyPollInterests(*m_clients.registration(owner, clientId));
    watchOwner(owner);
    updateIdleTimer();
}

void DaemonService::UpdateClientInterest(const QString& clientId, const QStringList& serials,
                                         uint intervalMs) {
    const QString owner = callerOwner();
    const auto previous = m_clients.registration(owner, clientId);
    if (!previous || !m_clients.updateClient(owner, clientId, serials, intervalMs)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("the client is not registered or arguments are invalid"));
        }
        return;
    }
    removePollInterests(*previous);
    applyPollInterests(*m_clients.registration(owner, clientId));
    updateIdleTimer();
}

void DaemonService::UnregisterClient(const QString& clientId) {
    const QString owner = callerOwner();
    const auto removed = m_clients.takeClient(owner, clientId);
    if (!removed) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("the client is not registered"));
        }
        return;
    }
    removePollInterests(*removed);
    if (!m_clients.containsOwner(owner)) {
        m_ownerWatcher.removeWatchedService(owner);
    }
    updateIdleTimer();
}

QString DaemonService::DiscoverDevices() {
    if (m_discoveryActive) {
        return ListDevices();
    }

    m_discoveryActive = true;
    updateIdleTimer();
    ProcessRequest request;
    request.program = QStringLiteral("rds");
    request.arguments = {QStringLiteral("--json"), QStringLiteral("--quiet"),
                         QStringLiteral("--timeout"), QStringLiteral("3000"),
                         QStringLiteral("discover")};
    request.timeout = std::chrono::milliseconds(3000);
    QPointer<DaemonService> service(this);
    m_processRunner->start(request, [service](ProcessResult result) {
        if (!service || service->m_shuttingDown) {
            return;
        }
        service->m_discoveryActive = false;
        bool changed = false;
        for (const DeviceRecord& device : parseDiscoveredDevices(result)) {
            changed = service->mergeDevice(device) || changed;
        }
        if (changed) {
            service->m_configStore->saveDevices(service->m_deviceRegistry.devices());
            Q_EMIT service->DeviceListChanged(service->ListDevices());
        }
        service->updateIdleTimer();
    });
    return ListDevices();
}

DaemonService::ValidatedCommand DaemonService::validateCommand(const QString& serial,
                                                               const QString& commandJson) const {
    constexpr qsizetype maximumCommandBytes = 2048;
    const QByteArray encoded = commandJson.toUtf8();
    if (encoded.size() > maximumCommandBytes) {
        return {{}, QStringLiteral("commandTooLarge")};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {{}, QStringLiteral("invalidCommand")};
    }
    const QJsonObject object = document.object();
    const QStringList objectKeys = object.keys();
    const QString operation = object.value(QStringLiteral("operation")).toString();
    if (operation == QStringLiteral("keypress")) {
        const QSet<QString> expectedKeys{QStringLiteral("operation"), QStringLiteral("key")};
        if (QSet<QString>(objectKeys.cbegin(), objectKeys.cend()) != expectedKeys) {
            return {{}, QStringLiteral("invalidCommand")};
        }
        const QString key = object.value(QStringLiteral("key")).toString();
        static const QSet<QString> allowedKeys{
            QStringLiteral("Home"),      QStringLiteral("Back"), QStringLiteral("Up"),
            QStringLiteral("Down"),      QStringLiteral("Left"), QStringLiteral("Right"),
            QStringLiteral("Select"),    QStringLiteral("Play"), QStringLiteral("Pause"),
            QStringLiteral("Rev"),       QStringLiteral("Fwd"),  QStringLiteral("InstantReplay"),
            QStringLiteral("FindRemote")};
        if (key.isEmpty() || key.size() > 32 || key.startsWith(QChar(u'-')) ||
            !allowedKeys.contains(key)) {
            return {{}, QStringLiteral("unsupportedCommand")};
        }
        if (key == QStringLiteral("FindRemote")) {
            const auto snapshotIt = m_snapshots.constFind(serial);
            if (snapshotIt == m_snapshots.cend() ||
                !snapshotIt->capabilities.contains(QStringLiteral("supports-find-remote"))) {
                return {{}, QStringLiteral("unsupportedCapability")};
            }
        }
        return {{QStringLiteral("keypress"), key}, {}};
    }

    if (operation == QStringLiteral("launch")) {
        const QSet<QString> expectedKeys{QStringLiteral("operation"), QStringLiteral("appId")};
        if (QSet<QString>(objectKeys.cbegin(), objectKeys.cend()) != expectedKeys) {
            return {{}, QStringLiteral("invalidCommand")};
        }
        const QString appId = object.value(QStringLiteral("appId")).toString();
        static const QRegularExpression safeAppId(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
        if (!safeAppId.match(appId).hasMatch()) {
            return {{}, QStringLiteral("unsupportedCommand")};
        }
        return {{QStringLiteral("launch"), appId}, {}};
    }

    return {{}, QStringLiteral("unsupportedOperation")};
}

QString DaemonService::SendCommand(const QString& serial, const QString& commandJson) {
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto device = findDevice(serial);
    if (!device) {
        QTimer::singleShot(0, this, [this, requestId, serial] {
            finishCommandWithError(requestId, serial, QStringLiteral("unknownDevice"));
        });
        return requestId;
    }

    const ValidatedCommand validated = validateCommand(serial, commandJson);
    if (!validated.errorCode.isEmpty()) {
        QTimer::singleShot(0, this, [this, requestId, serial, errorCode = validated.errorCode] {
            finishCommandWithError(requestId, serial, errorCode);
        });
        return requestId;
    }

    m_commandQueues[serial].enqueue({requestId, serial, validated.arguments});
    updateIdleTimer();
    processNextCommand(serial);
    return requestId;
}

void DaemonService::processNextCommand(const QString& serial) {
    auto queueIt = m_commandQueues.find(serial);
    if (m_shuttingDown || m_activeCommandSerials.contains(serial) ||
        queueIt == m_commandQueues.end() || queueIt->isEmpty()) {
        return;
    }
    const auto device = findDevice(serial);
    if (!device) {
        const QueuedCommand command = queueIt->dequeue();
        if (queueIt->isEmpty()) {
            m_commandQueues.erase(queueIt);
        }
        finishCommandWithError(command.requestId, serial, QStringLiteral("unknownDevice"));
        updateIdleTimer();
        processNextCommand(serial);
        return;
    }

    const QueuedCommand command = queueIt->head();
    ProcessRequest request;
    request.program = QStringLiteral("rds");
    request.arguments = {QStringLiteral("--json"),    QStringLiteral("--quiet"),
                         QStringLiteral("--timeout"), QStringLiteral("3000"),
                         QStringLiteral("--ip"),      device->address};
    request.arguments.append(command.arguments);
    request.timeout = std::chrono::milliseconds(3000);
    m_activeCommandSerials.insert(serial);

    QPointer<DaemonService> service(this);
    m_processRunner->start(request, [service, serial, command](ProcessResult result) {
        if (!service || service->m_shuttingDown) {
            return;
        }
        auto activeQueueIt = service->m_commandQueues.find(serial);
        if (activeQueueIt != service->m_commandQueues.end() && !activeQueueIt->isEmpty()) {
            activeQueueIt->dequeue();
            if (activeQueueIt->isEmpty()) {
                service->m_commandQueues.erase(activeQueueIt);
            }
        }
        service->m_activeCommandSerials.remove(serial);
        service->finishCommand(command, result);
        service->processNextCommand(serial);
        service->updateIdleTimer();
    });
}

void DaemonService::finishCommand(const QueuedCommand& command, const ProcessResult& result) {
    const bool succeeded = !result.timedOut && result.exitCode == 0;
    QJsonObject payload{{QStringLiteral("schemaVersion"), 1},
                        {QStringLiteral("requestId"), command.requestId},
                        {QStringLiteral("serial"), command.serial},
                        {QStringLiteral("success"), succeeded}};
    if (!succeeded) {
        payload.insert(QStringLiteral("error"), result.timedOut ? QStringLiteral("timedOut")
                                                                : QStringLiteral("commandFailed"));
    }
    Q_EMIT CommandFinished(command.requestId, compactJson(payload));
}

void DaemonService::finishCommandWithError(const QString& requestId, const QString& serial,
                                           const QString& errorCode) {
    Q_EMIT CommandFinished(requestId,
                           compactJson(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                   {QStringLiteral("requestId"), requestId},
                                                   {QStringLiteral("serial"), serial},
                                                   {QStringLiteral("success"), false},
                                                   {QStringLiteral("error"), errorCode}}));
}

std::optional<DeviceRecord> DaemonService::findDevice(const QString& serial) const {
    const QList<DeviceRecord> devices = m_deviceRegistry.devices();
    const auto it = std::ranges::find(devices, serial, &DeviceRecord::serial);
    if (it == devices.cend()) {
        return std::nullopt;
    }
    return *it;
}

void DaemonService::pollDueDevices() {
    const QList<PollClaim> claims = m_pollScheduler.claimDuePolls();
    for (const PollClaim& claim : claims) {
        const auto device = findDevice(claim.serial);
        if (!device) {
            static_cast<void>(m_pollScheduler.recordResult(claim, false));
            continue;
        }
        const QString address = device->address;
        const quint64 addressGeneration = m_addressGenerations.value(claim.serial);
        QPointer<DaemonService> service(this);
        m_rdsClient.queryDevice(
            address, [service, claim, address, addressGeneration](RdsQueryResult result) {
                if (!service || service->m_shuttingDown) {
                    return;
                }
                const auto currentDevice = service->findDevice(claim.serial);
                if (!currentDevice || currentDevice->address != address ||
                    service->m_addressGenerations.value(claim.serial) != addressGeneration) {
                    static_cast<void>(service->m_pollScheduler.recordResult(claim, true));
                    return;
                }
                if (!result || result.value().serial != claim.serial) {
                    static_cast<void>(service->m_pollScheduler.recordResult(claim, false));
                    return;
                }

                DeviceSnapshot snapshot = result.value();
                snapshot.serial = claim.serial;
                snapshot.address = address;
                snapshot.capturedAt = QDateTime::currentDateTimeUtc();
                if (snapshot.displayName.isEmpty()) {
                    snapshot.displayName = currentDevice->displayName;
                }
                service->m_snapshots.insert(claim.serial, snapshot);
                const bool deviceChanged =
                    service->mergeDevice({claim.serial, snapshot.displayName, address});
                if (deviceChanged) {
                    service->m_configStore->saveDevices(service->m_deviceRegistry.devices());
                    Q_EMIT service->DeviceListChanged(service->ListDevices());
                }
                static_cast<void>(service->m_pollScheduler.recordResult(claim, true));
                Q_EMIT service->DeviceSnapshotChanged(claim.serial, snapshotJson(snapshot));
            });
    }
}

void DaemonService::removeClientsForOwner(const QString& owner) {
    const QList<ClientRegistration> registrations = m_clients.takeOwner(owner);
    for (const ClientRegistration& registration : registrations) {
        removePollInterests(registration);
    }
    m_ownerWatcher.removeWatchedService(owner);
    updateIdleTimer();
}

void DaemonService::automationEnablementChanged() {
    updateIdleTimer();
}

void DaemonService::updateIdleTimer() {
    if (m_clients.isEmpty() && !m_hasEnabledAutomations() && !hasAcceptedWork()) {
        m_idleTimer.start();
    } else {
        m_idleTimer.stop();
    }
}

bool DaemonService::hasAcceptedWork() const {
    return m_discoveryActive || !m_activeCommandSerials.isEmpty() || !m_commandQueues.isEmpty();
}

void DaemonService::handleIdleTimeout() {
    if (!m_clients.isEmpty() || m_hasEnabledAutomations() || hasAcceptedWork()) {
        updateIdleTimer();
        return;
    }
    Q_EMIT idleExitRequested();
}

quint32 DaemonService::effectiveInterval(const QString& serial) const {
    if (!m_pollScheduler.nextDueFor(serial)) {
        return 0;
    }
    const auto interval = m_pollScheduler.intervalFor(serial).count();
    return static_cast<quint32>(std::clamp<std::chrono::milliseconds::rep>(
        interval, 0, std::numeric_limits<quint32>::max()));
}

bool DaemonService::idleTimerActive() const {
    return m_idleTimer.isActive();
}

int DaemonService::idleGracePeriodMs() const {
    return m_idleTimer.interval();
}

qsizetype DaemonService::watchedClientOwnerCount() const {
    return m_ownerWatcher.watchedServices().size();
}

QString DaemonService::lastRegistrationError() const {
    return m_lastRegistrationError;
}

} // namespace RokuDeck

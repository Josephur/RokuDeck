// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_service.h"

#include "config_store.h"
#include "core/qprocess_runner.h"
#include "rokudeck1_adaptor.h"

#include <QCoreApplication>
#include <QDBusError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
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

} // namespace

DaemonService::DaemonService(QDBusConnection connection,
                             std::function<bool()> hasEnabledAutomations,
                             std::unique_ptr<ProcessRunner> processRunner,
                             std::unique_ptr<ConfigStore> configStore, QObject* parent)
    : QObject(parent), m_connection(std::move(connection)),
      m_hasEnabledAutomations(std::move(hasEnabledAutomations)),
      m_configStore(configStore ? std::move(configStore) : std::make_unique<ConfigStore>()),
      m_processRunner(processRunner ? std::move(processRunner)
                                    : std::make_unique<QProcessRunner>()),
      m_rdsClient(m_processRunner.get()), m_ownerWatcher(this) {
    new RokuDeck1Adaptor(this);
    m_ownerWatcher.setConnection(m_connection);
    m_ownerWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    for (const DeviceRecord& device : m_configStore->loadDevices()) {
        m_deviceRegistry.upsert(device);
    }

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(static_cast<int>(idleGracePeriod.count()));
    connect(&m_idleTimer, &QTimer::timeout, this, [] {
        if (QCoreApplication::instance()) {
            QCoreApplication::quit();
        }
    });

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
    if (m_registeredOnBus) {
        return true;
    }
    if (!m_connection.isConnected() ||
        !m_connection.registerObject(QString::fromLatin1(objectPath), this,
                                     QDBusConnection::ExportScriptableSlots |
                                         QDBusConnection::ExportScriptableSignals)) {
        return false;
    }
    if (!m_connection.registerService(QString::fromLatin1(serviceName))) {
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
    m_deviceRegistry.upsert(device);
    m_configStore->saveDevices(m_deviceRegistry.devices());
    Q_EMIT DeviceListChanged(ListDevices());
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

QString DaemonService::DiscoverDevices() const {
    return ListDevices();
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

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(commandJson.toUtf8(), &parseError);
    const QJsonArray argumentValues =
        document.isObject() ? document.object().value(QStringLiteral("arguments")).toArray()
                            : QJsonArray();
    QStringList arguments;
    for (const QJsonValue& value : argumentValues) {
        if (!value.isString() || value.toString().isEmpty()) {
            arguments.clear();
            break;
        }
        arguments.append(value.toString());
    }
    if (parseError.error != QJsonParseError::NoError || arguments.isEmpty()) {
        QTimer::singleShot(0, this, [this, requestId, serial] {
            finishCommandWithError(requestId, serial, QStringLiteral("invalidCommand"));
        });
        return requestId;
    }

    m_commandQueues[serial].enqueue({requestId, serial, arguments});
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
        finishCommandWithError(command.requestId, serial, QStringLiteral("unknownDevice"));
        processNextCommand(serial);
        return;
    }

    const QueuedCommand command = queueIt->head();
    ProcessRequest request;
    request.program = QStringLiteral("rds");
    request.arguments = {QStringLiteral("--json"),    QStringLiteral("--quiet"),
                         QStringLiteral("--timeout"), QStringLiteral("3000"),
                         QStringLiteral("command"),   device->address};
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
        QPointer<DaemonService> service(this);
        m_rdsClient.queryDevice(address, [service, claim, address](RdsQueryResult result) {
            if (!service || service->m_shuttingDown) {
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
            service->m_snapshots.insert(claim.serial, snapshot);
            service->m_deviceRegistry.upsert({claim.serial, snapshot.displayName, address});
            service->m_configStore->saveDevices(service->m_deviceRegistry.devices());
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
    updateIdleTimer();
}

void DaemonService::automationEnablementChanged() {
    updateIdleTimer();
}

void DaemonService::updateIdleTimer() {
    if (m_clients.isEmpty() && !m_hasEnabledAutomations()) {
        m_idleTimer.start();
    } else {
        m_idleTimer.stop();
    }
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
    return static_cast<int>(idleGracePeriod.count());
}

} // namespace RokuDeck

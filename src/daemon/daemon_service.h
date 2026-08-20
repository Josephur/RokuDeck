// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "client_registry.h"
#include "config_store.h"
#include "core/device_registry.h"
#include "core/device_state.h"
#include "core/poll_scheduler.h"
#include "core/process_runner.h"
#include "core/rds_client.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QDeadlineTimer>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace RokuDeck {

class DaemonService final : public QObject, protected QDBusContext {
    Q_OBJECT

  public:
    using DeadlineRemaining = std::function<qint64(const QDeadlineTimer&)>;

    explicit DaemonService(
        QDBusConnection connection = QDBusConnection::sessionBus(),
        std::function<bool()> hasEnabledAutomations = [] { return false; },
        std::unique_ptr<ProcessRunner> processRunner = {},
        std::unique_ptr<ConfigStore> configStore = {},
        std::chrono::milliseconds idleGrace = std::chrono::seconds(30),
        DeadlineRemaining deadlineRemaining = {}, QObject* parent = nullptr);
    ~DaemonService() override;

    [[nodiscard]] bool registerOnBus();
    void upsertDevice(const DeviceRecord& device);
    void automationEnablementChanged();

    [[nodiscard]] quint32 effectiveInterval(const QString& serial) const;
    [[nodiscard]] bool idleTimerActive() const;
    [[nodiscard]] int idleGracePeriodMs() const;
    [[nodiscard]] qsizetype watchedClientOwnerCount() const;
    [[nodiscard]] QString lastRegistrationError() const;

  public Q_SLOTS:
    [[nodiscard]] QString ListDevices() const;
    [[nodiscard]] QString GetSnapshot(const QString& serial) const;
    void RegisterClient(const QString& clientId, const QStringList& serials, uint intervalMs);
    void UpdateClientInterest(const QString& clientId, const QStringList& serials, uint intervalMs);
    void UnregisterClient(const QString& clientId);
    [[nodiscard]] QString DiscoverDevices();
    [[nodiscard]] QString SendCommand(const QString& serial, const QString& commandJson);

  Q_SIGNALS:
    void DeviceSnapshotChanged(const QString& serial, const QString& snapshotJson);
    void DeviceListChanged(const QString& devicesJson);
    void CommandFinished(const QString& requestId, const QString& resultJson);
    void idleExitRequested();

  private Q_SLOTS:
    void removeClientsForOwner(const QString& owner);
    void pollDueDevices();
    void handleIdleTimeout();

  private:
    struct QueuedCommand {
        QString requestId;
        QString serial;
        QStringList arguments;
        QDeadlineTimer deadline;
    };

    struct ValidatedCommand {
        QStringList arguments;
        QString errorCode;
    };

    [[nodiscard]] QString callerOwner() const;
    [[nodiscard]] std::optional<DeviceRecord> findDevice(const QString& serial) const;
    void removePollInterests(const ClientRegistration& registration);
    void applyPollInterests(const ClientRegistration& registration);
    void watchOwner(const QString& owner);
    void updateIdleTimer();
    [[nodiscard]] bool hasAcceptedWork() const;
    [[nodiscard]] bool mergeDevice(const DeviceRecord& device);
    [[nodiscard]] ValidatedCommand validateCommand(const QString& serial,
                                                   const QString& commandJson) const;
    void processNextCommand(const QString& serial);
    void finishCommand(const QueuedCommand& command, const ProcessResult& result);
    void finishCommandWithError(const QString& requestId, const QString& serial,
                                const QString& errorCode);

    [[nodiscard]] static QString devicesJson(const QList<DeviceRecord>& devices);
    [[nodiscard]] static QString snapshotJson(const DeviceSnapshot& snapshot);

    QDBusConnection m_connection;
    std::function<bool()> m_hasEnabledAutomations;
    DeadlineRemaining m_deadlineRemaining;
    std::chrono::milliseconds m_idleGrace;
    std::unique_ptr<ConfigStore> m_configStore;
    std::unique_ptr<ProcessRunner> m_processRunner;
    DeviceRegistry m_deviceRegistry;
    PollScheduler m_pollScheduler;
    RdsClient m_rdsClient;
    ClientRegistry m_clients;
    QDBusServiceWatcher m_ownerWatcher;
    QTimer m_idleTimer;
    QTimer m_pollTimer;
    QHash<QString, DeviceSnapshot> m_snapshots;
    QHash<QString, quint64> m_addressGenerations;
    QHash<QString, QQueue<QueuedCommand>> m_commandQueues;
    QSet<QString> m_activeCommandSerials;
    bool m_registeredOnBus = false;
    bool m_discoveryActive = false;
    bool m_shuttingDown = false;
    QString m_lastRegistrationError;
};

} // namespace RokuDeck

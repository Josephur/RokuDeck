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
    Q_CLASSINFO("D-Bus Interface", "io.github.josephur.RokuDeck1")

  public:
    explicit DaemonService(
        QDBusConnection connection = QDBusConnection::sessionBus(),
        std::function<bool()> hasEnabledAutomations = [] { return false; },
        std::unique_ptr<ProcessRunner> processRunner = {},
        std::unique_ptr<ConfigStore> configStore = {}, QObject* parent = nullptr);
    ~DaemonService() override;

    [[nodiscard]] bool registerOnBus();
    void upsertDevice(const DeviceRecord& device);
    void automationEnablementChanged();

    [[nodiscard]] quint32 effectiveInterval(const QString& serial) const;
    [[nodiscard]] bool idleTimerActive() const;
    [[nodiscard]] int idleGracePeriodMs() const;

  public Q_SLOTS:
    Q_SCRIPTABLE [[nodiscard]] QString ListDevices() const;
    Q_SCRIPTABLE [[nodiscard]] QString GetSnapshot(const QString& serial) const;
    Q_SCRIPTABLE void RegisterClient(const QString& clientId, const QStringList& serials,
                                     uint intervalMs);
    Q_SCRIPTABLE void UpdateClientInterest(const QString& clientId, const QStringList& serials,
                                           uint intervalMs);
    Q_SCRIPTABLE void UnregisterClient(const QString& clientId);
    Q_SCRIPTABLE [[nodiscard]] QString DiscoverDevices() const;
    Q_SCRIPTABLE [[nodiscard]] QString SendCommand(const QString& serial,
                                                   const QString& commandJson);

  Q_SIGNALS:
    Q_SCRIPTABLE void DeviceSnapshotChanged(const QString& serial, const QString& snapshotJson);
    Q_SCRIPTABLE void DeviceListChanged(const QString& devicesJson);
    Q_SCRIPTABLE void CommandFinished(const QString& requestId, const QString& resultJson);

  private Q_SLOTS:
    void removeClientsForOwner(const QString& owner);
    void pollDueDevices();

  private:
    struct QueuedCommand {
        QString requestId;
        QString serial;
        QStringList arguments;
    };

    [[nodiscard]] QString callerOwner() const;
    [[nodiscard]] std::optional<DeviceRecord> findDevice(const QString& serial) const;
    void removePollInterests(const ClientRegistration& registration);
    void applyPollInterests(const ClientRegistration& registration);
    void watchOwner(const QString& owner);
    void updateIdleTimer();
    void processNextCommand(const QString& serial);
    void finishCommand(const QueuedCommand& command, const ProcessResult& result);
    void finishCommandWithError(const QString& requestId, const QString& serial,
                                const QString& errorCode);

    [[nodiscard]] static QString devicesJson(const QList<DeviceRecord>& devices);
    [[nodiscard]] static QString snapshotJson(const DeviceSnapshot& snapshot);

    static constexpr std::chrono::milliseconds idleGracePeriod{30000};

    QDBusConnection m_connection;
    std::function<bool()> m_hasEnabledAutomations;
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
    QHash<QString, QQueue<QueuedCommand>> m_commandQueues;
    QSet<QString> m_activeCommandSerials;
    bool m_registeredOnBus = false;
    bool m_shuttingDown = false;
};

} // namespace RokuDeck

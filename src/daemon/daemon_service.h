// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "client_registry.h"
#include "config_store.h"
#include "core/device_registry.h"
#include "core/device_state.h"
#include "core/poll_scheduler.h"
#include "core/process_runner.h"
#include "core/rds_client.h"
#include "core/rule_model.h"
#include "core/rule_scheduler.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDeadlineTimer>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace RokuDeck {

class DaemonService final : public QObject, protected QDBusContext {
    Q_OBJECT

  public:
    using DeadlineRemaining = std::function<qint64(const QDeadlineTimer&)>;
    using WallClock = std::function<QDateTime()>;

    explicit DaemonService(
        QDBusConnection connection = QDBusConnection::sessionBus(),
        std::function<bool()> hasEnabledAutomations = [] { return false; },
        std::unique_ptr<ProcessRunner> processRunner = {},
        std::unique_ptr<ConfigStore> configStore = {},
        std::chrono::milliseconds idleGrace = std::chrono::seconds(30),
        DeadlineRemaining deadlineRemaining = {},
        WallClock wallClock = {},
        std::chrono::milliseconds localPcPollInterval = std::chrono::seconds(2),
        QObject* parent = nullptr);
    ~DaemonService() override;

    [[nodiscard]] bool registerOnBus();
    void upsertDevice(const DeviceRecord& device);
    void automationEnablementChanged();

    [[nodiscard]] quint32 effectiveInterval(const QString& serial) const;
    [[nodiscard]] bool idleTimerActive() const;
    [[nodiscard]] int idleGracePeriodMs() const;
    [[nodiscard]] qsizetype watchedClientOwnerCount() const;
    [[nodiscard]] QString lastRegistrationError() const;
    [[nodiscard]] bool pollingEnabled() const;
    [[nodiscard]] QString pollingDiagnostic() const;
    void evaluateDueRules();
    // Fires one rule through the §4 mode gate (single/restart/queued) and, if
    // admitted, runs it to completion — draining a queued firing when one
    // arrived mid-run. Public because it is the seam the mode tests drive
    // re-entrantly; every trigger origin (scheduled, stateChange, D-Bus
    // RunRule) routes through here. Returns the most recent executed run's
    // outcome, "dropped" when this firing was suppressed, empty when it was
    // parked in the queued slot and returned before running.
    QString requestRuleFire(const QString& ruleId);

  public Q_SLOTS:
    [[nodiscard]] QString ListDevices() const;
    [[nodiscard]] QString GetSnapshot(const QString& serial) const;
    void RegisterClient(const QString& clientId, const QStringList& serials, uint intervalMs);
    void UpdateClientInterest(const QString& clientId, const QStringList& serials, uint intervalMs);
    void UnregisterClient(const QString& clientId);
    [[nodiscard]] QString DiscoverDevices();
    [[nodiscard]] QString AddDevice(const QString& deviceJson);
    [[nodiscard]] QString UpdateDevice(const QString& serial, const QString& changesJson);
    void RemoveDevice(const QString& serial);
    void SetDeviceOrder(const QStringList& serials);
    [[nodiscard]] QString TestDevice(const QString& address);
    [[nodiscard]] QString SendCommand(const QString& serial, const QString& commandJson);
    [[nodiscard]] QString ListRules() const;
    [[nodiscard]] QString AddRule(const QString& ruleJson);
    void UpdateRule(const QString& ruleJson);
    void DeleteRule(const QString& ruleId);
    void SetRuleEnabled(const QString& ruleId, bool enabled);
    [[nodiscard]] QString RunRule(const QString& ruleId);

  Q_SIGNALS:
    void DeviceSnapshotChanged(const QString& serial, const QString& snapshotJson);
    void DeviceListChanged(const QString& devicesJson);
    void CommandFinished(const QString& requestId, const QString& resultJson);
    void RulesChanged(const QString& rulesJson);
    void idleExitRequested();

  private Q_SLOTS:
    void removeClientsForOwner(const QString& owner);
    void pollDueDevices();
    // Spec §2.1: every 2 s read the default PipeWire sink via wpctl and feed
    // the result through the same snapshot pipeline as a Roku poll.
    void pollLocalPcAudio();
    // Spec §2.2: org.mpris.MediaPlayer2.* registrations and losses arrive as
    // NameOwnerChanged deliveries from the bus daemon.
    void mprisNameOwnerChanged(const QString& name, const QString& oldOwner,
                               const QString& newOwner);
    // PlaybackStatus flips arrive without an attributable sender here; the
    // handler re-queries every tracked player instead.
    void mprisPropertiesChanged(const QString& interfaceName,
                                const QVariantMap& changedProperties,
                                const QStringList& invalidatedProperties);
    void handleIdleTimeout();

  private:
    struct QueuedCommand {
        QString requestId;
        QString serial;
        QStringList arguments;
        QDeadlineTimer deadline;
        // Automation dispatches wait on their delivery verdicts to build an
        // honest last-run outcome; widget-issued commands have no waiter.
        bool trackDelivery = false;
        // The binary the queued arguments run against: "rds" for Roku ECP,
        // "wpctl" for absolute This PC audio control (spec §4).
        QString program = QStringLiteral("rds");
    };

    struct ValidatedCommand {
        QStringList arguments;
        QString errorCode;
    };

    // Settings written by the user through the device-management API. Discovery
    // and polling only know a device's identity, so they must never supply one.
    enum class MergeMode { PreserveSettings, ApplySettings };

    struct ValidatedRecord {
        DeviceRecord device;
        QString errorMessage;
    };

    // Outcome of one manual rule run over its consenting targets. accepted
    // means every attempted dispatch succeeded; reason carries "partial" when
    // some dispatches were rejected, or the terminal failure cause otherwise.
    // cycleDetected marks a runRule branch cut for re-entering an ancestor.
    struct ActionResult {
        bool accepted;
        QString reason;
        int dispatched = 0;
        int rejected = 0;
        int deliveryFailures = 0;
        bool cycleDetected = false;
    };

    [[nodiscard]] QString callerOwner() const;
    [[nodiscard]] std::optional<DeviceRecord> findDevice(const QString& serial) const;
    void removePollInterests(const ClientRegistration& registration);
    void applyPollInterests(const ClientRegistration& registration);
    void watchOwner(const QString& owner);
    void updateIdleTimer();
    [[nodiscard]] bool hasAcceptedWork() const;
    // True when any enabled rule names LOCAL-PC or uses the wildcard, which
    // keeps the daemon alive per the PC-state-source design (§5 idle parity).
    [[nodiscard]] bool hasEnabledLocalPcRules() const;
    [[nodiscard]] bool mergeDevice(const DeviceRecord& device,
                                   MergeMode mode = MergeMode::PreserveSettings);
    [[nodiscard]] ValidatedRecord validateDeviceJson(const QString& deviceJson,
                                                     const std::optional<DeviceRecord>& base) const;
    [[nodiscard]] int nextSortIndex() const;
    [[nodiscard]] std::chrono::milliseconds policyAdjustedInterval(const QString& serial,
                                                         std::chrono::milliseconds requested) const;
    void reapplyPollInterests();
    void persistAndAnnounceDevices();
    void finishDeviceTest(const QString& address, const QDBusMessage& reply);
    [[nodiscard]] ValidatedCommand validateCommand(const QString& serial,
                                                   const QString& commandJson) const;
    void processNextCommand(const QString& serial);
    void finishCommand(const QueuedCommand& command, const ProcessResult& result);
    void finishCommandWithError(const QString& requestId, const QString& serial,
                                const QString& errorCode, bool trackDelivery = false);

    [[nodiscard]] QString rulesJson() const;
    // Spec §2.2: track every org.mpris.MediaPlayer2.* player on the session
    // bus — an initial Get on appearance plus PropertiesChanged subscriptions
    // — and keep `playing` true while any of them reports Playing.
    void mprisPlayerAppeared(const QString& service);
    void mprisPlayerVanished(const QString& service);
    // Issues one async org.freedesktop.DBus.Properties.Get for PlaybackStatus.
    void queryMprisStatus(const QString& service);
    void storeMprisStatus(const QString& service, const QVariant& value);
    void recomputeMprisPlaying();    [[nodiscard]] QString nextRuleId() const;
    void announceRules();
    [[nodiscard]] QList<RuleDocument>::iterator findRule(const QString& ruleId);
    [[nodiscard]] ActionResult executeActions(const RuleDocument& rule,
                                               const QStringList& targets,
                                               const QStringList& callStack,
                                               const QString& currentRuleId,
                                               const std::shared_ptr<std::atomic_bool>& cancelToken
                                                   = {});
    // Walks one authored action list against the consenting targets. Returns
    // false when an action type cannot be executed yet, failing the whole run
    // before anything dispatches; per-target rejections are counted instead.
    // currentRuleId owns `actions`; callStack carries the ancestor ids of
    // nested runRule dispatches, so a macro naming any of them (or this rule)
    // is a cycle: that branch counts as one failure and reports cycleDetected.
    // cancelToken (the owning run's) is checked between actions and inside
    // delay waits; once set, the remaining actions are skipped without error.
    [[nodiscard]] bool dispatchActionList(const QList<ActionNode>& actions,
                                           const QStringList& targets,
                                           const QStringList& callStack,
                                           const QString& currentRuleId, int& dispatched,
                                           int& rejected, int& deliveryFailures,
                                           QStringList& pendingRequests, bool& cycleDetected,
                                           const std::shared_ptr<std::atomic_bool>& cancelToken
                                               = {});
    void rearmScheduleTimer();
    // Consumes one completed wpctl reading: healthy output becomes an Online
    // snapshot, and the third consecutive failure flips health to Offline.
    void finishLocalPcPoll(const ProcessResult& result);
    // Shared insert path with the rds poll tail: baseline bookkeeping,
    // DeviceSnapshotChanged announcement, then edge firings (§4.1).
    void applyLocalPcSnapshot(DeviceSnapshot snapshot);
    // Runs one rule to completion and records its outcome. callStack carries
    // the ids of the enclosing macro runs; runRule steps recurse synchronously
    // with the ancestry extended by this rule's own id. A set cancelToken (a
    // restart superseded this run) aborts the remainder and skips both the
    // last-run writeback and the announcement — the replacing run owns the
    // rule's observable history.
    QString executeRuleRun(const QString& ruleId, const QStringList& callStack = {},
                           const std::shared_ptr<std::atomic_bool>& cancelToken = {});
    void runStateChangeFirings(const QStringList& ruleIds);
    // Bookends one gated run. Cleanup is ownership-aware: a superseded frame
    // unwinds through its replacement's event pumps, so the shared entries
    // are only released when this run's own token is still the registered
    // one — otherwise they describe the live replacement.
    void finishRuleRun(const QString& ruleId,
                       const std::shared_ptr<std::atomic_bool>& token);
    // Records a mode-suppressed firing: only the lastRun fields are written
    // behind the find-rule guard (never a whole-struct clobber), then the
    // rules are announced — mirroring the targeted-writeback pattern.
    void recordDroppedFiring(const QString& ruleId);
    [[nodiscard]] static QString devicesJson(const QList<DeviceRecord>& devices);
    [[nodiscard]] static QJsonObject deviceObject(const DeviceRecord& device);
    [[nodiscard]] static QString snapshotJson(const DeviceSnapshot& snapshot,
                                              const QString& diagnostic = {});

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
    QSet<QString> m_mprisPlayers;
    QHash<QString, QString> m_mprisPlaybackStatuses;
    bool m_mprisPlaying = false;
    WallClock m_wallClock;
    QTimer m_idleTimer;
    QTimer m_pollTimer;
    QTimer m_pcPollTimer;
    int m_pcPollFailures = 0;
    bool m_pcPollInFlight = false;
    QTimer m_scheduleTimer;
    QList<RuleDocument> m_rules;
    // Delivery verdicts (true = rds acknowledged) keyed by requestId, written
    // by finishCommand/finishCommandWithError and consumed by rule runs so a
    // run's outcome reflects what actually happened on the devices.
    QHash<QString, bool> m_deliveryOutcomes;
    // Per-rule run-state machine for the §4 mode gate: 0 = idle, 1 = active,
    // 2 = one firing queued behind the active run. Each active run also owns
    // a cancellation token a restart firing flips to supersede it.
    enum { kRunIdle = 0, kRunActive = 1, kRunQueued = 2 };
    QHash<QString, int> m_runStates;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_runTokens;
    RuleScheduler m_ruleScheduler;
    QHash<QString, DeviceSnapshot> m_snapshots;
    QHash<QString, quint64> m_addressGenerations;
    QList<QDBusMessage> m_discoveryReplies;
    QHash<QString, QList<QDBusMessage>> m_deviceTestReplies;
    QHash<QString, QQueue<QueuedCommand>> m_commandQueues;
    QSet<QString> m_activeCommandSerials;
    bool m_registeredOnBus = false;
    bool m_discoveryActive = false;
    bool m_pollingEnabled = true;
    bool m_shuttingDown = false;
    bool m_dispatchActive = false;
    QString m_lastRegistrationError;
    QString m_pollingDiagnostic;
};

} // namespace RokuDeck

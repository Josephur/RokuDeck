// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <memory>

class IoGithubJosephurRokuDeck1Interface;
class QDBusPendingCall;
class QDBusServiceWatcher;

namespace RokuDeck {

class AutomationsModel;
class DevicesModel;
struct DeviceStateSnapshot;

class DaemonProxy final : public QObject {
    Q_OBJECT

  public:
    // The automation model is optional so existing embedders (and tests that
    // only exercise devices) can construct a proxy without one.
    explicit DaemonProxy(DevicesModel* devices, QString clientId,
                         AutomationsModel* rules = nullptr, QObject* parent = nullptr);
    ~DaemonProxy() override;

    [[nodiscard]] bool isAvailable() const;

    void start();
    void stop();
    void refresh();
    void sendCommand(const QString& serial, const QString& action, const QVariant& value);
    void addDevice(const QVariantMap& device);
    void updateDevice(const QString& serial, const QVariantMap& changes);
    void removeDevice(const QString& serial);
    void setDeviceOrder(const QStringList& serials);
    void testDevice(const QString& address);
    void discoverDevices();
    void addRule(const QString& ruleJson);
    void updateRule(const QString& ruleJson);
    void setRuleEnabled(const QString& ruleId, bool enabled);
    void deleteRule(const QString& ruleId);
    void runRule(const QString& ruleId);

    // The complete definition the daemon last announced for a rule, so
    // editors can round-trip documents without owning any rule state.
    [[nodiscard]] QVariantMap ruleDocument(const QString& ruleId) const;

  Q_SIGNALS:
    void availableChanged(bool available);
    void errorOccurred(const QString& message);
    void deviceOperationFailed(const QString& operation, const QString& message);
    void deviceOperationSucceeded(const QString& operation);
    void deviceTestFinished(const QString& address, const QVariantMap& snapshot,
                            const QString& error);
    void discoveryFinished(const QString& error);
    void ruleRunFinished(const QString& ruleId, const QString& outcome);
    void busyChanged();

  private:
    // An optimistic mute toggle waiting for a snapshot to confirm or refute
    // it. A mute keypress toggles the television's state, so its effect is
    // predictable: the opposite of the last reported value. armMuteExpectation()
    // flips the model the moment the daemon accepts the command; every view
    // mirrors it instantly instead of waiting out the post-command pull.
    struct MuteExpectation {
        QString requestId;
        bool expected = false;
        bool previous = false;
        QDateTime setAt;
    };
    void armMuteExpectation(const QString& serial, const QString& requestId,
                            const QString& action, const QVariant& value);
    void reconcileMuteExpectation(const QString& serial, DeviceStateSnapshot& snapshot);
    [[nodiscard]] bool revertMuteExpectation(const QString& serial, const QString& requestId);
    void handleServiceOwnerChanged(const QString& service, const QString& oldOwner,
                                   const QString& newOwner);
    void applyDeviceListJson(const QString& devicesJson);
    void applyRulesJson(const QString& rulesJson);
    void fetchRules();
    void applySnapshotJson(const QString& expectedSerial, const QString& snapshotJson);
    void requestSnapshot(const QString& serial);
    void updateRegistration();
    void setAvailable(bool available);
    void clearPendingCommands();
    void handleCommandFinished(const QString& requestId, const QString& resultJson);
    void watchDeviceCall(const QString& operation, const QDBusPendingCall& call);
    void setBusyCount(int count);

  public:
    [[nodiscard]] bool isBusy() const;

  private:

    DevicesModel* const m_devices;
    AutomationsModel* const m_rules;
    const QString m_clientId;
    std::unique_ptr<IoGithubJosephurRokuDeck1Interface> m_interface;
    std::unique_ptr<QDBusServiceWatcher> m_ownerWatcher;
    QHash<QString, QString> m_requestSerials;
    QHash<QString, MuteExpectation> m_muteExpectations;
    QHash<QString, QDateTime> m_lastSnapshotAt;
    QHash<QString, QJsonObject> m_ruleDocuments;
    QStringList m_registeredSerials;
    quint64 m_generation = 0;
    int m_busyCount = 0;
    bool m_active = false;
    bool m_available = false;
    bool m_registered = false;
    bool m_registrationPending = false;
};

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "automationsmodel.h"
#include "daemonproxy.h"
#include "devicesmodel.h"

#include <Plasma/Applet>

#include <KPluginMetaData>

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace RokuDeck {

class RokuDeckApplet final : public Plasma::Applet {
    Q_OBJECT

    Q_PROPERTY(DevicesModel* devices READ devices CONSTANT)
    Q_PROPERTY(AutomationsModel* automations READ automations CONSTANT)
    Q_PROPERTY(QString selectedSerial READ selectedSerial WRITE setSelectedSerial NOTIFY
                   selectedSerialChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

  public:
    explicit RokuDeckApplet(QObject* parent, const KPluginMetaData& data, const QVariantList& args);
    ~RokuDeckApplet() override;

    [[nodiscard]] DevicesModel* devices();
    [[nodiscard]] AutomationsModel* automations();
    Q_INVOKABLE [[nodiscard]] QVariantList deviceEntries() const;
    [[nodiscard]] QString selectedSerial() const;
    void setSelectedSerial(const QString& serial);
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void sendCommand(const QString& serial, const QString& action,
                                 const QVariant& value = {});
    Q_INVOKABLE void addDevice(const QVariantMap& device);
    Q_INVOKABLE void updateDevice(const QString& serial, const QVariantMap& changes);
    Q_INVOKABLE void removeDevice(const QString& serial);
    Q_INVOKABLE void setDeviceOrder(const QStringList& serials);
    Q_INVOKABLE void testDevice(const QString& address);
    Q_INVOKABLE void discoverDevices();
    Q_INVOKABLE void setRuleEnabled(const QString& ruleId, bool enabled);
    Q_INVOKABLE void deleteRule(const QString& ruleId);
    Q_INVOKABLE void runRule(const QString& ruleId);
    Q_INVOKABLE void addRule(const QString& ruleJson);
    Q_INVOKABLE void updateRule(const QString& ruleJson);
    Q_INVOKABLE QVariantMap ruleDocument(const QString& ruleId) const;
    // Re-fetches the rule from the daemon synchronously (editors open against
    // the live document, never a stale announcement cache).
    Q_INVOKABLE [[nodiscard]] QVariantMap fetchRuleDocument(const QString& ruleId);
    Q_INVOKABLE void copyToClipboard(const QString& text);
    // Choices for the editor's runRule step; the rule being edited is
    // excluded so a macro cannot call itself.
    Q_INVOKABLE [[nodiscard]] QVariantList ruleEntries(const QString& excludingId = {}) const;

    // Peer management (M9a): listener toggle, pairing both directions, and
    // the listing the Peers config page renders.
    Q_INVOKABLE void setPeerListenerEnabled(bool enabled);
    Q_INVOKABLE void pairPeer(const QString& address, uint port, const QString& code);
    Q_INVOKABLE void removePeerTarget(const QString& machineId);
    Q_INVOKABLE void revokePairedController(const QString& machineId);
    Q_INVOKABLE [[nodiscard]] QString peerPairingCode();
    Q_INVOKABLE [[nodiscard]] QVariantMap listPeers();

  Q_SIGNALS:
    void selectedSerialChanged();
    void availableChanged();
    void lastErrorChanged();
    void deviceOperationFailed(const QString& operation, const QString& message);
    void deviceOperationSucceeded(const QString& operation);
    void deviceTestFinished(const QString& address, const QVariantMap& snapshot,
                            const QString& error);
    void discoveryFinished(const QString& error);
    void ruleRunFinished(const QString& ruleId, const QString& outcome);

  private:
    void ensureSelectedSerial();
    void setLastError(const QString& message);

    DevicesModel m_devices;
    AutomationsModel m_automations;
    DaemonProxy m_daemon;
    QString m_selectedSerial;
    QString m_lastError;
};

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

class DevicesModel;

class DaemonProxy final : public QObject {
    Q_OBJECT

  public:
    explicit DaemonProxy(DevicesModel* devices, QString clientId, QObject* parent = nullptr);
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

  Q_SIGNALS:
    void availableChanged(bool available);
    void errorOccurred(const QString& message);
    void deviceOperationFailed(const QString& operation, const QString& message);
    void deviceOperationSucceeded(const QString& operation);
    void deviceTestFinished(const QString& address, const QVariantMap& snapshot,
                            const QString& error);
    void discoveryFinished(const QString& error);
    void busyChanged();

  private:
    void handleServiceOwnerChanged(const QString& service, const QString& oldOwner,
                                   const QString& newOwner);
    void applyDeviceListJson(const QString& devicesJson);
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
    const QString m_clientId;
    std::unique_ptr<IoGithubJosephurRokuDeck1Interface> m_interface;
    std::unique_ptr<QDBusServiceWatcher> m_ownerWatcher;
    QHash<QString, QString> m_requestSerials;
    QHash<QString, QSet<QString>> m_awaitingSnapshot;
    QStringList m_registeredSerials;
    quint64 m_generation = 0;
    int m_busyCount = 0;
    bool m_active = false;
    bool m_available = false;
    bool m_registered = false;
    bool m_registrationPending = false;
};

} // namespace RokuDeck

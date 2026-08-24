// SPDX-License-Identifier: GPL-3.0-or-later

#include <KLocalizedQmlContext>

#include <Plasma/Plasma>

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <QtQuickTest>

class FixtureDevicesModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        SerialRole = Qt::UserRole + 1,
        NameRole,
        AddressRole,
        HealthRole,
        PowerRole,
        ScreensaverRole,
        ActiveLabelRole,
        VolumeRole,
        MutedRole,
        CapabilitiesRole,
        StaleRole,
        PendingCommandRole,
        AllowAutomationsRole,
        WakeOnWlanRole,
        RefreshPolicyRole,
        VolumeCeilingRole,
    };

    struct Device {
        QString serial;
        QString name;
        QString address;
        QString health;
        QString power;
        QString screensaver;
        QString activeLabel;
        QVariant volume;
        bool muted = false;
        QStringList capabilities;
        bool stale = false;
        bool pendingCommand = false;
        bool allowAutomations = true;
        bool wakeOnWlan = false;
        QString refreshPolicy = QStringLiteral("balanced");
        int volumeCeiling = 100;
    };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_devices.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
            return {};
        }
        const Device& device = m_devices.at(index.row());
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
            return device.volume;
        case MutedRole:
            return device.muted;
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
            return device.pendingCommand;
        default:
            return {};
        }
    }

    [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
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
                {PendingCommandRole, "pendingCommand"},
                {AllowAutomationsRole, "allowAutomations"},
                {WakeOnWlanRole, "wakeOnWlan"},
                {RefreshPolicyRole, "refreshPolicy"},
                {VolumeCeilingRole, "volumeCeiling"}};
    }

    void replace(const QVariantList& entries) {
        beginResetModel();
        m_devices.clear();
        m_devices.reserve(entries.size());
        for (const QVariant& entry : entries) {
            const QVariantMap values = entry.toMap();
            Device device;
            device.serial = values.value(QStringLiteral("serial"),
                                         QStringLiteral("SERIAL-TEST"))
                                .toString();
            device.name =
                values.value(QStringLiteral("name"), QStringLiteral("Test Roku")).toString();
            device.address = values
                                 .value(QStringLiteral("address"),
                                        QStringLiteral("192.0.2.10"))
                                 .toString();
            device.health =
                values.value(QStringLiteral("health"), QStringLiteral("online")).toString();
            device.power =
                values.value(QStringLiteral("power"), QStringLiteral("on")).toString();
            device.screensaver = values
                                     .value(QStringLiteral("screensaver"),
                                            QStringLiteral("inactive"))
                                     .toString();
            device.activeLabel = values
                                     .value(QStringLiteral("activeLabel"),
                                            QStringLiteral("Home"))
                                     .toString();
            device.volume = values.value(QStringLiteral("volume"), 12);
            device.muted = values.value(QStringLiteral("muted"), false).toBool();
            device.capabilities = values.value(QStringLiteral("capabilities")).toStringList();
            device.stale = values.value(QStringLiteral("stale"), false).toBool();
            device.pendingCommand =
                values.value(QStringLiteral("pendingCommand"), false).toBool();
            device.allowAutomations =
                values.value(QStringLiteral("allowAutomations"), true).toBool();
            device.wakeOnWlan = values.value(QStringLiteral("wakeOnWlan"), false).toBool();
            device.refreshPolicy = values
                                       .value(QStringLiteral("refreshPolicy"),
                                              QStringLiteral("balanced"))
                                       .toString();
            device.volumeCeiling = values.value(QStringLiteral("volumeCeiling"), 100).toInt();
            m_devices.append(device);
        }
        endResetModel();
    }

    Q_INVOKABLE void setFixtureProperty(int row, const QString& roleName,
                                        const QVariant& value) {
        if (row < 0 || row >= m_devices.size()) {
            return;
        }
        Device& device = m_devices[row];
        int role = -1;
        if (roleName == QStringLiteral("capabilities")) {
            device.capabilities = value.toStringList();
            role = CapabilitiesRole;
        } else if (roleName == QStringLiteral("pendingCommand")) {
            device.pendingCommand = value.toBool();
            role = PendingCommandRole;
        } else if (roleName == QStringLiteral("health")) {
            device.health = value.toString();
            role = HealthRole;
        } else if (roleName == QStringLiteral("power")) {
            device.power = value.toString();
            role = PowerRole;
        } else if (roleName == QStringLiteral("screensaver")) {
            device.screensaver = value.toString();
            role = ScreensaverRole;
        } else if (roleName == QStringLiteral("stale")) {
            device.stale = value.toBool();
            role = StaleRole;
        } else if (roleName == QStringLiteral("muted")) {
            device.muted = value.toBool();
            role = MutedRole;
        } else if (roleName == QStringLiteral("allowAutomations")) {
            device.allowAutomations = value.toBool();
            role = AllowAutomationsRole;
        } else if (roleName == QStringLiteral("wakeOnWlan")) {
            device.wakeOnWlan = value.toBool();
            role = WakeOnWlanRole;
        } else if (roleName == QStringLiteral("refreshPolicy")) {
            device.refreshPolicy = value.toString();
            role = RefreshPolicyRole;
        } else if (roleName == QStringLiteral("volumeCeiling")) {
            device.volumeCeiling = value.toInt();
            role = VolumeCeilingRole;
        }
        if (role >= 0) {
            const QModelIndex changed = index(row, 0);
            Q_EMIT dataChanged(changed, changed, {role});
        }
    }

  private:
    QList<Device> m_devices;
};

class BridgeFixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(FixtureDevicesModel* devices READ devices CONSTANT)
    Q_PROPERTY(QString selectedSerial READ selectedSerial WRITE setSelectedSerial NOTIFY
                   selectedSerialChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError CONSTANT)
    Q_PROPERTY(int refreshCount READ refreshCount NOTIFY refreshCountChanged)
    Q_PROPERTY(QVariantList sentCommands READ sentCommands NOTIFY sentCommandsChanged)
    Q_PROPERTY(QVariantList addedDevices READ addedDevices NOTIFY addedDevicesChanged)
    Q_PROPERTY(QVariantList updatedDevices READ updatedDevices NOTIFY updatedDevicesChanged)
    Q_PROPERTY(QStringList removedDevices READ removedDevices NOTIFY removedDevicesChanged)
    Q_PROPERTY(QStringList testedAddresses READ testedAddresses NOTIFY testedAddressesChanged)
    Q_PROPERTY(QStringList orderedSerials READ orderedSerials NOTIFY orderedSerialsChanged)
    Q_PROPERTY(int discoveryCount READ discoveryCount CONSTANT)

  public:
    using QObject::QObject;

    [[nodiscard]] FixtureDevicesModel* devices() { return &m_devices; }
    [[nodiscard]] QString selectedSerial() const { return m_selectedSerial; }
    void setSelectedSerial(const QString& serial) {
        if (m_selectedSerial == serial) {
            return;
        }
        m_selectedSerial = serial;
        Q_EMIT selectedSerialChanged();
    }
    [[nodiscard]] bool available() const { return m_available; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString lastError() const { return {}; }
    [[nodiscard]] int refreshCount() const { return m_refreshCount; }
    [[nodiscard]] QVariantList sentCommands() const { return m_sentCommands; }
    [[nodiscard]] QVariantList addedDevices() const { return addedDevicesList; }
    [[nodiscard]] QVariantList updatedDevices() const { return updatedDevicesList; }
    [[nodiscard]] QStringList removedDevices() const { return removedDevicesList; }
    [[nodiscard]] QStringList testedAddresses() const { return testedAddressesList; }
    [[nodiscard]] QStringList orderedSerials() const { return orderedSerialsList; }
    [[nodiscard]] int discoveryCount() const { return m_discoveryCount; }

    Q_INVOKABLE void refresh() {
        ++m_refreshCount;
        Q_EMIT refreshCountChanged();
    }

    Q_INVOKABLE void sendCommand(const QString& serial, const QString& action,
                                 const QVariant& value = {}) {
        m_sentCommands.append(QVariantMap{{QStringLiteral("serial"), serial},
                                          {QStringLiteral("action"), action},
                                          {QStringLiteral("value"), value}});
        Q_EMIT sentCommandsChanged();
    }

    Q_INVOKABLE void addDevice(const QVariantMap& device) {
        addedDevicesList.append(device);
        Q_EMIT addedDevicesChanged();
    }

    Q_INVOKABLE void updateDevice(const QString& serial, const QVariantMap& changes) {
        updatedDevicesList.append(QVariantMap{{QStringLiteral("serial"), serial},
                                              {QStringLiteral("changes"), changes}});
        Q_EMIT updatedDevicesChanged();
    }

    Q_INVOKABLE void removeDevice(const QString& serial) {
        removedDevicesList.append(serial);
        Q_EMIT removedDevicesChanged();
    }

    Q_INVOKABLE void setDeviceOrder(const QStringList& serials) {
        orderedSerialsList = serials;
        Q_EMIT orderedSerialsChanged();
    }

    Q_INVOKABLE void testDevice(const QString& address) {
        testedAddressesList.append(address);
        Q_EMIT testedAddressesChanged();
    }

    Q_INVOKABLE void discoverDevices() {
        ++m_discoveryCount;
    }

    Q_INVOKABLE void finishDeviceTest(const QString& address, const QVariantMap& snapshot,
                                      const QString& error) {
        Q_EMIT deviceTestFinished(address, snapshot, error);
    }

    Q_INVOKABLE void failDeviceOperation(const QString& operation, const QString& message) {
        Q_EMIT deviceOperationFailed(operation, message);
    }

    Q_INVOKABLE void succeedDeviceOperation(const QString& operation) {
        Q_EMIT deviceOperationSucceeded(operation);
    }

    Q_INVOKABLE void setFixtureAvailable(bool available) {
        if (m_available == available) {
            return;
        }
        m_available = available;
        Q_EMIT availableChanged();
    }

    Q_INVOKABLE void setFixtureBusy(bool busy) {
        if (m_busy == busy) {
            return;
        }
        m_busy = busy;
        Q_EMIT busyChanged();
    }

    Q_INVOKABLE void setFixtureDevices(const QVariantList& entries) {
        m_devices.replace(entries);
        setSelectedSerial(entries.isEmpty()
                              ? QString()
                              : entries.first().toMap().value(QStringLiteral("serial")).toString());
    }

  Q_SIGNALS:
    void selectedSerialChanged();
    void refreshCountChanged();
    void sentCommandsChanged();
    void availableChanged();
    void busyChanged();
    void addedDevicesChanged();
    void updatedDevicesChanged();
    void removedDevicesChanged();
    void testedAddressesChanged();
    void orderedSerialsChanged();
    void deviceOperationFailed(const QString& operation, const QString& message);
    void deviceOperationSucceeded(const QString& operation);
    void deviceTestFinished(const QString& address, const QVariantMap& snapshot,
                            const QString& error);
    void discoveryFinished(const QString& error);

  private:
    QVariantList addedDevicesList;
    QVariantList updatedDevicesList;
    QStringList removedDevicesList;
    QStringList testedAddressesList;
    QStringList orderedSerialsList;
    FixtureDevicesModel m_devices;
    QString m_selectedSerial;
    int m_refreshCount = 0;
    int m_discoveryCount = 0;
    bool m_available = true;
    bool m_busy = false;
    QVariantList m_sentCommands;
};

class Setup final : public QObject {
    Q_OBJECT

  public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine* engine) {
        qmlRegisterType<BridgeFixture>("RokuDeck.Testing", 1, 0, "BridgeFixture");
        Plasma::setupPlasmaStyle(engine);
        auto* i18nContext = KLocalization::setupLocalizedContext(engine);
        i18nContext->setTranslationDomain(
            QStringLiteral("plasma_applet_io.github.josephur.rokudeck"));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(RokuDeckQml, Setup)

#include "qmltest.moc"

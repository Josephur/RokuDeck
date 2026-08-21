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
                {PendingCommandRole, "pendingCommand"}};
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
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString lastError READ lastError CONSTANT)
    Q_PROPERTY(int refreshCount READ refreshCount NOTIFY refreshCountChanged)
    Q_PROPERTY(QVariantList sentCommands READ sentCommands NOTIFY sentCommandsChanged)

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
    [[nodiscard]] bool available() const { return true; }
    [[nodiscard]] QString lastError() const { return {}; }
    [[nodiscard]] int refreshCount() const { return m_refreshCount; }
    [[nodiscard]] QVariantList sentCommands() const { return m_sentCommands; }

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

  private:
    FixtureDevicesModel m_devices;
    QString m_selectedSerial;
    int m_refreshCount = 0;
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

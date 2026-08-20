// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QStringList>

#include <optional>

namespace RokuDeck {

struct DeviceDescription {
    QString serial;
    QString name;
    QString address;
};

struct DeviceStateSnapshot {
    QString serial;
    std::optional<QString> name;
    std::optional<QString> address;
    std::optional<QString> health;
    std::optional<QString> power;
    std::optional<QString> screensaver;
    std::optional<QString> activeLabel;
    std::optional<std::optional<int>> volume;
    std::optional<std::optional<bool>> muted;
    std::optional<QStringList> capabilities;
    bool stale = true;
};

class DevicesModel final : public QAbstractListModel {
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
    Q_ENUM(Role)

    explicit DevicesModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replaceDevices(const QList<DeviceDescription>& devices);
    void applySnapshot(const DeviceStateSnapshot& snapshot);
    void markStale(const QString& serial);
    void markAllStale();
    void markCommandPending(const QString& serial, const QString& requestId);
    void finishPendingCommand(const QString& serial, const QString& requestId);

    [[nodiscard]] QStringList serials() const;
    [[nodiscard]] bool containsSerial(const QString& serial) const;

  private:
    struct DeviceItem {
        QString serial;
        QString name;
        QString address;
        QString health = QStringLiteral("unknown");
        QString power = QStringLiteral("unknown");
        QString screensaver = QStringLiteral("unknown");
        QString activeLabel;
        std::optional<int> volume;
        std::optional<bool> muted;
        QStringList capabilities;
        bool stale = true;
        QSet<QString> pendingRequests;
    };

    [[nodiscard]] qsizetype indexOfSerial(const QString& serial) const;

    QList<DeviceItem> m_devices;
};

} // namespace RokuDeck

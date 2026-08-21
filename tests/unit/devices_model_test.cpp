// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmoid/devicesmodel.h"

#include <QSignalSpy>
#include <QTest>

using RokuDeck::DeviceDescription;
using RokuDeck::DevicesModel;
using RokuDeck::DeviceStateSnapshot;

namespace {

DeviceDescription device(const char* serial, const char* name) {
    return {QString::fromLatin1(serial), QString::fromLatin1(name), QStringLiteral("192.0.2.10")};
}

QList<DeviceDescription> deviceArray(std::initializer_list<DeviceDescription> devices) {
    return QList<DeviceDescription>(devices);
}

DeviceStateSnapshot snapshot(const char* serial, int volume, bool muted) {
    DeviceStateSnapshot value;
    value.serial = QString::fromLatin1(serial);
    value.health = QStringLiteral("online");
    value.power = QStringLiteral("on");
    value.screensaver = QStringLiteral("inactive");
    value.volume = std::optional<int>{volume};
    value.muted = std::optional<bool>{muted};
    value.stale = false;
    return value;
}

QList<int> changedRoles(const QSignalSpy& spy) {
    return qvariant_cast<QList<int>>(spy.constLast().at(2));
}

} // namespace

class DevicesModelTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void updatesOneDeviceWithoutResettingTheModel();
    void exposesTheCompleteStableRoleContract();
    void emitsOnlyRolesWhoseValuesChanged();
    void reconcilesDevicesBySerialWithoutLosingState();
    void pendingStateRemainsUntilEveryRequestFinishes();
    void reportsIncrementalDeviceInsertionAndRemoval();
};

void DevicesModelTest::updatesOneDeviceWithoutResettingTheModel() {
    DevicesModel model;
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.replaceDevices(deviceArray({device("SERIAL-A", "Living Room")}));
    model.applySnapshot(snapshot("SERIAL-A", 45, false));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), DevicesModel::VolumeRole).toInt(), 45);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(reset.count(), 0);
}

void DevicesModelTest::exposesTheCompleteStableRoleContract() {
    DevicesModel model;

    const QHash<int, QByteArray> expected{
        {Qt::UserRole + 1, "serial"},      {Qt::UserRole + 2, "name"},
        {Qt::UserRole + 3, "address"},     {Qt::UserRole + 4, "health"},
        {Qt::UserRole + 5, "power"},       {Qt::UserRole + 6, "screensaver"},
        {Qt::UserRole + 7, "activeLabel"}, {Qt::UserRole + 8, "volume"},
        {Qt::UserRole + 9, "muted"},       {Qt::UserRole + 10, "capabilities"},
        {Qt::UserRole + 11, "stale"},      {Qt::UserRole + 12, "pendingCommand"},
        {Qt::UserRole + 13, "allowAutomations"},
        {Qt::UserRole + 14, "wakeOnWlan"},
        {Qt::UserRole + 15, "refreshPolicy"},
    };

    QCOMPARE(model.roleNames(), expected);
}

void DevicesModelTest::emitsOnlyRolesWhoseValuesChanged() {
    DevicesModel model;
    model.replaceDevices(deviceArray({device("SERIAL-A", "Living Room")}));
    const DeviceStateSnapshot initial = snapshot("SERIAL-A", 45, false);
    model.applySnapshot(initial);

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.applySnapshot(initial);
    QCOMPARE(changed.count(), 0);

    DeviceStateSnapshot updated = initial;
    updated.volume = std::optional<int>{46};
    model.applySnapshot(updated);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changedRoles(changed), QList<int>{DevicesModel::VolumeRole});
}

void DevicesModelTest::reconcilesDevicesBySerialWithoutLosingState() {
    DevicesModel model;
    const DeviceDescription first = device("SERIAL-A", "Living Room");
    DeviceDescription second = device("SERIAL-B", "Office");
    second.address = QStringLiteral("198.51.100.20");
    model.replaceDevices(deviceArray({first, second}));
    model.applySnapshot(snapshot("SERIAL-A", 45, false));

    DeviceDescription renamed = first;
    renamed.name = QStringLiteral("Family Room");
    renamed.address = QStringLiteral("203.0.113.30");
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.replaceDevices(deviceArray({second, renamed}));

    QCOMPARE(reset.count(), 0);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1), DevicesModel::SerialRole).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(model.data(model.index(1), DevicesModel::NameRole).toString(),
             QStringLiteral("Family Room"));
    QCOMPARE(model.data(model.index(1), DevicesModel::AddressRole).toString(),
             QStringLiteral("203.0.113.30"));
    QCOMPARE(model.data(model.index(1), DevicesModel::VolumeRole).toInt(), 45);
}

void DevicesModelTest::pendingStateRemainsUntilEveryRequestFinishes() {
    DevicesModel model;
    model.replaceDevices(deviceArray({device("SERIAL-A", "Living Room")}));
    const QModelIndex index = model.index(0);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.markCommandPending(QStringLiteral("SERIAL-A"), QStringLiteral("REQUEST-ONE"));
    model.markCommandPending(QStringLiteral("SERIAL-A"), QStringLiteral("REQUEST-TWO"));
    QCOMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(changed.count(), 1);

    model.finishPendingCommand(QStringLiteral("SERIAL-A"), QStringLiteral("REQUEST-ONE"));
    QCOMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(changed.count(), 1);

    model.finishPendingCommand(QStringLiteral("SERIAL-A"), QStringLiteral("REQUEST-TWO"));
    QCOMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changedRoles(changed), QList<int>{DevicesModel::PendingCommandRole});
}

void DevicesModelTest::reportsIncrementalDeviceInsertionAndRemoval() {
    DevicesModel model;
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

    model.replaceDevices(
        deviceArray({device("SERIAL-A", "Living Room"), device("SERIAL-B", "Office")}));
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(reset.count(), 0);

    model.replaceDevices(deviceArray({device("SERIAL-B", "Office")}));
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.constLast().at(1).toInt(), 1);
    QCOMPARE(removed.constLast().at(2).toInt(), 1);
    QCOMPARE(reset.count(), 0);
}

QTEST_GUILESS_MAIN(DevicesModelTest)

#include "devices_model_test.moc"

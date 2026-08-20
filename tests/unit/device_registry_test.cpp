// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/device_registry.h"

#include <QtTest>

using namespace RokuDeck;

class DeviceRegistryTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void rediscoveryUpdatesAddressBySerial();
    void distinctSerialsAtOneAddressRemainDistinct();
};

void DeviceRegistryTest::rediscoveryUpdatesAddressBySerial()
{
    DeviceRegistry registry;
    registry.upsert({QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"), QStringLiteral("192.0.2.8")});
    registry.upsert({QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"), QStringLiteral("192.0.2.9")});

    QCOMPARE(registry.devices().size(), 1);
    QCOMPARE(registry.devices().front().address, QStringLiteral("192.0.2.9"));
}

void DeviceRegistryTest::distinctSerialsAtOneAddressRemainDistinct()
{
    DeviceRegistry registry;
    registry.upsert({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture One"), QStringLiteral("198.51.100.7")});
    registry.upsert({QStringLiteral("SERIAL-B"), QStringLiteral("Fixture Two"), QStringLiteral("198.51.100.7")});

    QCOMPARE(registry.devices().size(), 2);
}

QTEST_GUILESS_MAIN(DeviceRegistryTest)
#include "device_registry_test.moc"

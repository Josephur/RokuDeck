// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmoid/daemonproxy.h"
#include "plasmoid/devicesmodel.h"
#include "plasmoid/rokudeckapplet.h"

#include <Plasma/Applet>

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <KPluginMetaData>

#include <memory>
#include <type_traits>

using RokuDeck::DaemonProxy;
using RokuDeck::DevicesModel;
using RokuDeck::RokuDeckApplet;

namespace {

constexpr auto serviceName = "io.github.josephur.RokuDeck1";
constexpr auto objectPath = "/io/github/josephur/RokuDeck1";

QString devicesJson(bool includeSecond = true) {
    const QString second =
        includeSecond
            ? QStringLiteral(
                  R"json(,{"serial":"SERIAL-B","displayName":"Office","address":"198.51.100.20"})json")
            : QString();
    return QStringLiteral(
               R"json({"schemaVersion":1,"devices":[{"serial":"SERIAL-A","displayName":"Living Room","address":"192.0.2.10"}%1]})json")
        .arg(second);
}

QString fullSnapshot(const QString& serial) {
    const QString name = serial == QStringLiteral("SERIAL-A") ? QStringLiteral("Living Room")
                                                              : QStringLiteral("Office");
    const QString address = serial == QStringLiteral("SERIAL-A") ? QStringLiteral("192.0.2.10")
                                                                 : QStringLiteral("198.51.100.20");
    const QJsonObject object{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("serial"), serial},
        {QStringLiteral("displayName"), name},
        {QStringLiteral("address"), address},
        {QStringLiteral("health"), QStringLiteral("online")},
        {QStringLiteral("power"), QStringLiteral("on")},
        {QStringLiteral("screensaver"), QStringLiteral("inactive")},
        {QStringLiteral("activeApp"),
         QJsonObject{{QStringLiteral("id"), QStringLiteral("fixture.app")},
                     {QStringLiteral("name"), QStringLiteral("Fixture Channel")},
                     {QStringLiteral("type"), QStringLiteral("appl")},
                     {QStringLiteral("input"), QString()}}},
        {QStringLiteral("volume"), 45},
        {QStringLiteral("muted"), false},
        {QStringLiteral("capabilities"),
         QJsonArray{QStringLiteral("power"), QStringLiteral("volume")}},
        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-20T16:00:00.000-04:00")},
    };
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

template <typename Applet> std::unique_ptr<Applet> createApplet() {
    if constexpr (std::is_base_of_v<Plasma::Applet, Applet>) {
        return std::make_unique<Applet>(nullptr, KPluginMetaData(), QVariantList());
    } else {
        return std::make_unique<Applet>(nullptr);
    }
}

} // namespace

class FakeDaemon final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.josephur.RokuDeck1")

  public:
    explicit FakeDaemon(QObject* parent = nullptr)
        : QObject(parent), m_connectionName(QStringLiteral("rokudeck-plasmoid-test-") +
                                            QUuid::createUuid().toString(QUuid::WithoutBraces)),
          m_connection(
              QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName)) {}

    ~FakeDaemon() override {
        QDBusConnection::disconnectFromBus(m_connectionName);
    }

    QStringList registrationAttempts;
    QStringList unregistrationAttempts;
    QSet<QString> activeClients;
    bool delayNextRegistration = false;

    bool registerOnBus() {
        const bool objectRegistered = m_connection.registerObject(
            QString::fromLatin1(objectPath), this,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        const bool serviceRegistered =
            m_connection.registerService(QString::fromLatin1(serviceName));
        return objectRegistered && serviceRegistered;
    }

    bool unregisterService() {
        return m_connection.unregisterService(QString::fromLatin1(serviceName));
    }

    bool registerService() {
        return m_connection.registerService(QString::fromLatin1(serviceName));
    }

    void unregisterFromBus() {
        static_cast<void>(unregisterService());
        m_connection.unregisterObject(QString::fromLatin1(objectPath));
    }

    bool completeDelayedRegistration() {
        if (!m_hasDelayedRegistration) {
            return false;
        }
        m_hasDelayedRegistration = false;
        return m_connection.send(m_delayedRegistration.createReply());
    }

  public Q_SLOTS:
    QString ListDevices() const {
        return devicesJson();
    }

    QString GetSnapshot(const QString& serial) const {
        return fullSnapshot(serial);
    }

    void RegisterClient(const QString& clientId, const QStringList& serials, uint intervalMs) {
        Q_UNUSED(serials)
        Q_UNUSED(intervalMs)
        registrationAttempts.append(clientId);
        if (delayNextRegistration) {
            delayNextRegistration = false;
            m_hasDelayedRegistration = true;
            m_delayedRegistration = message();
            setDelayedReply(true);
            return;
        }
        activeClients.insert(clientId);
    }

    void UpdateClientInterest(const QString& clientId, const QStringList& serials,
                              uint intervalMs) {
        Q_UNUSED(serials)
        Q_UNUSED(intervalMs)
        activeClients.insert(clientId);
    }

    void UnregisterClient(const QString& clientId) {
        unregistrationAttempts.append(clientId);
        activeClients.remove(clientId);
    }

    QString DiscoverDevices() const {
        return devicesJson();
    }

    QString SendCommand(const QString& serial, const QString& commandJson) {
        Q_UNUSED(serial)
        Q_UNUSED(commandJson)
        return QStringLiteral("REQUEST-ONE");
    }

  Q_SIGNALS:
    void DeviceSnapshotChanged(const QString& serial, const QString& snapshotJson);
    void DeviceListChanged(const QString& devicesJson);
    void CommandFinished(const QString& requestId, const QString& resultJson);

  private:
    const QString m_connectionName;
    QDBusConnection m_connection;
    QDBusMessage m_delayedRegistration;
    bool m_hasDelayedRegistration = false;
};

class PlasmoidBridgeTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();
    void bridgeIsTheConcreteAppletType();
    void appletInstancesOwnIndependentRegistrationAndSelection();
    void appletDestroyedLifecycleUnregistersAndReregisters();
    void destroyingOneAppletLeavesTheOtherRegistered();
    void partialSnapshotPreservesMissingFieldsAndExplicitlyClearsNullFields();
    void malformedPayloadMarksExistingStateStale();
    void malformedSnapshotsAreRejectedAtomically_data();
    void malformedSnapshotsAreRejectedAtomically();
    void successfulCommandRemainsPendingUntilFreshSnapshot();
    void staleRegistrationReplyCannotUnregisterReplacementOwner();

  private:
    [[nodiscard]] QModelIndex firstIndex(const DevicesModel& model) const;

    std::unique_ptr<FakeDaemon> m_fake;
};

void PlasmoidBridgeTest::init() {
    m_fake = std::make_unique<FakeDaemon>();
    QVERIFY2(m_fake->registerOnBus(), "failed to register the fake RokuDeck D-Bus service");
}

void PlasmoidBridgeTest::cleanup() {
    m_fake->unregisterFromBus();
    m_fake.reset();
}

void PlasmoidBridgeTest::bridgeIsTheConcreteAppletType() {
    QVERIFY2((std::is_base_of_v<Plasma::Applet, RokuDeckApplet>),
             "RokuDeckBridge state must be owned by each concrete Plasma::Applet instance");
}

void PlasmoidBridgeTest::appletInstancesOwnIndependentRegistrationAndSelection() {
    auto first = createApplet<RokuDeckApplet>();
    auto second = createApplet<RokuDeckApplet>();
    QTRY_COMPARE(m_fake->activeClients.size(), 2);
    QCOMPARE(
        QSet<QString>(m_fake->registrationAttempts.cbegin(), m_fake->registrationAttempts.cend())
            .size(),
        2);
    QTRY_COMPARE(first->devices()->rowCount(), 2);
    QTRY_COMPARE(second->devices()->rowCount(), 2);

    first->setSelectedSerial(QStringLiteral("SERIAL-B"));
    QCOMPARE(first->selectedSerial(), QStringLiteral("SERIAL-B"));
    QCOMPARE(second->selectedSerial(), QStringLiteral("SERIAL-A"));

    Q_EMIT m_fake->DeviceListChanged(devicesJson(false));
    QTRY_COMPARE(first->selectedSerial(), QStringLiteral("SERIAL-A"));
    QCOMPARE(second->selectedSerial(), QStringLiteral("SERIAL-A"));
}

void PlasmoidBridgeTest::appletDestroyedLifecycleUnregistersAndReregisters() {
    auto applet = createApplet<RokuDeckApplet>();
    QTRY_COMPARE(m_fake->activeClients.size(), 1);
    const QString clientId = *m_fake->activeClients.cbegin();

    QVERIFY(QMetaObject::invokeMethod(applet.get(), "destroyedChanged", Qt::DirectConnection,
                                      Q_ARG(bool, true)));
    QTRY_VERIFY(!m_fake->activeClients.contains(clientId));

    QVERIFY(QMetaObject::invokeMethod(applet.get(), "destroyedChanged", Qt::DirectConnection,
                                      Q_ARG(bool, false)));
    QTRY_VERIFY(m_fake->activeClients.contains(clientId));
}

void PlasmoidBridgeTest::destroyingOneAppletLeavesTheOtherRegistered() {
    auto first = createApplet<RokuDeckApplet>();
    auto second = createApplet<RokuDeckApplet>();
    QTRY_COMPARE(m_fake->activeClients.size(), 2);
    const QSet<QString> before = m_fake->activeClients;

    first.reset();
    QTRY_COMPARE(m_fake->activeClients.size(), 1);
    QVERIFY(before.contains(*m_fake->activeClients.cbegin()));
    QVERIFY(!second->selectedSerial().isEmpty());
}

void PlasmoidBridgeTest::partialSnapshotPreservesMissingFieldsAndExplicitlyClearsNullFields() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-PARTIAL"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    QTRY_COMPARE(model.data(index, DevicesModel::StaleRole).toBool(), false);

    const QString partial = QStringLiteral(
        R"json({"schemaVersion":1,"serial":"SERIAL-A","health":"degraded","capturedAt":"2026-08-20T16:01:00.000-04:00"})json");
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"), partial);
    QTRY_COMPARE(model.data(index, DevicesModel::HealthRole).toString(),
                 QStringLiteral("degraded"));
    QCOMPARE(model.data(index, DevicesModel::NameRole).toString(), QStringLiteral("Living Room"));
    QCOMPARE(model.data(index, DevicesModel::AddressRole).toString(), QStringLiteral("192.0.2.10"));
    QCOMPARE(model.data(index, DevicesModel::CapabilitiesRole).toStringList(),
             QStringList({QStringLiteral("power"), QStringLiteral("volume")}));

    const QString explicitClear = QStringLiteral(
        R"json({"schemaVersion":1,"serial":"SERIAL-A","activeApp":null,"volume":null,"muted":null,"capabilities":[],"capturedAt":"2026-08-20T16:02:00.000-04:00"})json");
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"), explicitClear);
    QTRY_VERIFY(!model.data(index, DevicesModel::VolumeRole).isValid());
    QVERIFY(!model.data(index, DevicesModel::MutedRole).isValid());
    QCOMPARE(model.data(index, DevicesModel::ActiveLabelRole).toString(), QString());
    QCOMPARE(model.data(index, DevicesModel::CapabilitiesRole).toStringList(), QStringList());
}

void PlasmoidBridgeTest::malformedPayloadMarksExistingStateStale() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-MALFORMED"));
    QSignalSpy errors(&proxy, &DaemonProxy::errorOccurred);
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    QTRY_COMPARE(model.data(index, DevicesModel::StaleRole).toBool(), false);

    const QString malformed = QStringLiteral(
        R"json({"schemaVersion":1,"serial":"SERIAL-A","capabilities":"power","capturedAt":"2026-08-20T16:03:00.000-04:00"})json");
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"), malformed);
    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(model.data(index, DevicesModel::StaleRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::CapabilitiesRole).toStringList(),
             QStringList({QStringLiteral("power"), QStringLiteral("volume")}));

    Q_EMIT m_fake->DeviceListChanged(
        QStringLiteral(R"json({"schemaVersion":1,"devices":[{"serial":"SERIAL-A"}]})json"));
    QTRY_COMPARE(errors.count(), 2);
    for (int row = 0; row < model.rowCount(); ++row) {
        QCOMPARE(model.data(model.index(row), DevicesModel::StaleRole).toBool(), true);
    }
}

void PlasmoidBridgeTest::malformedSnapshotsAreRejectedAtomically_data() {
    QTest::addColumn<QString>("snapshotJson");

    const QJsonObject poisonedSnapshot{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("serial"), QStringLiteral("SERIAL-A")},
        {QStringLiteral("displayName"), QStringLiteral("Poisoned Name")},
        {QStringLiteral("address"), QStringLiteral("203.0.113.99")},
        {QStringLiteral("health"), QStringLiteral("offline")},
        {QStringLiteral("power"), QStringLiteral("off")},
        {QStringLiteral("screensaver"), QStringLiteral("active")},
        {QStringLiteral("activeApp"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("Poisoned App")}}},
        {QStringLiteral("volume"), 99},
        {QStringLiteral("muted"), true},
        {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("poisoned")}},
        {QStringLiteral("capturedAt"), QStringLiteral("2026-08-20T16:04:00.000-04:00")},
    };
    const auto withMalformedValue = [&poisonedSnapshot](const QString& key,
                                                        const QJsonValue& value) {
        QJsonObject malformed = poisonedSnapshot;
        malformed.insert(key, value);
        return QString::fromUtf8(QJsonDocument(malformed).toJson(QJsonDocument::Compact));
    };

    QTest::newRow("null-serial") << withMalformedValue(QStringLiteral("serial"), QJsonValue::Null);
    QTest::newRow("empty-serial") << withMalformedValue(QStringLiteral("serial"), QString());
    QTest::newRow("null-display-name")
        << withMalformedValue(QStringLiteral("displayName"), QJsonValue::Null);
    QTest::newRow("null-address") << withMalformedValue(QStringLiteral("address"),
                                                        QJsonValue::Null);
    QTest::newRow("empty-address") << withMalformedValue(QStringLiteral("address"), QString());
    QTest::newRow("null-health") << withMalformedValue(QStringLiteral("health"), QJsonValue::Null);
    QTest::newRow("empty-health") << withMalformedValue(QStringLiteral("health"), QString());
    QTest::newRow("null-power") << withMalformedValue(QStringLiteral("power"), QJsonValue::Null);
    QTest::newRow("empty-power") << withMalformedValue(QStringLiteral("power"), QString());
    QTest::newRow("null-screensaver")
        << withMalformedValue(QStringLiteral("screensaver"), QJsonValue::Null);
    QTest::newRow("empty-screensaver")
        << withMalformedValue(QStringLiteral("screensaver"), QString());
    QTest::newRow("invalid-captured-at")
        << withMalformedValue(QStringLiteral("capturedAt"), QStringLiteral("not-a-timestamp"));
    QTest::newRow("empty-captured-at")
        << withMalformedValue(QStringLiteral("capturedAt"), QString());
    QTest::newRow("null-captured-at")
        << withMalformedValue(QStringLiteral("capturedAt"), QJsonValue::Null);
    QTest::newRow("wrong-schema-version") << withMalformedValue(QStringLiteral("schemaVersion"), 2);
    QTest::newRow("wrong-schema-type")
        << withMalformedValue(QStringLiteral("schemaVersion"), QStringLiteral("1"));
}

void PlasmoidBridgeTest::malformedSnapshotsAreRejectedAtomically() {
    QFETCH(QString, snapshotJson);

    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-ATOMIC"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    QTRY_COMPARE(model.data(index, DevicesModel::StaleRole).toBool(), false);

    QSignalSpy errors(&proxy, &DaemonProxy::errorOccurred);
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"), snapshotJson);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 1000);

    QCOMPARE(model.data(index, DevicesModel::NameRole).toString(), QStringLiteral("Living Room"));
    QCOMPARE(model.data(index, DevicesModel::AddressRole).toString(), QStringLiteral("192.0.2.10"));
    QCOMPARE(model.data(index, DevicesModel::HealthRole).toString(), QStringLiteral("online"));
    QCOMPARE(model.data(index, DevicesModel::PowerRole).toString(), QStringLiteral("on"));
    QCOMPARE(model.data(index, DevicesModel::ScreensaverRole).toString(),
             QStringLiteral("inactive"));
    QCOMPARE(model.data(index, DevicesModel::ActiveLabelRole).toString(),
             QStringLiteral("Fixture Channel"));
    QCOMPARE(model.data(index, DevicesModel::VolumeRole).toInt(), 45);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);
    QCOMPARE(model.data(index, DevicesModel::CapabilitiesRole).toStringList(),
             QStringList({QStringLiteral("power"), QStringLiteral("volume")}));
    QCOMPARE(model.data(index, DevicesModel::StaleRole).toBool(), true);
}

void PlasmoidBridgeTest::successfulCommandRemainsPendingUntilFreshSnapshot() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-COMMAND"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);

    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("Home"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    Q_EMIT m_fake->CommandFinished(
        QStringLiteral("REQUEST-ONE"),
        QStringLiteral(
            R"json({"schemaVersion":1,"requestId":"REQUEST-ONE","serial":"SERIAL-A","success":true})json"));
    QTest::qWait(25);
    QCOMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);

    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         fullSnapshot(QStringLiteral("SERIAL-A")));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);
}

void PlasmoidBridgeTest::staleRegistrationReplyCannotUnregisterReplacementOwner() {
    m_fake->delayNextRegistration = true;
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-RESTART"));
    proxy.start();
    QTRY_COMPARE(m_fake->registrationAttempts.size(), 1);
    QCOMPARE(m_fake->activeClients.size(), 0);

    QVERIFY(m_fake->unregisterService());
    QTRY_COMPARE(proxy.isAvailable(), false);
    QVERIFY(m_fake->registerService());
    QTRY_COMPARE(m_fake->registrationAttempts.size(), 2);
    QTRY_VERIFY(m_fake->activeClients.contains(QStringLiteral("PROXY-RESTART")));

    QVERIFY(m_fake->completeDelayedRegistration());
    QTest::qWait(100);
    QCOMPARE(m_fake->unregistrationAttempts.size(), 0);
    QVERIFY(m_fake->activeClients.contains(QStringLiteral("PROXY-RESTART")));
}

QModelIndex PlasmoidBridgeTest::firstIndex(const DevicesModel& model) const {
    return model.index(0);
}

QTEST_MAIN(PlasmoidBridgeTest)

#include "plasmoid_bridge_test.moc"

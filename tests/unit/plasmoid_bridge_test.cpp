// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmoid/automationsmodel.h"
#include "plasmoid/daemonproxy.h"
#include "plasmoid/devicesmodel.h"
#include "plasmoid/rokudeckapplet.h"

#include <Plasma/Applet>

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
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

using RokuDeck::AutomationsModel;
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
                  R"json(,{"serial":"SERIAL-B","displayName":"Office","address":"198.51.100.20","allowAutomations":true,"wakeOnWlan":false,"refreshPolicy":"balanced"})json")
            : QString();
    return QStringLiteral(
               R"json({"schemaVersion":1,"devices":[{"serial":"SERIAL-A","displayName":"Living Room","address":"192.0.2.10","allowAutomations":false,"wakeOnWlan":true,"refreshPolicy":"battery","volumeCeiling":50}%1]})json")
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

QString snapshotWith(const QString& serial, bool muted, const QString& capturedAt) {
    QJsonObject object = QJsonDocument::fromJson(fullSnapshot(serial).toUtf8()).object();
    object.insert(QStringLiteral("muted"), muted);
    object.insert(QStringLiteral("capturedAt"), capturedAt);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// Captures "now"+offset in the same ISO format the daemon emits, so tests can
// place a reading relative to an optimistic prediction made moments ago.
QString capturedAroundNow(qint64 offsetMs) {
    return QDateTime::currentDateTime().addMSecs(offsetMs).toString(Qt::ISODateWithMs);
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
    mutable QStringList snapshotRequests;
    QSet<QString> activeClients;
    QStringList addedDevices;
    QStringList updatedDevices;
    QStringList removedDevices;
    QStringList testedAddresses;
    QStringList orderedSerials;
    QString rulesJson = QStringLiteral(R"json({"schemaVersion":1,"rules":[]})json");
    QStringList ruleEnabledCalls;
    QStringList ruleDeletedCalls;
    QStringList ruleRunCalls;
    QStringList addedRules;
    QStringList updatedRules;
    QList<QJsonObject> storedRules;
    QString nextRunOutcome = QStringLiteral("ok");
    bool delayNextRegistration = false;
    bool delayNextDeviceCall = false;
    bool rejectNextCall = false;
    bool rejectNextRuleCall = false;
    // Tests driving two overlapping commands (rapid mute taps) need distinct
    // request ids; the default keeps the historical REQUEST-ONE contract.
    bool useNumberedRequestIds = false;
    int requestCounter = 0;

    // Minimal stand-in for the daemon's own contract behavior.
    void storeRule(QJsonObject object, const QString& id) {
        object.insert(QStringLiteral("id"), id);
        if (!object.contains(QStringLiteral("enabled"))) {
            object.insert(QStringLiteral("enabled"), true);
        }
        object.insert(QStringLiteral("schemaVersion"), 1);
        storedRules.append(std::move(object));
    }

    QString ruleId(int number) const {
        return QStringLiteral("rFIXTURE%1").arg(number, 5, 10, QChar(u'0'));
    }

    void announceRules() {
        QJsonArray array;
        for (const QJsonObject& rule : storedRules) {
            array.append(rule);
        }
        rulesJson = QString::fromUtf8(QJsonDocument(
            QJsonObject{{QStringLiteral("schemaVersion"), 1},
                        {QStringLiteral("rules"), array}})
                                          .toJson(QJsonDocument::Compact));
        Q_EMIT RulesChanged(rulesJson);
    }

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

    QString ListRules() const {
        return rulesJson;
    }

    void SetRuleEnabled(const QString& ruleId, bool enabled) {
        ruleEnabledCalls.append(ruleId + (enabled ? QStringLiteral("|on")
                                                  : QStringLiteral("|off")));
        if (rejectNextRuleCall) {
            rejectNextRuleCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fixture rejection"));
        }
    }

    void DeleteRule(const QString& ruleId) {
        ruleDeletedCalls.append(ruleId);
        if (rejectNextRuleCall) {
            rejectNextRuleCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fixture rejection"));
        }
    }

    QString RunRule(const QString& ruleId) {
        ruleRunCalls.append(ruleId);
        if (rejectNextRuleCall) {
            rejectNextRuleCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fixture rejection"));
            return {};
        }
        return nextRunOutcome;
    }

    QString AddRule(const QString& ruleJson) {
        addedRules.append(ruleJson);
        if (rejectNextRuleCall) {
            rejectNextRuleCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalidRule"));
            return {};
        }
        const QJsonDocument document = QJsonDocument::fromJson(ruleJson.toUtf8());
        storeRule(document.object(), ruleId(storedRules.size() + 1));
        announceRules();
        return QString::fromUtf8(
            QJsonDocument(storedRules.last()).toJson(QJsonDocument::Compact));
    }

    void UpdateRule(const QString& ruleJson) {
        updatedRules.append(ruleJson);
        if (rejectNextRuleCall) {
            rejectNextRuleCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknownRule"));
            return;
        }
        const QJsonObject object =
            QJsonDocument::fromJson(ruleJson.toUtf8()).object();
        const QString id = object.value(QStringLiteral("id")).toString();
        for (int i = 0; i < storedRules.size(); ++i) {
            if (storedRules.at(i).value(QStringLiteral("id")).toString() == id) {
                storedRules[i] = object;
                break;
            }
        }
        announceRules();
    }

    QString GetSnapshot(const QString& serial) const {
        snapshotRequests.append(serial);
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
        if (useNumberedRequestIds) {
            return QStringLiteral("REQUEST-%1").arg(++requestCounter);
        }
        return QStringLiteral("REQUEST-ONE");
    }

    QString AddDevice(const QString& deviceJson) {
        addedDevices.append(deviceJson);
        if (rejectNextCall) {
            rejectNextCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fixture rejection"));
            return {};
        }
        return deviceJson;
    }

    QString UpdateDevice(const QString& serial, const QString& changesJson) {
        updatedDevices.append(serial + QChar(u'|') + changesJson);
        if (rejectNextCall) {
            rejectNextCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fixture rejection"));
            return {};
        }
        return changesJson;
    }

    void RemoveDevice(const QString& serial) {
        removedDevices.append(serial);
        if (delayNextDeviceCall) {
            delayNextDeviceCall = false;
            m_hasDelayedDeviceCall = true;
            m_delayedDeviceCall = message();
            setDelayedReply(true);
        }
    }

    bool completeDelayedDeviceCall() {
        if (!m_hasDelayedDeviceCall) {
            return false;
        }
        m_hasDelayedDeviceCall = false;
        return m_connection.send(m_delayedDeviceCall.createReply());
    }

    void SetDeviceOrder(const QStringList& serials) {
        orderedSerials = serials;
    }

    QString TestDevice(const QString& address) {
        testedAddresses.append(address);
        if (rejectNextCall) {
            rejectNextCall = false;
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("fixture rejection"));
            return {};
        }
        return fullSnapshot(QStringLiteral("SERIAL-A"));
    }

  Q_SIGNALS:
    void DeviceSnapshotChanged(const QString& serial, const QString& snapshotJson);
    void DeviceListChanged(const QString& devicesJson);
    void CommandFinished(const QString& requestId, const QString& resultJson);
    void RulesChanged(const QString& rulesJson);

  private:
    const QString m_connectionName;
    QDBusConnection m_connection;
    QDBusMessage m_delayedRegistration;
    QDBusMessage m_delayedDeviceCall;
    bool m_hasDelayedRegistration = false;
    bool m_hasDelayedDeviceCall = false;
};

class PlasmoidBridgeTest : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();
    void bridgeIsTheConcreteAppletType();
    void appletInstancesOwnIndependentRegistrationAndSelection();
    void appletDestroyedLifecycleUnregistersAndReregisters();
    void appletExposesRuleChoicesAndForwardsRunOutcomes();
    void destroyingOneAppletLeavesTheOtherRegistered();
    void partialSnapshotPreservesMissingFieldsAndExplicitlyClearsNullFields();
    void malformedPayloadMarksExistingStateStale();
    void malformedSnapshotsAreRejectedAtomically_data();
    void malformedSnapshotsAreRejectedAtomically();
    void successfulCommandReenablesInputWithoutWaitingForAPoll();
    void successfulCommandTriggersAFreshSnapshot();
    void aLateStaleSnapshotCannotUndoNewerState();
    void muteKeypressFlipsTheIconOptimistically();
    void failedMuteCommandRevertsTheIconImmediately();
    void anInFlightCaptureCannotRegressAnArmedMuteIcon();
    void aFreshCaptureOutranksAPinnedMuteExpectation();
    void aSupersededMuteFailureKeepsTheNewerPrediction();
    void staleRegistrationReplyCannotUnregisterReplacementOwner();
    void deviceListExposesDaemonOwnedPreferences();
    void aMalformedVolumeCeilingKeepsTheStrictListRejection();
    void deviceManagementReachesTheDaemonAndReportsRejection();
    void testDeviceReportsTheProbedSnapshot();
    void anInFlightOperationIsReportedThroughPlasmasBusyProperty();
    void automationsModelLoadsAndTracksRulesChanged();
    void unknownFutureRuleTypesDegradeToCustomRule();
    void malformedRuleListReportsAnErrorAndKeepsTheLastGoodList();
    void ruleMutationsReachTheDaemonAndReportRejection();
    void editorFlowAddsARuleThroughTheDaemonAndSurfacesItsSummary();
    void anEditedRenamePersistsAcrossProxyRecreation();
    void aRejectedAddRuleExplainsItselfWithoutAdding();

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

void PlasmoidBridgeTest::successfulCommandReenablesInputWithoutWaitingForAPoll() {
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
    // Holding the buttons disabled until the next poll would make repeated
    // keypresses (d-pad navigation) wait seconds between each press; a fresh
    // snapshot refreshes labels on its own schedule regardless.
    QTest::qWait(25);
    QCOMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);

    // A snapshot still applies normally afterwards.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         fullSnapshot(QStringLiteral("SERIAL-A")));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);
}

void PlasmoidBridgeTest::successfulCommandTriggersAFreshSnapshot() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-COMMAND"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    // State changes caused by a command (e.g. standby after power off) must
    // reach the widget quickly, so a finished command pulls a snapshot right
    // away instead of waiting for the next periodic poll.
    const int before = m_fake->snapshotRequests.count(QStringLiteral("SERIAL-A"));

    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("Home"));
    const QModelIndex index = firstIndex(model);
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    Q_EMIT m_fake->CommandFinished(
        QStringLiteral("REQUEST-ONE"),
        QStringLiteral(
            R"json({"schemaVersion":1,"requestId":"REQUEST-ONE","serial":"SERIAL-A","success":true})json"));

    QTRY_COMPARE(m_fake->snapshotRequests.count(QStringLiteral("SERIAL-A")), before + 1);
}

void PlasmoidBridgeTest::aLateStaleSnapshotCannotUndoNewerState() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-ORDER"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);

    // Baseline reading.
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // The user tapped mute and the newer capture says muted.
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), true,
                     QStringLiteral("2026-08-20T16:05:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // A post-command pull started earlier can finish after the push above;
    // applying it would flip the mute icon back while the television really
    // is muted. Late stale readings must be dropped.
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTest::qWait(50);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // Genuinely newer readings keep flowing through.
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:06:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);
}

void PlasmoidBridgeTest::muteKeypressFlipsTheIconOptimistically() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-MUTE"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);

    // Baseline reading: not muted.
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // A Home keypress predicts nothing: only the mute toggle is predictable.
    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("Home"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);
    Q_EMIT m_fake->CommandFinished(
        QStringLiteral("REQUEST-ONE"),
        QStringLiteral(
            R"json({"schemaVersion":1,"requestId":"REQUEST-ONE","serial":"SERIAL-A","success":true})json"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);

    // Tapping mute flips the icon the moment the daemon accepts the command,
    // long before any post-command pull lands.
    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("VolumeMute"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // The pull confirms the prediction...
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), true,
                                                      capturedAroundNow(150)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);
    // ...and disarms it: the next genuinely newer reading flows through
    // instead of being pinned to the prediction.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), false,
                                                      capturedAroundNow(250)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);
}

void PlasmoidBridgeTest::failedMuteCommandRevertsTheIconImmediately() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-MUTE-FAIL"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("VolumeMute"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // The command failed: the icon returns to the last reported state right
    // away - no snapshot needs to arrive first.
    const int pullsBefore = m_fake->snapshotRequests.count(QStringLiteral("SERIAL-A"));
    Q_EMIT m_fake->CommandFinished(
        QStringLiteral("REQUEST-ONE"),
        QStringLiteral(
            R"json({"schemaVersion":1,"requestId":"REQUEST-ONE","serial":"SERIAL-A","success":false})json"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // The reverted prediction was never confirmed, so a fresh reading is
    // pulled to make sure the failed toggle cannot linger.
    QTRY_COMPARE(m_fake->snapshotRequests.count(QStringLiteral("SERIAL-A")), pullsBefore + 1);

    // The prediction is disarmed, so fresh reality flows unimpeded.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), true,
                                                      capturedAroundNow(150)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);
}

void PlasmoidBridgeTest::anInFlightCaptureCannotRegressAnArmedMuteIcon() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-MUTE-RACE"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("VolumeMute"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // A periodic poll captured after the command was sent but before the
    // television applied it still reports the old state. Its other fields
    // are fresh, but regressing the mute icon is exactly the desync the
    // prediction exists to prevent - so the state is kept.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), false,
                                                      capturedAroundNow(100)));
    QTest::qWait(50);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // The television's real state confirms the prediction...
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), true,
                                                      capturedAroundNow(200)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);
    // ...and disarms it.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), false,
                                                      capturedAroundNow(300)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);
}

void PlasmoidBridgeTest::aFreshCaptureOutranksAPinnedMuteExpectation() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-MUTE-ESCAPE"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("VolumeMute"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);

    // The command silently did not take; a capture made well after the
    // prediction cannot be an in-flight pre-command reading, so it must be
    // believed instead of pinned away - an honest icon beats a stuck one.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), false,
                                                      capturedAroundNow(4000)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // Disarmed: later readings flow unchanged.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), true,
                                                      capturedAroundNow(5000)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);
}

void PlasmoidBridgeTest::aSupersededMuteFailureKeepsTheNewerPrediction() {
    m_fake->useNumberedRequestIds = true;
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-MUTE-TWICE"));
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    Q_EMIT m_fake->DeviceSnapshotChanged(
        QStringLiteral("SERIAL-A"),
        snapshotWith(QStringLiteral("SERIAL-A"), false,
                     QStringLiteral("2026-08-20T16:00:00.000-04:00")));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // Two rapid taps: the second prediction replaces the first, and the
    // icon follows the last tap (false - muted then unmuted again).
    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("VolumeMute"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), true);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);
    proxy.sendCommand(QStringLiteral("SERIAL-A"), QStringLiteral("keypress"),
                      QStringLiteral("VolumeMute"));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // The first tap reaches the television and a pull reports its effect;
    // the second is still in flight, so the icon must not regress to it.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), true,
                                                      capturedAroundNow(100)));
    QTest::qWait(50);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // The first command then reports failure: superseded, it must not
    // clobber the newer prediction or revert the icon on its behalf.
    Q_EMIT m_fake->CommandFinished(
        QStringLiteral("REQUEST-1"),
        QStringLiteral(
            R"json({"schemaVersion":1,"requestId":"REQUEST-1","serial":"SERIAL-A","success":false})json"));
    QTest::qWait(50);
    QCOMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // The second command succeeds and its pull confirms the prediction.
    Q_EMIT m_fake->CommandFinished(
        QStringLiteral("REQUEST-2"),
        QStringLiteral(
            R"json({"schemaVersion":1,"requestId":"REQUEST-2","serial":"SERIAL-A","success":true})json"));
    QTRY_COMPARE(model.data(index, DevicesModel::PendingCommandRole).toBool(), false);
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), false,
                                                      capturedAroundNow(200)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), false);

    // Disarmed: reality flows.
    Q_EMIT m_fake->DeviceSnapshotChanged(QStringLiteral("SERIAL-A"),
                                         snapshotWith(QStringLiteral("SERIAL-A"), true,
                                                      capturedAroundNow(300)));
    QTRY_COMPARE(model.data(index, DevicesModel::MutedRole).toBool(), true);
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

void PlasmoidBridgeTest::deviceListExposesDaemonOwnedPreferences() {
    auto applet = createApplet<RokuDeckApplet>();
    DevicesModel* model = applet->devices();
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 2000);

    const QModelIndex first = firstIndex(*model);
    QCOMPARE(model->data(first, DevicesModel::SerialRole).toString(), QStringLiteral("SERIAL-A"));
    QVERIFY2(!model->data(first, DevicesModel::AllowAutomationsRole).toBool(),
             "the widget must show the automation choice the daemon stored");
    QVERIFY(model->data(first, DevicesModel::WakeOnWlanRole).toBool());
    QCOMPARE(model->data(first, DevicesModel::RefreshPolicyRole).toString(),
             QStringLiteral("battery"));
    QCOMPARE(model->data(first, DevicesModel::VolumeCeilingRole).toInt(), 50);

    const QModelIndex second = model->index(1);
    QVERIFY(model->data(second, DevicesModel::AllowAutomationsRole).toBool());
    QVERIFY(!model->data(second, DevicesModel::WakeOnWlanRole).toBool());
    QCOMPARE(model->data(second, DevicesModel::RefreshPolicyRole).toString(),
             QStringLiteral("balanced"));
    // An older daemon omits the key entirely: unrestricted by default.
    QCOMPARE(model->data(second, DevicesModel::VolumeCeilingRole).toInt(), 100);

    const QHash<int, QByteArray> roles = model->roleNames();
    QCOMPARE(roles.value(DevicesModel::AllowAutomationsRole), QByteArrayLiteral("allowAutomations"));
    QCOMPARE(roles.value(DevicesModel::WakeOnWlanRole), QByteArrayLiteral("wakeOnWlan"));
    QCOMPARE(roles.value(DevicesModel::RefreshPolicyRole), QByteArrayLiteral("refreshPolicy"));
    QCOMPARE(roles.value(DevicesModel::VolumeCeilingRole), QByteArrayLiteral("volumeCeiling"));
}

void PlasmoidBridgeTest::aMalformedVolumeCeilingKeepsTheStrictListRejection() {
    DevicesModel model;
    DaemonProxy proxy(&model, QStringLiteral("PROXY-CEILING"));
    QSignalSpy errors(&proxy, &DaemonProxy::errorOccurred);
    proxy.start();
    QTRY_COMPARE(model.rowCount(), 2);
    const QModelIndex index = firstIndex(model);
    QTRY_COMPARE(model.data(index, DevicesModel::StaleRole).toBool(), false);

    // A string ceiling is not a ceiling: the same strictness that rejects
    // other malformed device keys keeps the last good list but marks it stale.
    Q_EMIT m_fake->DeviceListChanged(QStringLiteral(
        R"json({"schemaVersion":1,"devices":[{"serial":"SERIAL-A","displayName":"Living Room","address":"192.0.2.10","volumeCeiling":"50"}]})json"));
    QTRY_COMPARE(errors.count(), 1);
    for (int row = 0; row < model.rowCount(); ++row) {
        QCOMPARE(model.data(model.index(row), DevicesModel::StaleRole).toBool(), true);
    }
}

void PlasmoidBridgeTest::deviceManagementReachesTheDaemonAndReportsRejection() {
    auto applet = createApplet<RokuDeckApplet>();
    QTRY_COMPARE_WITH_TIMEOUT(applet->devices()->rowCount(), 2, 2000);
    QSignalSpy failed(applet.get(), &RokuDeckApplet::deviceOperationFailed);

    applet->addDevice(QVariantMap{{QStringLiteral("serial"), QStringLiteral("SERIAL-C")},
                                  {QStringLiteral("displayName"), QStringLiteral("Kitchen")},
                                  {QStringLiteral("address"), QStringLiteral("198.51.100.30")}});
    QTRY_COMPARE_WITH_TIMEOUT(m_fake->addedDevices.size(), 1, 2000);
    const QJsonObject added =
        QJsonDocument::fromJson(m_fake->addedDevices.first().toUtf8()).object();
    QCOMPARE(added.value(QStringLiteral("serial")).toString(), QStringLiteral("SERIAL-C"));
    QCOMPARE(added.value(QStringLiteral("address")).toString(), QStringLiteral("198.51.100.30"));

    applet->updateDevice(QStringLiteral("SERIAL-A"),
                         QVariantMap{{QStringLiteral("refreshPolicy"),
                                      QStringLiteral("responsive")}});
    QTRY_COMPARE_WITH_TIMEOUT(m_fake->updatedDevices.size(), 1, 2000);
    QVERIFY(m_fake->updatedDevices.first().startsWith(QStringLiteral("SERIAL-A|")));
    QVERIFY(m_fake->updatedDevices.first().contains(QStringLiteral("responsive")));

    applet->removeDevice(QStringLiteral("SERIAL-B"));
    QTRY_COMPARE_WITH_TIMEOUT(m_fake->removedDevices.size(), 1, 2000);
    QCOMPARE(m_fake->removedDevices.first(), QStringLiteral("SERIAL-B"));

    applet->setDeviceOrder(QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-A")}));
    QTRY_COMPARE_WITH_TIMEOUT(m_fake->orderedSerials.size(), 2, 2000);
    QCOMPARE(m_fake->orderedSerials.first(), QStringLiteral("SERIAL-B"));

    QCOMPARE(failed.size(), 0);
    m_fake->rejectNextCall = true;
    applet->addDevice(QVariantMap{{QStringLiteral("serial"), QStringLiteral("SERIAL-D")},
                                  {QStringLiteral("address"), QStringLiteral("198.51.100.31")}});
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("addDevice"));
    QVERIFY2(!failed.first().at(1).toString().isEmpty(),
             "a rejected device change must explain itself to the user");
}

void PlasmoidBridgeTest::testDeviceReportsTheProbedSnapshot() {
    auto applet = createApplet<RokuDeckApplet>();
    QTRY_COMPARE_WITH_TIMEOUT(applet->devices()->rowCount(), 2, 2000);
    QSignalSpy finished(applet.get(), &RokuDeckApplet::deviceTestFinished);

    applet->testDevice(QStringLiteral("198.51.100.30"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 2000);
    QCOMPARE(m_fake->testedAddresses.first(), QStringLiteral("198.51.100.30"));
    QCOMPARE(finished.first().at(0).toString(), QStringLiteral("198.51.100.30"));
    const QVariantMap probed = finished.first().at(1).toMap();
    QCOMPARE(probed.value(QStringLiteral("serial")).toString(), QStringLiteral("SERIAL-A"));
    QCOMPARE(probed.value(QStringLiteral("displayName")).toString(),
             QStringLiteral("Living Room"));
    QCOMPARE(probed.value(QStringLiteral("volume")).toInt(), 45);
    QCOMPARE(probed.value(QStringLiteral("capabilities")).toStringList(),
             QStringList({QStringLiteral("power"), QStringLiteral("volume")}));
    QVERIFY2(finished.first().at(2).toString().isEmpty(),
             "a successful probe must not report an error");

    QVERIFY2(applet->devices()->rowCount() == 2,
             "probing an address must not add it to the widget's device list");

    m_fake->rejectNextCall = true;
    applet->testDevice(QStringLiteral("198.51.100.31"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 2000);
    QVERIFY2(!finished.at(1).at(2).toString().isEmpty(),
             "a failed probe must say why it failed");
    QVERIFY(finished.at(1).at(1).toMap().isEmpty());
}

void PlasmoidBridgeTest::anInFlightOperationIsReportedThroughPlasmasBusyProperty() {
    auto applet = createApplet<RokuDeckApplet>();
    QTRY_COMPARE_WITH_TIMEOUT(applet->devices()->rowCount(), 2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!applet->isBusy(), 2000);

    // Plasma::Applet::busy is FINAL: a shadowing Q_PROPERTY on the applet is
    // dropped by moc, so every QML binding on "busy" would read the base
    // class's value and never see an operation in flight.
    const QMetaObject* metaObject = applet->metaObject();
    const int busyIndex = metaObject->indexOfProperty("busy");
    QVERIFY(busyIndex >= 0);
    QCOMPARE(QString::fromLatin1(metaObject->property(busyIndex).typeName()),
             QStringLiteral("bool"));

    QSignalSpy busyChanged(applet.get(), &Plasma::Applet::busyChanged);
    m_fake->delayNextDeviceCall = true;
    applet->removeDevice(QStringLiteral("SERIAL-B"));
    QTRY_VERIFY_WITH_TIMEOUT(applet->isBusy(), 2000);
    QVERIFY(!busyChanged.isEmpty());
    QVERIFY(applet->property("busy").toBool());

    QTRY_COMPARE_WITH_TIMEOUT(m_fake->removedDevices.size(), 1, 2000);
    QVERIFY(m_fake->completeDelayedDeviceCall());
    QTRY_VERIFY_WITH_TIMEOUT(!applet->isBusy(), 2000);
    QVERIFY(!applet->property("busy").toBool());
}

namespace {

QString rulesFixtureJson() {
    return QStringLiteral(
        R"json({"schemaVersion":1,"rules":[)json"
        R"json({"schemaVersion":1,"id":"rAAA11111","name":"Nightly home","enabled":true,)json"
        R"json("mode":"queued","devices":[],"trigger":{"type":"time","kind":"daily","time":"23:00"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}],)json"
        R"json("lastRun":{"lastFiredAt":"2026-08-24T03:00:00.000Z","outcome":"ok"}},)json"
        R"json({"schemaVersion":1,"id":"rBBB22222","name":"Nudge","enabled":false,)json"
        R"json("mode":"queued","devices":[],"trigger":{"type":"interval","minutes":30},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"
        R"json(]})json");
}

} // namespace

void PlasmoidBridgeTest::appletExposesRuleChoicesAndForwardsRunOutcomes() {
    m_fake->rulesJson = rulesFixtureJson();
    auto applet = createApplet<RokuDeckApplet>();
    QTRY_COMPARE_WITH_TIMEOUT(applet->automations()->rowCount(), 2, 2000);

    // The run-rule picker asks the bridge for rule choices, excluding the
    // rule being edited so a macro cannot target itself.
    const QVariantList entries = applet->ruleEntries(QStringLiteral("rAAA11111"));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("rBBB22222"));
    QCOMPARE(entries.at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Nudge"));

    // The daemon's run outcome must reach QML, which shows "Rule finished".
    QSignalSpy finished(applet.get(), &RokuDeckApplet::ruleRunFinished);
    m_fake->nextRunOutcome = QStringLiteral("ok");
    applet->runRule(QStringLiteral("rAAA11111"));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("rAAA11111"));
    QCOMPARE(finished.at(0).at(1).toString(), QStringLiteral("ok"));
}

void PlasmoidBridgeTest::automationsModelLoadsAndTracksRulesChanged() {
    DevicesModel devices;
    AutomationsModel rules;
    DaemonProxy proxy(&devices, QStringLiteral("PROXY-RULES"), &rules);
    m_fake->rulesJson = rulesFixtureJson();
    proxy.start();

    QTRY_COMPARE(rules.rowCount(), 2);
    const QModelIndex first(rules.index(0, 0));
    QCOMPARE(first.data(AutomationsModel::IdRole).toString(), QStringLiteral("rAAA11111"));
    QCOMPARE(first.data(AutomationsModel::NameRole).toString(), QStringLiteral("Nightly home"));
    QCOMPARE(first.data(AutomationsModel::EnabledRole).toBool(), true);
    QCOMPARE(first.data(AutomationsModel::SummaryRole).toString(),
             QStringLiteral("Daily at 23:00 \u00b7 Press Home"));
    QCOMPARE(first.data(AutomationsModel::OutcomeRole).toString(), QStringLiteral("ok"));
    QCOMPARE(first.data(AutomationsModel::LastFiredRole).toString(),
             QStringLiteral("2026-08-24T03:00:00.000Z"));

    const QModelIndex second(rules.index(1, 0));
    QCOMPARE(second.data(AutomationsModel::SummaryRole).toString(),
             QStringLiteral("Every 30 minutes \u00b7 Press Home"));
    QCOMPARE(second.data(AutomationsModel::EnabledRole).toBool(), false);
    QCOMPARE(second.data(AutomationsModel::OutcomeRole).toString(), QString());

    // The daemon's announcement replaces the whole list, not patches it.
    m_fake->rulesJson = QStringLiteral(
        R"json({"schemaVersion":1,"rules":[{"schemaVersion":1,"id":"rCCC33333",)json"
        R"json("name":"Solo","enabled":true,"mode":"queued","devices":[],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Back"}]})json"
        R"json(]})json");
    Q_EMIT m_fake->RulesChanged(m_fake->rulesJson);
    QTRY_COMPARE(rules.rowCount(), 1);
    QCOMPARE(rules.index(0, 0).data(AutomationsModel::IdRole).toString(),
             QStringLiteral("rCCC33333"));
}

void PlasmoidBridgeTest::unknownFutureRuleTypesDegradeToCustomRule() {
    DevicesModel devices;
    AutomationsModel rules;
    DaemonProxy proxy(&devices, QStringLiteral("PROXY-FUTURE"), &rules);
    m_fake->rulesJson = QStringLiteral(
        R"json({"schemaVersion":1,"rules":[{"schemaVersion":1,"id":"rFFF44444",)json"
        R"json("name":"From the future","enabled":true,"mode":"queued","devices":[],)json"
        R"json("trigger":{"type":"telepathy"},"actions":[{"type":"teleport","target":"sofa"}]})json"
        R"json(]})json");
    proxy.start();

    QTRY_COMPARE(rules.rowCount(), 1);
    const QModelIndex row(rules.index(0, 0));
    QCOMPARE(row.data(AutomationsModel::NameRole).toString(), QStringLiteral("From the future"));
    QCOMPARE(row.data(AutomationsModel::SummaryRole).toString(), QStringLiteral("Custom rule"));
}

void PlasmoidBridgeTest::malformedRuleListReportsAnErrorAndKeepsTheLastGoodList() {
    DevicesModel devices;
    AutomationsModel rules;
    DaemonProxy proxy(&devices, QStringLiteral("PROXY-BADLIST"), &rules);
    m_fake->rulesJson = rulesFixtureJson();
    proxy.start();
    QTRY_COMPARE(rules.rowCount(), 2);

    QSignalSpy errors(&proxy, &DaemonProxy::errorOccurred);
    Q_EMIT m_fake->RulesChanged(QStringLiteral("{not json"));
    QTRY_COMPARE(errors.count(), 1);
    // A bad announcement must not wipe what the user is looking at.
    QCOMPARE(rules.rowCount(), 2);
}

void PlasmoidBridgeTest::ruleMutationsReachTheDaemonAndReportRejection() {
    DevicesModel devices;
    AutomationsModel rules;
    DaemonProxy proxy(&devices, QStringLiteral("PROXY-MUTATE"), &rules);
    m_fake->rulesJson = rulesFixtureJson();
    proxy.start();
    QTRY_COMPARE(rules.rowCount(), 2);

    QSignalSpy succeeded(&proxy, &DaemonProxy::deviceOperationSucceeded);
    QSignalSpy failures(&proxy, &DaemonProxy::deviceOperationFailed);
    QSignalSpy runFinished(&proxy, &DaemonProxy::ruleRunFinished);

    proxy.setRuleEnabled(QStringLiteral("rAAA11111"), false);
    QTRY_VERIFY(succeeded.count() >= 1);
    QCOMPARE(m_fake->ruleEnabledCalls, QStringList{QStringLiteral("rAAA11111|off")});

    m_fake->nextRunOutcome = QStringLiteral("partial");
    proxy.runRule(QStringLiteral("rAAA11111"));
    QTRY_COMPARE(runFinished.count(), 1);
    QCOMPARE(runFinished.at(0).at(0).toString(), QStringLiteral("rAAA11111"));
    QCOMPARE(runFinished.at(0).at(1).toString(), QStringLiteral("partial"));
    QCOMPARE(m_fake->ruleRunCalls, QStringList{QStringLiteral("rAAA11111")});

    m_fake->rejectNextRuleCall = true;
    proxy.deleteRule(QStringLiteral("rBBB22222"));
    QTRY_COMPARE(failures.count(), 1);
    QCOMPARE(failures.at(0).at(0).toString(), QStringLiteral("deleteRule"));
    QVERIFY(failures.at(0).at(1).toString().contains(QStringLiteral("fixture rejection")));
    QCOMPARE(m_fake->ruleDeletedCalls, QStringList{QStringLiteral("rBBB22222")});
}

namespace {

// The exact payload shape rulepayload.js assembles for a manual keypress
// rule; pinned byte-for-byte by the QML golden tests.
constexpr auto goldenManualKeypressPayload =
    R"json({"name":"Movie time","mode":"single","devices":["X001"],)json"
    R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"}]})json";

QString storedRuleJson(int number) {
    return QStringLiteral(
               R"json({"schemaVersion":1,"id":"rFIXTURE%1","name":"Nightly home",)json"
               R"json("enabled":true,"mode":"queued","devices":[],"trigger":{"type":"time",)json"
               R"json("kind":"daily","time":"23:00"},"actions":[{"type":"keypress","key":"Home"}]})json")
        .arg(number, 5, 10, QChar(u'0'));
}

} // namespace

void PlasmoidBridgeTest::editorFlowAddsARuleThroughTheDaemonAndSurfacesItsSummary() {
    auto applet = createApplet<RokuDeckApplet>();
    AutomationsModel* rules = applet->automations();
    QTRY_COMPARE(rules->rowCount(), 0);

    QSignalSpy succeeded(applet.get(), &RokuDeckApplet::deviceOperationSucceeded);

    applet->addRule(QString::fromLatin1(goldenManualKeypressPayload));

    QTRY_COMPARE_WITH_TIMEOUT(m_fake->addedRules.size(), 1, 2000);
    QCOMPARE(m_fake->addedRules.first(),
             QString::fromLatin1(goldenManualKeypressPayload));
    QTRY_COMPARE_WITH_TIMEOUT(succeeded.size(), 1, 2000);
    QCOMPARE(succeeded.first().at(0).toString(), QStringLiteral("addRule"));

    // The RulesChanged announcement drives the row in, daemon-normalized.
    QTRY_COMPARE_WITH_TIMEOUT(rules->rowCount(), 1, 2000);
    const QModelIndex row(rules->index(0, 0));
    QCOMPARE(row.data(AutomationsModel::IdRole).toString(),
             m_fake->ruleId(1));
    QCOMPARE(row.data(AutomationsModel::NameRole).toString(),
             QStringLiteral("Movie time"));
    QCOMPARE(row.data(AutomationsModel::EnabledRole).toBool(), true);
    QCOMPARE(row.data(AutomationsModel::SummaryRole).toString(),
             QStringLiteral("Manually run \u00b7 Press Home"));
}

void PlasmoidBridgeTest::anEditedRenamePersistsAcrossProxyRecreation() {
    m_fake->storeRule(
        QJsonDocument::fromJson(storedRuleJson(1).toUtf8()).object(),
        m_fake->ruleId(1));
    m_fake->rulesJson = QStringLiteral(
        R"json({"schemaVersion":1,"rules":[%1]})json").arg(storedRuleJson(1));

    DevicesModel devicesA;
    AutomationsModel rulesA;
    DaemonProxy proxyA(&devicesA, QStringLiteral("PROXY-EDIT-A"), &rulesA);
    proxyA.start();
    QTRY_COMPARE(rulesA.rowCount(), 1);
    QCOMPARE(rulesA.index(0, 0).data(AutomationsModel::NameRole).toString(),
             QStringLiteral("Nightly home"));

    // A complete UpdateRule document: same id and enabled, new name only.
    const QString renamed = QStringLiteral(
        R"json({"schemaVersion":1,"id":"%1","name":"Renamed nightly","enabled":true,)json"
        R"json("mode":"queued","devices":[],"trigger":{"type":"time",)json"
        R"json("kind":"daily","time":"23:00"},"actions":[{"type":"keypress","key":"Home"}]})json")
        .arg(m_fake->ruleId(1));
    proxyA.updateRule(renamed);
    QTRY_COMPARE_WITH_TIMEOUT(m_fake->updatedRules.size(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        rulesA.index(0, 0).data(AutomationsModel::NameRole).toString(),
        QStringLiteral("Renamed nightly"), 2000);

    // A fresh proxy reads the renamed rule back from storage.
    proxyA.stop();
    DevicesModel devicesB;
    AutomationsModel rulesB;
    DaemonProxy proxyB(&devicesB, QStringLiteral("PROXY-EDIT-B"), &rulesB);
    proxyB.start();
    QTRY_COMPARE(rulesB.rowCount(), 1);
    QCOMPARE(rulesB.index(0, 0).data(AutomationsModel::IdRole).toString(),
             m_fake->ruleId(1));
    QCOMPARE(rulesB.index(0, 0).data(AutomationsModel::NameRole).toString(),
             QStringLiteral("Renamed nightly"));
}

void PlasmoidBridgeTest::aRejectedAddRuleExplainsItselfWithoutAdding() {
    auto applet = createApplet<RokuDeckApplet>();
    AutomationsModel* rules = applet->automations();
    QTRY_COMPARE(rules->rowCount(), 0);

    QSignalSpy failed(applet.get(), &RokuDeckApplet::deviceOperationFailed);
    m_fake->rejectNextRuleCall = true;
    applet->addRule(QString::fromLatin1(goldenManualKeypressPayload));

    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
    QCOMPARE(failed.first().at(0).toString(), QStringLiteral("addRule"));
    QVERIFY(!failed.first().at(1).toString().isEmpty());
    QTest::qWait(50);
    QCOMPARE(rules->rowCount(), 0);
}

QTEST_MAIN(PlasmoidBridgeTest)

#include "plasmoid_bridge_test.moc"

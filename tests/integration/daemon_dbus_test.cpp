// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_contract.h"
#include "core/process_runner.h"
#include "daemon/config_store.h"
#include "daemon/daemon_service.h"
#include "rokudeck1_interface.h"
#include "support/fake_process_runner.h"

#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtTest>

#include <chrono>
#include <memory>

using namespace RokuDeck;
using namespace RokuDeck::Test;
using namespace std::chrono_literals;

class DaemonDbusTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();

    void generatedProxyAndAdaptorExposeExactContract();
    void twoClientsShareOneDeviceInterest();
    void disconnectingBusOwnerRemovesItsInterest();
    void sameClientIdFromDifferentOwnersDoesNotCollide();
    void registrationFailureCleansObjectAndReportsDiagnostic();
    void daemonExecutableReportsRegistrationFailure();
    void lifecycleTracksClientsAndEnabledAutomations();
    void deviceJsonIsVersionedAndAddressChangesPreserveIdentity();
    void addressChangeInvalidatesSnapshotAndRejectsStalePoll();
    void discoveryUpdatesSerialKeyedRegistry();
    void discoveryFailureReturnsTypedError();
    void queryFailureDoesNotBlockAnotherDevice();
    void incompatibleRdsDisablesPollingAndExposesDiagnostic();
    void immediateMethodFailuresUseTypedDbusErrors();
    void commandSchemaRejectsUnboundedAndUnsupportedWork();
    void commandQueueIsBoundedPerDevice();
    void expiryBetweenSweepAndDispatchDoesNotReachRunner();
    void deepestQueuedCommandsExpireWithoutDispatch();
    void commandsSerializePerDevice();
    void commandsOnDifferentDevicesRunIndependently();
    void pendingCommandDefersActualIdleExpiry();
    void deviceManagementAddsRenamesAndRemovesDevices();
    void deviceManagementRejectsInvalidInput();
    void deviceOrderIsUserControlledAndPersisted();
    void discoveryPreservesConfiguredDeviceSettings();
    void testDeviceProbesAnAddressWithoutRegisteringIt();
    void refreshPolicyBoundsHowOftenTheDaemonPollsADevice();
    void aUserChosenNameSurvivesDiscoveryAndPolling();
    void powerAndAudioKeypressesHonourObservedDeviceState();
    void addRuleAssignsIdPersistsAndAnnounces();
    void addRuleRejectsClientOwnedAndMalformedPayloads();
    void updateRuleReplacesDefinitionInPlaceAndPersists();
    void deleteRuleRemovesAndLaterMutationsError();
    void setRuleEnabledFlipsWithoutTouchingDefinition();
    void runRuleWithoutConsentingDevicesIsConsentBlocked();
    void runRuleDispatchesKeypressThroughTheCommandPipeline();
    void runRuleReportsUndeliveredCommandsAsFailed();
    void runRuleReportsMixedDeliveryAsPartial();
    void runRuleReportsUnsupportedActionsAsFailed();
    void runRuleWithUnknownIdErrors();
    void runRuleWithMatchingConditionsRunsAndReportsOk();
    void runRuleWithFailingConditionsRecordsConditionFalse();
    void scheduledDailyRuleFiresThroughTheCommandPipeline();
    void disablingARuleUnschedulesItsNextFire();
    void onceRuleMissedByLessThanADayFiresAtStartup();
    void intervalRuleReanchorsAcrossDaemonRestarts();
    void pendingScheduleKeepsTheDaemonAwake();
    void stateChangeRuleFiresOnRisingEdgeOnly();
    void firstObservationIsBaselineAndNeverFires();
    void muteActionPressesVolumeMuteOnce();
    void muteActionMatchingObservedStateIsANoOp();
    void setVolumeWalksDownWithBoundedPresses();
    void volumeActionsWithoutObservedAudioFailWithoutDispatching();
    void mixedAudioStateAcrossTargetsYieldsPartial();
    void notYetExecutableActionsAbortTheWholeRunWithoutDispatching();
    void macroChainRunsChildAndReportsOk();
    void selfReferencingMacroReportsCycleError();
    void missingMacroTargetCountsAsFailure();

  private:
    [[nodiscard]] QDBusMessage call(const QString& method,
                                    const QVariantList& arguments = {}) const;
    [[nodiscard]] static QDBusMessage callOnConnection(const QDBusConnection& connection,
                                                       const QString& method,
                                                       const QVariantList& arguments = {});
    [[nodiscard]] static QDBusMessage waitForReply(const QDBusPendingCall& pending);
    [[nodiscard]] static QJsonObject jsonResult(const QDBusMessage& reply);
    [[nodiscard]] static bool waitForHelperRegistration(QProcess& client);
    void recreateDaemon(std::chrono::milliseconds idleGrace = 30s,
                        DaemonService::DeadlineRemaining deadlineRemaining = {},
                        DaemonService::WallClock wallClock = {});
    QString addManualRule(const QString& name);
    QString addRuleReturningId(const QString& payload);
    QJsonObject rulesListing() const;
    // Completes pending fake-runner callbacks while the daemon blocks inside
    // a rule run waiting for delivery verdicts. `script` drives per-callback
    // exit codes (0 = delivered, non-zero = transport failure); it defaults
    // to every command succeeding.
    void startRunnerCompleter(const QList<int>& script = {});
    void stopRunnerCompleter();
    // Home keypress requests seen by the fake runner, i.e. stateChange
    // firings that reached a device.
    [[nodiscard]] int homeKeypressCount() const;
    // Keypress requests for one specific key seen by the fake runner.
    [[nodiscard]] int keypressCount(const QString& key) const;
    // Serves the one-time rds --version probe that precedes the very first
    // device query of a daemon's lifetime.
    void completePollVersion();
    // Waits for the next device-info request, then serves it plus the
    // active-app/player/audio-device readings. Only one poll can be in flight
    // per serial, so this consumes exactly one cycle no matter how eagerly the
    // scheduler re-arms. A firing triggered by the audio response dispatches
    // synchronously inside this call.
    void completePollReadings(int volume, bool muted = false);

    QTemporaryDir m_configDirectory;
    QString m_configPath;
    bool m_automationEnabled = true;
    QDateTime m_now;
    FakeProcessRunner* m_runner = nullptr;
    QTimer m_runnerCompleter;
    QList<int> m_completerScript;
    bool m_completerWired = false;
    std::unique_ptr<DaemonService> m_daemon;
    QString m_clientConnectionName;
    std::unique_ptr<QDBusConnection> m_clientConnection;
    std::unique_ptr<IoGithubJosephurRokuDeck1Interface> m_proxy;
};

void DaemonDbusTest::init() {
    QVERIFY2(QDBusConnection::sessionBus().isConnected(),
             "daemon-dbus must run on a private session bus");
    QVERIFY(m_configDirectory.isValid());
    m_automationEnabled = true;
    m_configPath = m_configDirectory.filePath(QString::fromLatin1(QTest::currentTestFunction()) +
                                              QStringLiteral(".rc"));

    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 0));
    recreateDaemon(30s, {}, [this] { return m_now; });
}

void DaemonDbusTest::recreateDaemon(std::chrono::milliseconds idleGrace,
                                    DaemonService::DeadlineRemaining deadlineRemaining,
                                    DaemonService::WallClock wallClock) {
    m_proxy.reset();
    m_daemon.reset();
    auto runner = std::make_unique<FakeProcessRunner>();
    m_runner = runner.get();
    auto config = KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig);
    auto configStore = std::make_unique<ConfigStore>(std::move(config));
    m_daemon = std::make_unique<DaemonService>(
        QDBusConnection::sessionBus(), [this] { return m_automationEnabled; }, std::move(runner),
        std::move(configStore), idleGrace, std::move(deadlineRemaining), std::move(wallClock));
    QVERIFY2(m_daemon->registerOnBus(),
             "the test daemon could not own its private session-bus name");
    if (!m_clientConnection) {
        m_clientConnectionName =
            QStringLiteral("rokudeck-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_clientConnection = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_clientConnectionName));
        QVERIFY2(m_clientConnection->isConnected(),
                 "could not create the test client bus connection");
    }
    m_proxy = std::make_unique<IoGithubJosephurRokuDeck1Interface>(
        QStringLiteral("io.github.josephur.RokuDeck1"),
        QStringLiteral("/io/github/josephur/RokuDeck1"), *m_clientConnection);
}

void DaemonDbusTest::cleanup() {
    m_proxy.reset();
    m_daemon.reset();
    m_runner = nullptr;
    m_clientConnection.reset();
    if (!m_clientConnectionName.isEmpty()) {
        QDBusConnection::disconnectFromBus(m_clientConnectionName);
        m_clientConnectionName.clear();
    }
    QCoreApplication::processEvents();
}

QDBusMessage DaemonDbusTest::call(const QString& method, const QVariantList& arguments) const {
    return callOnConnection(QDBusConnection::sessionBus(), method, arguments);
}

QDBusMessage DaemonDbusTest::callOnConnection(const QDBusConnection& connection,
                                              const QString& method,
                                              const QVariantList& arguments) {
    QDBusMessage request =
        QDBusMessage::createMethodCall(QStringLiteral("io.github.josephur.RokuDeck1"),
                                       QStringLiteral("/io/github/josephur/RokuDeck1"),
                                       QStringLiteral("io.github.josephur.RokuDeck1"), method);
    request.setArguments(arguments);
    return waitForReply(connection.asyncCall(request, 2000));
}

QDBusMessage DaemonDbusTest::waitForReply(const QDBusPendingCall& pending) {
    QDBusPendingCallWatcher watcher(pending);
    if (!watcher.isFinished()) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(2500);
        loop.exec();
    }
    if (!watcher.isFinished()) {
        return QDBusMessage::createError(QStringLiteral("io.github.josephur.RokuDeck1.TestTimeout"),
                                         QStringLiteral("D-Bus test call timed out"));
    }
    return watcher.reply();
}

bool DaemonDbusTest::waitForHelperRegistration(QProcess& client) {
    QByteArray clientOutput;
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (!clientOutput.contains("registered") && waitTimer.elapsed() < 1000) {
        QTest::qWait(10);
        clientOutput.append(client.readAllStandardOutput());
    }
    return clientOutput.contains("registered");
}

void DaemonDbusTest::generatedProxyAndAdaptorExposeExactContract() {
    QCOMPARE(QString::fromLatin1(IoGithubJosephurRokuDeck1Interface::staticInterfaceName()),
             QStringLiteral("io.github.josephur.RokuDeck1"));
    const QDBusMessage listed = waitForReply(m_proxy->ListDevices());
    QVERIFY2(listed.type() != QDBusMessage::ErrorMessage, qPrintable(listed.errorMessage()));
    QCOMPARE(jsonResult(listed).value(QStringLiteral("schemaVersion")).toInt(), 1);

    QDBusInterface introspection(QStringLiteral("io.github.josephur.RokuDeck1"),
                                 QStringLiteral("/io/github/josephur/RokuDeck1"),
                                 QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                 QDBusConnection::sessionBus());
    const QDBusMessage introspected =
        waitForReply(introspection.asyncCall(QStringLiteral("Introspect")));
    QVERIFY2(introspected.type() != QDBusMessage::ErrorMessage,
             qPrintable(introspected.errorMessage()));
    QVERIFY(introspected.arguments().size() == 1);
    const QString introspectionXml = introspected.arguments().front().toString();
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownDevice")));
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCommand")));
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCapability")));
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.SafetyRejected")));
    QVERIFY(
        introspectionXml.contains(QStringLiteral("io.github.josephur.RokuDeck1.Error.Timeout")));
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.TransportFailure")));
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.InvalidRule")));
    QVERIFY(introspectionXml.contains(
        QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownRule")));

    QHash<QString, QStringList> methods;
    QHash<QString, QStringList> signals;
    QXmlStreamReader xml(introspectionXml);
    bool inTargetInterface = false;
    QString currentMember;
    bool currentIsSignal = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"interface") {
            inTargetInterface = xml.attributes().value(u"name") == u"io.github.josephur.RokuDeck1";
        } else if (inTargetInterface && xml.isStartElement() &&
                   (xml.name() == u"method" || xml.name() == u"signal")) {
            currentMember = xml.attributes().value(u"name").toString();
            currentIsSignal = xml.name() == u"signal";
            (currentIsSignal ? signals : methods).insert(currentMember, {});
        } else if (inTargetInterface && !currentMember.isEmpty() && xml.isStartElement() &&
                   xml.name() == u"arg") {
            const QString type = xml.attributes().value(u"type").toString();
            const QString direction = xml.attributes().value(u"direction").toString();
            (currentIsSignal ? signals : methods)[currentMember].append(
                currentIsSignal ? type : direction + QChar(u':') + type);
        } else if (xml.isEndElement() && (xml.name() == u"method" || xml.name() == u"signal")) {
            currentMember.clear();
        } else if (xml.isEndElement() && xml.name() == u"interface" && inTargetInterface) {
            inTargetInterface = false;
        }
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QCOMPARE(methods.size(), 18);
    QCOMPARE(methods.value(QStringLiteral("ListDevices")), QStringList{QStringLiteral("out:s")});
    QCOMPARE(methods.value(QStringLiteral("GetSnapshot")),
             QStringList({QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(
        methods.value(QStringLiteral("RegisterClient")),
        QStringList({QStringLiteral("in:s"), QStringLiteral("in:as"), QStringLiteral("in:u")}));
    QCOMPARE(
        methods.value(QStringLiteral("UpdateClientInterest")),
        QStringList({QStringLiteral("in:s"), QStringLiteral("in:as"), QStringLiteral("in:u")}));
    QCOMPARE(methods.value(QStringLiteral("UnregisterClient")),
             QStringList{QStringLiteral("in:s")});
    QCOMPARE(methods.value(QStringLiteral("DiscoverDevices")),
             QStringList{QStringLiteral("out:s")});
    QCOMPARE(
        methods.value(QStringLiteral("SendCommand")),
        QStringList({QStringLiteral("in:s"), QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(methods.value(QStringLiteral("AddDevice")),
             QStringList({QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(
        methods.value(QStringLiteral("UpdateDevice")),
        QStringList({QStringLiteral("in:s"), QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(methods.value(QStringLiteral("RemoveDevice")),
             QStringList{QStringLiteral("in:s")});
    QCOMPARE(methods.value(QStringLiteral("SetDeviceOrder")),
             QStringList{QStringLiteral("in:as")});
    QCOMPARE(methods.value(QStringLiteral("TestDevice")),
             QStringList({QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(methods.value(QStringLiteral("ListRules")), QStringList{QStringLiteral("out:s")});
    QCOMPARE(methods.value(QStringLiteral("AddRule")),
             QStringList({QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(methods.value(QStringLiteral("UpdateRule")), QStringList{QStringLiteral("in:s")});
    QCOMPARE(methods.value(QStringLiteral("DeleteRule")), QStringList{QStringLiteral("in:s")});
    QCOMPARE(methods.value(QStringLiteral("SetRuleEnabled")),
             QStringList({QStringLiteral("in:s"), QStringLiteral("in:b")}));
    QCOMPARE(methods.value(QStringLiteral("RunRule")),
             QStringList({QStringLiteral("in:s"), QStringLiteral("out:s")}));
    QCOMPARE(signals.size(), 4);
    QCOMPARE(signals.value(QStringLiteral("DeviceSnapshotChanged")),
             QStringList({QStringLiteral("s"), QStringLiteral("s")}));
    QCOMPARE(signals.value(QStringLiteral("DeviceListChanged")), QStringList{QStringLiteral("s")});
    QCOMPARE(signals.value(QStringLiteral("CommandFinished")),
             QStringList({QStringLiteral("s"), QStringLiteral("s")}));
    QCOMPARE(signals.value(QStringLiteral("RulesChanged")), QStringList{QStringLiteral("s")});

    QSignalSpy generatedSignal(m_proxy.get(),
                               &IoGithubJosephurRokuDeck1Interface::DeviceListChanged);
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Generated Fixture"),
                            QStringLiteral("192.0.2.80")});
    QTRY_COMPARE_WITH_TIMEOUT(generatedSignal.size(), 1, 500);
    QCOMPARE(QJsonDocument::fromJson(generatedSignal.front().front().toString().toUtf8())
                 .object()
                 .value(QStringLiteral("schemaVersion"))
                 .toInt(),
             1);

    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("generated-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1000u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    const QDBusMessage updated = waitForReply(m_proxy->UpdateClientInterest(
        QStringLiteral("generated-widget"), QStringList{QStringLiteral("SERIAL-B")}, 2500u));
    QVERIFY2(updated.type() != QDBusMessage::ErrorMessage, qPrintable(updated.errorMessage()));
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 0u);
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-B")), 2500u);
}

void DaemonDbusTest::disconnectingBusOwnerRemovesItsInterest() {
    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());

    QProcess client;
    client.setProgram(QCoreApplication::applicationDirPath() +
                      QStringLiteral("/daemon-client-helper"));
    client.setArguments(
        {QStringLiteral("widget-owner"), QStringLiteral("SERIAL-A"), QStringLiteral("1200")});
    client.start();
    QVERIFY(client.waitForStarted());
    if (!waitForHelperRegistration(client)) {
        const QString failure = QStringLiteral("helper did not register: %1")
                                    .arg(QString::fromUtf8(client.readAllStandardError()));
        client.kill();
        client.waitForFinished();
        QFAIL(qPrintable(failure));
    }
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 1200u);
    QCOMPARE(m_daemon->watchedClientOwnerCount(), 1);
    QVERIFY(!m_daemon->idleTimerActive());

    client.terminate();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 0u, 1000);
    QCOMPARE(m_daemon->watchedClientOwnerCount(), 0);
    QVERIFY(m_daemon->idleTimerActive());
}

void DaemonDbusTest::sameClientIdFromDifferentOwnersDoesNotCollide() {
    QProcess first;
    first.setProgram(QCoreApplication::applicationDirPath() +
                     QStringLiteral("/daemon-client-helper"));
    first.setArguments(
        {QStringLiteral("shared-widget"), QStringLiteral("SERIAL-A"), QStringLiteral("1100")});
    first.start();
    QVERIFY(first.waitForStarted());
    QVERIFY(waitForHelperRegistration(first));

    QProcess second;
    second.setProgram(QCoreApplication::applicationDirPath() +
                      QStringLiteral("/daemon-client-helper"));
    second.setArguments(
        {QStringLiteral("shared-widget"), QStringLiteral("SERIAL-B"), QStringLiteral("2200")});
    second.start();
    QVERIFY(second.waitForStarted());
    QVERIFY(waitForHelperRegistration(second));

    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 1100u);
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-B")), 2200u);
    QCOMPARE(m_daemon->watchedClientOwnerCount(), 2);

    first.terminate();
    QTRY_COMPARE_WITH_TIMEOUT(first.state(), QProcess::NotRunning, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 0u, 1000);
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-B")), 2200u);
    QCOMPARE(m_daemon->watchedClientOwnerCount(), 1);

    second.terminate();
    QTRY_COMPARE_WITH_TIMEOUT(second.state(), QProcess::NotRunning, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(m_daemon->effectiveInterval(QStringLiteral("SERIAL-B")), 0u, 1000);
    QCOMPARE(m_daemon->watchedClientOwnerCount(), 0);
}

void DaemonDbusTest::registrationFailureCleansObjectAndReportsDiagnostic() {
    const QString connectionName = QStringLiteral("rokudeck-registration-failure-fixture");
    QDBusConnection challenger = QDBusConnection::connectToBus(
        QString::fromUtf8(qgetenv("DBUS_SESSION_BUS_ADDRESS")), connectionName);
    QVERIFY(challenger.isConnected());

    auto config = KSharedConfig::openConfig(
        m_configDirectory.filePath(QStringLiteral("challenger.rc")), KConfig::SimpleConfig);
    auto challengerDaemon = std::make_unique<DaemonService>(
        challenger, [] { return true; }, std::make_unique<FakeProcessRunner>(),
        std::make_unique<ConfigStore>(std::move(config)), 30s);
    QVERIFY(!challengerDaemon->registerOnBus());
    QVERIFY(!challengerDaemon->lastRegistrationError().isEmpty());

    QObject replacement;
    QVERIFY(
        challenger.registerObject(QStringLiteral("/io/github/josephur/RokuDeck1"), &replacement));
    challenger.unregisterObject(QStringLiteral("/io/github/josephur/RokuDeck1"));
    challengerDaemon.reset();
    QDBusConnection::disconnectFromBus(connectionName);
}

void DaemonDbusTest::daemonExecutableReportsRegistrationFailure() {
    QProcess daemon;
    daemon.setProgram(QCoreApplication::applicationDirPath() + QStringLiteral("/rokudeckd"));
    daemon.start();
    QVERIFY(daemon.waitForStarted());
    QVERIFY(daemon.waitForFinished(1000));
    QCOMPARE(daemon.exitStatus(), QProcess::NormalExit);
    QCOMPARE(daemon.exitCode(), 1);
    QVERIFY(QString::fromUtf8(daemon.readAllStandardError())
                .contains(QStringLiteral("rokudeckd could not register on D-Bus:")));
}

QJsonObject DaemonDbusTest::jsonResult(const QDBusMessage& reply) {
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() != 1) {
        return {};
    }
    return QJsonDocument::fromJson(reply.arguments().front().toString().toUtf8()).object();
}

void DaemonDbusTest::twoClientsShareOneDeviceInterest() {
    const QDBusMessage first =
        call(QStringLiteral("RegisterClient"),
             {QStringLiteral("widget-a"), QStringList{QStringLiteral("SERIAL-A")}, 1000u});
    const QDBusMessage second =
        call(QStringLiteral("RegisterClient"),
             {QStringLiteral("widget-b"), QStringList{QStringLiteral("SERIAL-A")}, 5000u});
    QVERIFY2(first.type() != QDBusMessage::ErrorMessage, qPrintable(first.errorMessage()));
    QVERIFY2(second.type() != QDBusMessage::ErrorMessage, qPrintable(second.errorMessage()));
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 1000u);

    const QDBusMessage otherDevice =
        call(QStringLiteral("RegisterClient"),
             {QStringLiteral("widget-c"), QStringList{QStringLiteral("SERIAL-B")}, 7000u});
    QVERIFY2(otherDevice.type() != QDBusMessage::ErrorMessage,
             qPrintable(otherDevice.errorMessage()));

    const QDBusMessage removed =
        call(QStringLiteral("UnregisterClient"), {QStringLiteral("widget-a")});
    QVERIFY2(removed.type() != QDBusMessage::ErrorMessage, qPrintable(removed.errorMessage()));
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 5000u);

    const QDBusMessage lastRemoved =
        call(QStringLiteral("UnregisterClient"), {QStringLiteral("widget-b")});
    QVERIFY2(lastRemoved.type() != QDBusMessage::ErrorMessage,
             qPrintable(lastRemoved.errorMessage()));
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 0u);
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-B")), 7000u);
}

void DaemonDbusTest::lifecycleTracksClientsAndEnabledAutomations() {
    QVERIFY(!m_daemon->idleTimerActive());
    QCOMPARE(m_daemon->idleGracePeriodMs(), 30000);

    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());

    const QDBusMessage registered =
        call(QStringLiteral("RegisterClient"),
             {QStringLiteral("widget-a"), QStringList{QStringLiteral("SERIAL-A")}, 1000u});
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QVERIFY(!m_daemon->idleTimerActive());

    const QDBusMessage removed =
        call(QStringLiteral("UnregisterClient"), {QStringLiteral("widget-a")});
    QVERIFY2(removed.type() != QDBusMessage::ErrorMessage, qPrintable(removed.errorMessage()));
    QVERIFY(m_daemon->idleTimerActive());

    m_automationEnabled = true;
    m_daemon->automationEnablementChanged();
    QVERIFY(!m_daemon->idleTimerActive());
}

void DaemonDbusTest::deviceJsonIsVersionedAndAddressChangesPreserveIdentity() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.10")});
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.11")});

    const QDBusMessage reply = call(QStringLiteral("ListDevices"));
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    const QJsonObject payload = jsonResult(reply);
    QCOMPARE(payload.value(QStringLiteral("schemaVersion")).toInt(), 1);
    const QJsonArray devices = payload.value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 1);
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("address")).toString(),
             QStringLiteral("192.0.2.11"));

    ConfigStore reloaded(KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig));
    const QList<DeviceRecord> stored = reloaded.loadDevices();
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.front().serial, QStringLiteral("SERIAL-A"));
    QCOMPARE(stored.front().address, QStringLiteral("192.0.2.11"));
}

void DaemonDbusTest::addressChangeInvalidatesSnapshotAndRejectsStalePoll() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.30")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("address-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    const QByteArray onlineDevice = QByteArrayLiteral(
        R"json({"deviceName":"Fixture Room","serialNumber":"SERIAL-A","powerMode":"PowerOn","supportsFindRemote":true})json");
    m_runner->complete({0, onlineDevice, {}, false});
    QCOMPARE(m_runner->requests.size(), 5);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<active-app><app id=\"fixture-app\" type=\"appl\">Fixture App</app></active-app>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<player state=\"idle\"/>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>18</volume><mute-status>false</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});
    QCOMPARE(snapshots.size(), 1);
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("SERIAL-A")}))
                 .value(QStringLiteral("address"))
                 .toString(),
             QStringLiteral("192.0.2.30"));

    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 6, 1000);
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.31")});
    const QJsonObject invalidated =
        jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("SERIAL-A")}));
    QCOMPARE(invalidated.value(QStringLiteral("address")).toString(), QStringLiteral("192.0.2.31"));
    QCOMPARE(invalidated.value(QStringLiteral("health")).toString(), QStringLiteral("unknown"));

    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.30")});
    const QJsonObject returnedAddress =
        jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("SERIAL-A")}));
    QCOMPARE(returnedAddress.value(QStringLiteral("address")).toString(),
             QStringLiteral("192.0.2.30"));
    QCOMPARE(returnedAddress.value(QStringLiteral("health")).toString(), QStringLiteral("unknown"));

    m_runner->complete({0, onlineDevice, {}, false});
    QCOMPARE(m_runner->requests.size(), 9);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<active-app><app id=\"fixture-app\" type=\"appl\">Fixture App</app></active-app>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<player state=\"idle\"/>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>18</volume><mute-status>false</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});
    QCOMPARE(snapshots.size(), 1);
    const QJsonObject devices = jsonResult(call(QStringLiteral("ListDevices")));
    QCOMPARE(devices.value(QStringLiteral("devices"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("address"))
                 .toString(),
             QStringLiteral("192.0.2.30"));
}

void DaemonDbusTest::discoveryUpdatesSerialKeyedRegistry() {
    QSignalSpy changed(m_daemon.get(), &DaemonService::DeviceListChanged);
    const QDBusPendingReply<QString> discovery = m_proxy->DiscoverDevices();
    QDBusPendingCallWatcher discoveryWatcher(discovery);
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 500);
    QVERIFY(!discoveryWatcher.isFinished());
    QCOMPARE(m_runner->lastRequest.program, QStringLiteral("rds"));
    QCOMPARE(m_runner->lastRequest.arguments,
             QStringList({QStringLiteral("--json"), QStringLiteral("--quiet"),
                          QStringLiteral("--timeout"), QStringLiteral("3000"),
                          QStringLiteral("discover")}));
    QCOMPARE(m_runner->lastRequest.timeout, 7s);

    const QByteArray discovered = R"json([
        {"serialNumber":"SERIAL-A","deviceName":"First Fixture","ip":"198.51.100.40"},
        {"serialNumber":"SERIAL-B","deviceName":"Second Fixture","ip":"198.51.100.41"}
    ])json";
    m_runner->complete({0, discovered, {}, false});
    const QDBusMessage completed = waitForReply(discovery);
    QVERIFY2(completed.type() != QDBusMessage::ErrorMessage, qPrintable(completed.errorMessage()));
    const QJsonArray completedDevices =
        jsonResult(completed).value(QStringLiteral("devices")).toArray();
    QCOMPARE(completedDevices.size(), 2);
    QCOMPARE(completedDevices.at(0).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(changed.size(), 1);
    QJsonArray devices =
        jsonResult(waitForReply(m_proxy->ListDevices())).value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 2);

    const QDBusPendingReply<QString> rediscovery = m_proxy->DiscoverDevices();
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 2, 500);
    const QByteArray moved = R"json([
        {"serialNumber":"SERIAL-A","deviceName":"First Fixture","ip":"198.51.100.42"}
    ])json";
    m_runner->complete({0, moved, {}, false});
    const QDBusMessage rediscovered = waitForReply(rediscovery);
    QVERIFY2(rediscovered.type() != QDBusMessage::ErrorMessage,
             qPrintable(rediscovered.errorMessage()));
    QCOMPARE(jsonResult(rediscovered)
                 .value(QStringLiteral("devices"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("address"))
                 .toString(),
             QStringLiteral("198.51.100.42"));
    QCOMPARE(changed.size(), 2);
    devices =
        jsonResult(waitForReply(m_proxy->ListDevices())).value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("address")).toString(),
             QStringLiteral("198.51.100.42"));
}

void DaemonDbusTest::discoveryFailureReturnsTypedError() {
    const QDBusPendingReply<QString> discovery = m_proxy->DiscoverDevices();
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 500);
    m_runner->complete({-1, {}, QByteArrayLiteral("fixture timeout"), true});

    const QDBusMessage reply = waitForReply(discovery);
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(reply.errorName(), QStringLiteral("io.github.josephur.RokuDeck1.Error.Timeout"));
}

void DaemonDbusTest::incompatibleRdsDisablesPollingAndExposesDiagnostic() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.22")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("polling-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 500);
    QCOMPARE(m_runner->lastRequest.arguments, QStringList{QStringLiteral("--version")});

    m_runner->complete({0, QByteArrayLiteral("2.0.0\n"), {}, false});
    QTRY_VERIFY_WITH_TIMEOUT(!m_daemon->pollingEnabled(), 500);
    QVERIFY(m_daemon->pollingDiagnostic().contains(QStringLiteral("1.0.0")));
    QTest::qWait(150);
    QCOMPARE(m_runner->requests.size(), 1);

    const QJsonObject snapshot =
        jsonResult(waitForReply(m_proxy->GetSnapshot(QStringLiteral("SERIAL-A"))));
    QCOMPARE(snapshot.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QVERIFY(
        snapshot.value(QStringLiteral("diagnostic")).toString().contains(QStringLiteral("1.0.0")));
}

void DaemonDbusTest::immediateMethodFailuresUseTypedDbusErrors() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("203.0.113.50")});
    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);

    const QDBusMessage unknown = waitForReply(m_proxy->GetSnapshot(QStringLiteral("MISSING")));
    QCOMPARE(unknown.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(unknown.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownDevice"));

    const QDBusMessage unsupported = waitForReply(m_proxy->SendCommand(
        QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"operation":"erase"})json")));
    QCOMPARE(unsupported.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(unsupported.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCommand"));

    const QDBusMessage capability = waitForReply(m_proxy->SendCommand(
        QStringLiteral("SERIAL-A"),
        QStringLiteral(R"json({"operation":"keypress","key":"FindRemote"})json")));
    QCOMPARE(capability.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(capability.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCapability"));

    const QDBusMessage safety = waitForReply(m_proxy->SendCommand(
        QStringLiteral("SERIAL-A"),
        QStringLiteral(R"json({"operation":"keypress","key":"VolumeUp"})json")));
    QCOMPARE(safety.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(safety.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.SafetyRejected"));
    QCOMPARE(finished.size(), 0);
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::queryFailureDoesNotBlockAnotherDevice() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Unavailable Fixture"),
                            QStringLiteral("192.0.2.20")});
    m_daemon->upsertDevice({QStringLiteral("SERIAL-B"), QStringLiteral("Standby Fixture"),
                            QStringLiteral("192.0.2.21")});
    const QDBusMessage registered =
        call(QStringLiteral("RegisterClient"),
             {QStringLiteral("widget-a"),
              QStringList{QStringLiteral("SERIAL-A"), QStringLiteral("SERIAL-B")}, 1u});
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);

    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    QCOMPARE(m_runner->requests.size(), 3);
    m_runner->complete({1, {}, QByteArrayLiteral("fixture failure"), false});
    const QByteArray standbyDevice = QByteArrayLiteral(
        R"json({"deviceName":"Standby Fixture","serialNumber":"SERIAL-B","powerMode":"Ready"})json");
    m_runner->complete({0, standbyDevice, {}, false});
    QCOMPARE(m_runner->requests.size(), 6);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<active-app><app id=\"home\" type=\"home\">Roku</app></active-app>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<player state=\"idle\"/>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>12</volume><mute-status>false</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});

    QCOMPARE(snapshots.size(), 1);
    QCOMPARE(snapshots.at(0).at(0).toString(), QStringLiteral("SERIAL-B"));
    const QJsonObject snapshot =
        QJsonDocument::fromJson(snapshots.at(0).at(1).toString().toUtf8()).object();
    QCOMPARE(snapshot.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(snapshot.value(QStringLiteral("health")).toString(), QStringLiteral("standby"));
}

void DaemonDbusTest::commandSchemaRejectsUnboundedAndUnsupportedWork() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("203.0.113.50")});
    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);
    const QString oversized = QStringLiteral("{\"operation\":\"keypress\",\"key\":\"") +
                              QString(3000, QChar(u'x')) + QStringLiteral("\"}");
    const QStringList rejected{
        QStringLiteral("{"),
        QStringLiteral(R"json({"arguments":["keypress","Home"]})json"),
        QStringLiteral(R"json({"operation":"erase"})json"),
        QStringLiteral(R"json({"operation":"keypress","key":"--help"})json"),
        oversized,
        QStringLiteral(R"json({"operation":"keypress","key":"FindRemote"})json")};

    for (qsizetype index = 0; index < rejected.size(); ++index) {
        const QDBusMessage reply =
            waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), rejected.at(index)));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(reply.errorName(),
                 index == rejected.size() - 1
                     ? QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCapability")
                     : QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCommand"));
    }
    QCOMPARE(finished.size(), 0);
    QVERIFY(m_runner->requests.isEmpty());

    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("capability-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Fixture Room","serialNumber":"SERIAL-A","powerMode":"PowerOn","supportsFindRemote":true})json"),
         {},
         false});
    QCOMPARE(m_runner->requests.size(), 5);
    m_runner->complete({1, {}, QByteArrayLiteral("fixture app failure"), false});
    m_runner->complete({1, {}, QByteArrayLiteral("fixture media failure"), false});
    m_runner->complete({1, {}, QByteArrayLiteral("fixture audio failure"), false});
    const QDBusMessage unregistered =
        waitForReply(m_proxy->UnregisterClient(QStringLiteral("capability-widget")));
    QVERIFY2(unregistered.type() != QDBusMessage::ErrorMessage,
             qPrintable(unregistered.errorMessage()));

    const QString findRemote =
        QStringLiteral(R"json({"operation":"keypress","key":"FindRemote"})json");
    const QDBusMessage accepted =
        waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), findRemote));
    QVERIFY2(accepted.type() != QDBusMessage::ErrorMessage, qPrintable(accepted.errorMessage()));
    QCOMPARE(m_runner->requests.size(), 6);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("FindRemote")}));
    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
}

void DaemonDbusTest::commandQueueIsBoundedPerDevice() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.20")});
    const QString command = QStringLiteral(R"json({"operation":"keypress","key":"Home"})json");
    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);

    for (int index = 0; index < CommandContract::maximumOutstandingPerDevice; ++index) {
        const QDBusMessage accepted =
            waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command));
        QVERIFY2(accepted.type() != QDBusMessage::ErrorMessage,
                 qPrintable(accepted.errorMessage()));
        QVERIFY(!accepted.arguments().front().toString().isEmpty());
    }
    const QDBusMessage rejected =
        waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command));
    QCOMPARE(rejected.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(rejected.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.TransportFailure"));

    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(finished.size(), 0);
}

void DaemonDbusTest::expiryBetweenSweepAndDispatchDoesNotReachRunner() {
    int deadlineReads = 0;
    recreateDaemon(
        30s, [&deadlineReads](const QDeadlineTimer&) { return deadlineReads++ == 0 ? 1 : 0; });
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.20")});
    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);

    const QDBusMessage reply = waitForReply(
        m_proxy->SendCommand(QStringLiteral("SERIAL-A"),
                             QStringLiteral(R"json({"operation":"keypress","key":"Home"})json")));

    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 500);
    QCOMPARE(m_runner->requests.size(), 0);
    const QJsonObject result =
        QJsonDocument::fromJson(finished.front().at(1).toString().toUtf8()).object();
    QCOMPARE(result.value(QStringLiteral("success")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("timeout"));
}

void DaemonDbusTest::deepestQueuedCommandsExpireWithoutDispatch() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.20")});
    const QString command = QStringLiteral(R"json({"operation":"keypress","key":"Home"})json");
    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);
    for (int index = 0; index < CommandContract::maximumOutstandingPerDevice; ++index) {
        const QDBusMessage reply =
            waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command));
        QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    }
    QCOMPARE(m_runner->requests.size(), 1);

    QTest::qWait(CommandContract::daemonCompletionDeadlineMs + 50);
    m_runner->complete({0, QByteArrayLiteral("{}"), {}, false});

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), CommandContract::maximumOutstandingPerDevice, 500);
    QCOMPARE(m_runner->requests.size(), 1);
    int timedOut = 0;
    for (const QList<QVariant>& arguments : finished) {
        const QJsonObject result =
            QJsonDocument::fromJson(arguments.at(1).toString().toUtf8()).object();
        timedOut += result.value(QStringLiteral("error")).toString() == QStringLiteral("timeout");
    }
    QCOMPARE(timedOut, CommandContract::maximumOutstandingPerDevice - 1);
}

void DaemonDbusTest::commandsSerializePerDevice() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.20")});
    const QString command = QStringLiteral(R"json({"operation":"keypress","key":"Home"})json");

    const QDBusMessage first =
        call(QStringLiteral("SendCommand"), {QStringLiteral("SERIAL-A"), command});
    const QDBusMessage second =
        call(QStringLiteral("SendCommand"), {QStringLiteral("SERIAL-A"), command});
    QVERIFY2(first.type() != QDBusMessage::ErrorMessage, qPrintable(first.errorMessage()));
    QVERIFY2(second.type() != QDBusMessage::ErrorMessage, qPrintable(second.errorMessage()));
    QVERIFY(first.arguments().front().toString() != second.arguments().front().toString());
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->callbacks.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.first(6),
             QStringList({QStringLiteral("--json"), QStringLiteral("--quiet"),
                          QStringLiteral("--timeout"), QStringLiteral("3000"),
                          QStringLiteral("--ip"), QStringLiteral("198.51.100.20")}));

    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);
    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
    QCOMPARE(finished.size(), 1);
    QCOMPARE(m_runner->requests.size(), 2);
    QCOMPARE(m_runner->callbacks.size(), 1);

    m_runner->complete({1, {}, QByteArrayLiteral("fixture failure"), false});
    QCOMPARE(finished.size(), 2);
    const QJsonObject result =
        QJsonDocument::fromJson(finished.at(1).at(1).toString().toUtf8()).object();
    QCOMPARE(result.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(result.value(QStringLiteral("success")).toBool(), false);
}

void DaemonDbusTest::commandsOnDifferentDevicesRunIndependently() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("First Fixture"),
                            QStringLiteral("198.51.100.60")});
    m_daemon->upsertDevice({QStringLiteral("SERIAL-B"), QStringLiteral("Second Fixture"),
                            QStringLiteral("198.51.100.61")});
    const QString command = QStringLiteral(R"json({"operation":"keypress","key":"Home"})json");
    const QString firstId = waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command))
                                .arguments()
                                .front()
                                .toString();
    const QString queuedId = waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command))
                                 .arguments()
                                 .front()
                                 .toString();
    const QString independentId =
        waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-B"), command))
            .arguments()
            .front()
            .toString();

    QCOMPARE(m_runner->requests.size(), 2);
    QCOMPARE(m_runner->requests.at(0).arguments.at(5), QStringLiteral("198.51.100.60"));
    QCOMPARE(m_runner->requests.at(1).arguments.at(5), QStringLiteral("198.51.100.61"));
    QCOMPARE(m_runner->callbacks.size(), 2);
    QSignalSpy finished(m_daemon.get(), &DaemonService::CommandFinished);

    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
    QCOMPARE(m_runner->requests.size(), 3);
    QCOMPARE(m_runner->callbacks.size(), 2);
    QCOMPARE(finished.at(0).at(0).toString(), firstId);

    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
    QCOMPARE(finished.at(1).at(0).toString(), independentId);
    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
    QCOMPARE(finished.at(2).at(0).toString(), queuedId);
}

void DaemonDbusTest::pendingCommandDefersActualIdleExpiry() {
    m_automationEnabled = false;
    recreateDaemon(30ms);
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("203.0.113.70")});
    QSignalSpy idleExit(m_daemon.get(), &DaemonService::idleExitRequested);
    QSignalSpy commandFinished(m_daemon.get(), &DaemonService::CommandFinished);
    QVERIFY(m_daemon->idleTimerActive());

    const QString command = QStringLiteral(R"json({"operation":"keypress","key":"Home"})json");
    const QDBusMessage accepted =
        waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command));
    QVERIFY2(accepted.type() != QDBusMessage::ErrorMessage, qPrintable(accepted.errorMessage()));
    QVERIFY(!m_daemon->idleTimerActive());
    QTest::qWait(80);
    QCOMPARE(idleExit.size(), 0);

    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
    QCOMPARE(commandFinished.size(), 1);
    QVERIFY(m_daemon->idleTimerActive());
    QTRY_COMPARE_WITH_TIMEOUT(idleExit.size(), 1, 500);
}

namespace {

[[nodiscard]] QString deviceJson(const QString& serial, const QString& displayName,
                                 const QString& address) {
    return QStringLiteral(R"json({"serial":"%1","displayName":"%2","address":"%3"})json")
        .arg(serial, displayName, address);
}

[[nodiscard]] QJsonObject deviceEntry(const QJsonObject& listing, const QString& serial) {
    const QJsonArray devices = listing.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& entry : devices) {
        if (entry.toObject().value(QStringLiteral("serial")).toString() == serial) {
            return entry.toObject();
        }
    }
    return {};
}

[[nodiscard]] QStringList listedSerials(const QJsonObject& listing) {
    QStringList serials;
    const QJsonArray devices = listing.value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& entry : devices) {
        serials.append(entry.toObject().value(QStringLiteral("serial")).toString());
    }
    return serials;
}

[[nodiscard]] QStringList listedRuleNames(const QJsonObject& listing) {
    QStringList names;
    const QJsonArray rules = listing.value(QStringLiteral("rules")).toArray();
    for (const QJsonValue& entry : rules) {
        names.append(entry.toObject().value(QStringLiteral("name")).toString());
    }
    return names;
}

} // namespace

void DaemonDbusTest::deviceManagementAddsRenamesAndRemovesDevices() {
    QSignalSpy changed(m_daemon.get(), &DaemonService::DeviceListChanged);

    const QDBusMessage added =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(added.type() != QDBusMessage::ErrorMessage, qPrintable(added.errorMessage()));
    QJsonObject record = jsonResult(added);
    QCOMPARE(record.value(QStringLiteral("serial")).toString(), QStringLiteral("SERIAL-A"));
    QCOMPARE(record.value(QStringLiteral("displayName")).toString(), QStringLiteral("Living Room"));
    QCOMPARE(record.value(QStringLiteral("address")).toString(), QStringLiteral("198.51.100.10"));
    QVERIFY2(record.value(QStringLiteral("allowAutomations")).toBool(),
             "automations must be allowed until the user opts out");
    QVERIFY2(!record.value(QStringLiteral("wakeOnWlan")).toBool(),
             "waking a sleeping TV must stay opt-in");
    QCOMPARE(record.value(QStringLiteral("refreshPolicy")).toString(), QStringLiteral("balanced"));
    QCOMPARE(changed.size(), 1);

    const QDBusMessage duplicate =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Second Try"),
                         QStringLiteral("198.51.100.11"))});
    QCOMPARE(duplicate.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
            .value(QStringLiteral("address"))
            .toString(),
        QStringLiteral("198.51.100.10"));

    const QDBusMessage updated = call(
        QStringLiteral("UpdateDevice"),
        {QStringLiteral("SERIAL-A"),
         QStringLiteral(
             R"json({"displayName":"Den","refreshPolicy":"responsive","allowAutomations":false})json")});
    QVERIFY2(updated.type() != QDBusMessage::ErrorMessage, qPrintable(updated.errorMessage()));
    record = jsonResult(updated);
    QCOMPARE(record.value(QStringLiteral("displayName")).toString(), QStringLiteral("Den"));
    QCOMPARE(record.value(QStringLiteral("refreshPolicy")).toString(),
             QStringLiteral("responsive"));
    QVERIFY(!record.value(QStringLiteral("allowAutomations")).toBool());
    QVERIFY2(record.value(QStringLiteral("address")).toString() ==
                 QStringLiteral("198.51.100.10"),
             "an unnamed key must keep its stored value");

    recreateDaemon();
    record =
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"));
    QCOMPARE(record.value(QStringLiteral("displayName")).toString(), QStringLiteral("Den"));
    QCOMPARE(record.value(QStringLiteral("refreshPolicy")).toString(),
             QStringLiteral("responsive"));
    QVERIFY(!record.value(QStringLiteral("allowAutomations")).toBool());

    const QDBusMessage removed = call(QStringLiteral("RemoveDevice"), {QStringLiteral("SERIAL-A")});
    QVERIFY2(removed.type() != QDBusMessage::ErrorMessage, qPrintable(removed.errorMessage()));
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))).size(), 0);

    const QDBusMessage removedAgain =
        call(QStringLiteral("RemoveDevice"), {QStringLiteral("SERIAL-A")});
    QCOMPARE(removedAgain.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(removedAgain.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownDevice"));

    recreateDaemon();
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))).size(), 0);
}

void DaemonDbusTest::deviceManagementRejectsInvalidInput() {
    const QStringList rejectedAdds{
        QStringLiteral(R"json({"displayName":"No Serial","address":"198.51.100.10"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"198.51.100"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"999.1.1.1"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"not-an-address"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"127.0.0.1"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"224.0.0.1"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"0.0.0.0"})json"),
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"::1"})json"),
        QStringLiteral(
            R"json({"serial":"SERIAL-A","address":"198.51.100.10","refreshPolicy":"turbo"})json"),
        QStringLiteral(R"json({"serial":"  ","address":"198.51.100.10"})json"),
        QStringLiteral(R"json(["not","an","object"])json"),
        QStringLiteral(R"json(not json at all)json"),
    };
    for (const QString& payload : rejectedAdds) {
        const QDBusMessage reply = call(QStringLiteral("AddDevice"), {payload});
        QVERIFY2(reply.type() == QDBusMessage::ErrorMessage,
                 qPrintable(QStringLiteral("AddDevice accepted %1").arg(payload)));
    }

    const QString controlCharacterName =
        QStringLiteral(R"json({"serial":"SERIAL-A","address":"198.51.100.10","displayName":"a%1b"})json")
            .arg(QChar(u'\u0007'));
    QCOMPARE(call(QStringLiteral("AddDevice"), {controlCharacterName}).type(),
             QDBusMessage::ErrorMessage);

    const QString oversized =
        QStringLiteral(
            R"json({"serial":"SERIAL-A","address":"198.51.100.10","displayName":"%1"})json")
            .arg(QString(8192, QChar(u'x')));
    QCOMPARE(call(QStringLiteral("AddDevice"), {oversized}).type(), QDBusMessage::ErrorMessage);

    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))).size(), 0);

    const QDBusMessage unknownUpdate =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("SERIAL-MISSING"), QStringLiteral(R"json({"displayName":"X"})json")});
    QCOMPARE(unknownUpdate.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(unknownUpdate.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownDevice"));

    const QDBusMessage accepted =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(accepted.type() != QDBusMessage::ErrorMessage, qPrintable(accepted.errorMessage()));

    const QDBusMessage badUpdate =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"address":"10.0.0"})json")});
    QCOMPARE(badUpdate.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
            .value(QStringLiteral("address"))
            .toString(),
        QStringLiteral("198.51.100.10"));

    const QDBusMessage serialChange =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"serial":"SERIAL-B"})json")});
    QVERIFY2(serialChange.type() == QDBusMessage::ErrorMessage,
             "a device identity must not be rewritten through UpdateDevice");
}

void DaemonDbusTest::deviceOrderIsUserControlledAndPersisted() {
    const QList<std::pair<QString, QString>> fixtures{
        {QStringLiteral("SERIAL-A"), QStringLiteral("198.51.100.10")},
        {QStringLiteral("SERIAL-B"), QStringLiteral("198.51.100.11")},
        {QStringLiteral("SERIAL-C"), QStringLiteral("198.51.100.12")}};
    for (const auto& [serial, address] : fixtures) {
        const QDBusMessage added =
            call(QStringLiteral("AddDevice"),
                 {deviceJson(serial, QStringLiteral("Fixture"), address)});
        QVERIFY2(added.type() != QDBusMessage::ErrorMessage, qPrintable(added.errorMessage()));
    }
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-A"), QStringLiteral("SERIAL-B"),
                          QStringLiteral("SERIAL-C")}));

    QSignalSpy changed(m_daemon.get(), &DaemonService::DeviceListChanged);
    const QDBusMessage reordered =
        call(QStringLiteral("SetDeviceOrder"),
             {QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-A"),
                           QStringLiteral("SERIAL-B")})});
    QVERIFY2(reordered.type() != QDBusMessage::ErrorMessage, qPrintable(reordered.errorMessage()));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-A"),
                          QStringLiteral("SERIAL-B")}));

    const QDBusMessage unknown =
        call(QStringLiteral("SetDeviceOrder"),
             {QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-MISSING")})});
    QCOMPARE(unknown.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-A"),
                          QStringLiteral("SERIAL-B")}));

    const QDBusMessage duplicated =
        call(QStringLiteral("SetDeviceOrder"),
             {QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-C")})});
    QCOMPARE(duplicated.type(), QDBusMessage::ErrorMessage);

    const QDBusMessage partial =
        call(QStringLiteral("SetDeviceOrder"), {QStringList{QStringLiteral("SERIAL-B")}});
    QVERIFY2(partial.type() != QDBusMessage::ErrorMessage, qPrintable(partial.errorMessage()));
    QVERIFY2(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))) ==
                 QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-C"),
                              QStringLiteral("SERIAL-A")}),
             "devices the caller omitted must keep their relative order behind the named ones");

    recreateDaemon();
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-C"),
                          QStringLiteral("SERIAL-A")}));
}

void DaemonDbusTest::discoveryPreservesConfiguredDeviceSettings() {
    QDBusMessage reply =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    reply = call(QStringLiteral("AddDevice"),
                 {deviceJson(QStringLiteral("SERIAL-B"), QStringLiteral("Bedroom"),
                             QStringLiteral("198.51.100.11"))});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    reply = call(
        QStringLiteral("UpdateDevice"),
        {QStringLiteral("SERIAL-A"),
         QStringLiteral(
             R"json({"displayName":"Renamed By User","allowAutomations":false,"wakeOnWlan":true,"refreshPolicy":"battery"})json")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    reply = call(QStringLiteral("SetDeviceOrder"),
                 {QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-A")})});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));

    const QDBusPendingReply<QString> discovery = m_proxy->DiscoverDevices();
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 500);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json([{"serialNumber":"SERIAL-A","deviceName":"Roku Advertised Name","ip":"198.51.100.42"}])json"),
         {},
         false});
    const QDBusMessage discovered = waitForReply(discovery);
    QVERIFY2(discovered.type() != QDBusMessage::ErrorMessage,
             qPrintable(discovered.errorMessage()));

    const QJsonObject record =
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"));
    QVERIFY2(record.value(QStringLiteral("address")).toString() ==
                 QStringLiteral("198.51.100.42"),
             "discovery must still follow a device that moved");
    QVERIFY2(!record.value(QStringLiteral("allowAutomations")).toBool(),
             "discovery must not re-enable automations the user turned off");
    QVERIFY2(record.value(QStringLiteral("wakeOnWlan")).toBool(),
             "discovery must not discard the wake preference");
    QCOMPARE(record.value(QStringLiteral("refreshPolicy")).toString(), QStringLiteral("battery"));
    QVERIFY2(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))) ==
                 QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-A")}),
             "discovery must not reshuffle the order the user chose");
}

void DaemonDbusTest::testDeviceProbesAnAddressWithoutRegisteringIt() {
    const QDBusMessage rejected = call(QStringLiteral("TestDevice"), {QStringLiteral("10.0.0")});
    QCOMPARE(rejected.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(m_runner->requests.size(), 0);

    const QDBusPendingReply<QString> probe = m_proxy->TestDevice(QStringLiteral("198.51.100.55"));

    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 500);
    QCOMPARE(m_runner->lastRequest.arguments, QStringList{QStringLiteral("--version")});
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});

    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 2, 500);
    QVERIFY2(m_runner->lastRequest.arguments.contains(QStringLiteral("198.51.100.55")),
             "the probe must target the address the caller asked about");
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Probed Fixture","serialNumber":"SERIAL-PROBE","powerMode":"PowerOn"})json"),
         {},
         false});
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 5, 500);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<active-app><app id=\"home\" type=\"home\">Roku</app></active-app>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<player state=\"idle\"/>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>27</volume><mute-status>false</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});

    const QDBusMessage probed = waitForReply(probe);
    QVERIFY2(probed.type() != QDBusMessage::ErrorMessage, qPrintable(probed.errorMessage()));
    const QJsonObject snapshot = jsonResult(probed);
    QCOMPARE(snapshot.value(QStringLiteral("serial")).toString(), QStringLiteral("SERIAL-PROBE"));
    QCOMPARE(snapshot.value(QStringLiteral("displayName")).toString(),
             QStringLiteral("Probed Fixture"));
    QCOMPARE(snapshot.value(QStringLiteral("address")).toString(),
             QStringLiteral("198.51.100.55"));
    QCOMPARE(snapshot.value(QStringLiteral("power")).toString(), QStringLiteral("on"));
    QCOMPARE(snapshot.value(QStringLiteral("volume")).toInt(), 27);

    QVERIFY2(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))).isEmpty(),
             "testing an address must never register it behind the user's back");
}

void DaemonDbusTest::refreshPolicyBoundsHowOftenTheDaemonPollsADevice() {
    QDBusMessage reply =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    reply = call(QStringLiteral("UpdateDevice"),
                 {QStringLiteral("SERIAL-A"),
                  QStringLiteral(R"json({"refreshPolicy":"battery"})json")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));

    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("hungry-widget"), QStringList{QStringLiteral("SERIAL-A")}, 250u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QVERIFY2(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")) >= 30000u,
             "a battery-conscious device must not be polled at the rate a widget asked for");

    reply = call(QStringLiteral("UpdateDevice"),
                 {QStringLiteral("SERIAL-A"),
                  QStringLiteral(R"json({"refreshPolicy":"responsive"})json")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    QVERIFY2(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")) < 30000u,
             "relaxing the policy must take effect without re-registering the widget");
}

void DaemonDbusTest::aUserChosenNameSurvivesDiscoveryAndPolling() {
    QDBusMessage reply =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Advertised Name"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));

    // Before the user renames anything, the device still owns its own name.
    QDBusPendingReply<QString> discovery = m_proxy->DiscoverDevices();
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 500);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json([{"serialNumber":"SERIAL-A","deviceName":"Renamed By Roku","ip":"198.51.100.10"}])json"),
         {},
         false});
    QVERIFY2(waitForReply(discovery).type() != QDBusMessage::ErrorMessage, "discovery failed");
    QCOMPARE(
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
            .value(QStringLiteral("displayName"))
            .toString(),
        QStringLiteral("Renamed By Roku"));

    reply = call(QStringLiteral("UpdateDevice"),
                 {QStringLiteral("SERIAL-A"),
                  QStringLiteral(R"json({"displayName":"Kitchen"})json")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));

    discovery = m_proxy->DiscoverDevices();
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 2, 500);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json([{"serialNumber":"SERIAL-A","deviceName":"Renamed By Roku","ip":"198.51.100.11"}])json"),
         {},
         false});
    QVERIFY2(waitForReply(discovery).type() != QDBusMessage::ErrorMessage, "rediscovery failed");

    QJsonObject record =
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"));
    QVERIFY2(record.value(QStringLiteral("displayName")).toString() == QStringLiteral("Kitchen"),
             "discovery must not undo the name the user typed");
    QVERIFY2(record.value(QStringLiteral("address")).toString() ==
                 QStringLiteral("198.51.100.11"),
             "the address must still follow the device");

    // A poll reports the advertised name too, and must not undo the rename
    // either: polling is the path that runs continuously.
    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("naming-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QTRY_VERIFY_WITH_TIMEOUT(m_runner->requests.size() >= 3, 500);
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    QTRY_VERIFY_WITH_TIMEOUT(m_runner->requests.size() >= 4, 500);
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Renamed By Roku","serialNumber":"SERIAL-A","powerMode":"PowerOn"})json"),
         {},
         false});
    QTRY_VERIFY_WITH_TIMEOUT(m_runner->callbacks.size() >= 3, 500);
    while (!m_runner->callbacks.isEmpty()) {
        m_runner->complete({0, QByteArrayLiteral(R"json({"success":false})json"), {}, false});
    }

    // The snapshot is what widgets actually display, so the rename has to
    // survive there as well, not only in the device registry.
    QCOMPARE(snapshots.size(), 1);
    const QJsonObject emitted =
        QJsonDocument::fromJson(snapshots.at(0).at(1).toString().toUtf8()).object();
    QVERIFY2(emitted.value(QStringLiteral("displayName")).toString() == QStringLiteral("Kitchen"),
             "the snapshot signal must carry the name the user typed");
    const QJsonObject stored =
        jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("SERIAL-A")}));
    QVERIFY2(stored.value(QStringLiteral("displayName")).toString() == QStringLiteral("Kitchen"),
             "GetSnapshot must carry the name the user typed");

    QCOMPARE(
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
            .value(QStringLiteral("displayName"))
            .toString(),
        QStringLiteral("Kitchen"));

    // Clearing the name hands ownership back to the device, which is the only
    // way a user could recover a name they cannot otherwise know.
    reply = call(QStringLiteral("UpdateDevice"),
                 {QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"displayName":""})json")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));

    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Renamed By Roku"),
                            QStringLiteral("198.51.100.11")});
    QCOMPARE(
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
            .value(QStringLiteral("displayName"))
            .toString(),
        QStringLiteral("Renamed By Roku"));
}

void DaemonDbusTest::powerAndAudioKeypressesHonourObservedDeviceState() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.30")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("keypad-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Fixture Room","serialNumber":"SERIAL-A","powerMode":"PowerOn"})json"),
         {},
         false});
    QCOMPARE(m_runner->requests.size(), 5);
    m_runner->complete({1, {}, QByteArrayLiteral("fixture app failure"), false});
    m_runner->complete({1, {}, QByteArrayLiteral("fixture media failure"), false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>12</volume><mute-status>false</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});

    // Power toggling and, once an audio reading exists, the audio keys are all
    // plain ECP keypresses; the widget relies on that contract.
    const QStringList accepted{QStringLiteral("Power"),    QStringLiteral("PowerOn"),
                               QStringLiteral("VolumeUp"), QStringLiteral("VolumeDown"),
                               QStringLiteral("VolumeMute")};
    for (const QString& key : accepted) {
        const QString command =
            QStringLiteral(R"json({"operation":"keypress","key":"%1"})json").arg(key);
        const QDBusMessage reply =
            waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), command));
        QVERIFY2(reply.type() != QDBusMessage::ErrorMessage,
                 qPrintable(QStringLiteral("%1 was rejected: %2").arg(key, reply.errorMessage())));
        QCOMPARE(m_runner->lastRequest.arguments.last(2),
                 QStringList({QStringLiteral("keypress"), key}));
        m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
    }

    // Losing the observed state (here: the device moved, invalidating the
    // snapshot) must re-arm the audio safety gate, while power stays available.
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.31")});
    const QDBusMessage volume =
        waitForReply(m_proxy->SendCommand(
            QStringLiteral("SERIAL-A"),
            QStringLiteral(R"json({"operation":"keypress","key":"VolumeUp"})json")));
    QCOMPARE(volume.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(volume.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.SafetyRejected"));

    const QDBusMessage power = waitForReply(m_proxy->SendCommand(
        QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"operation":"keypress","key":"Power"})json")));
    QVERIFY2(power.type() != QDBusMessage::ErrorMessage, qPrintable(power.errorMessage()));
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Power")}));
    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
}

void DaemonDbusTest::addRuleAssignsIdPersistsAndAnnounces() {
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);
    const QString input =
        QStringLiteral(R"json({"name":"Movie time","trigger":{"type":"manual"},)json"
                       R"json("actions":[{"type":"keypress","key":"Select"}]})json");

    const QDBusMessage created = waitForReply(m_proxy->AddRule(input));
    QVERIFY2(created.type() != QDBusMessage::ErrorMessage,
             qPrintable(created.errorMessage()));
    const QJsonObject rule = jsonResult(created);
    QVERIFY2(rule.value(QStringLiteral("id")).toString().startsWith(QLatin1Char('r')),
             "the daemon must mint the rule id, not the caller");
    QVERIFY2(rule.value(QStringLiteral("id")).toString().size() > 1,
             "the minted rule id must carry entropy beyond the prefix");
    QCOMPARE(rule.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(rule.value(QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(rule.value(QStringLiteral("name")).toString(), QStringLiteral("Movie time"));
    QCOMPARE(rule.value(QStringLiteral("trigger")).toObject().value(QStringLiteral("type")),
             QJsonValue(QStringLiteral("manual")));
    QVERIFY2(!rule.contains(QStringLiteral("lastRun")),
             "a freshly created rule has never run");

    const QJsonObject listing = jsonResult(waitForReply(m_proxy->ListRules()));
    QCOMPARE(listing.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(listing.value(QStringLiteral("rules")).toArray().size(), 1);
    QCOMPARE(listing.value(QStringLiteral("rules")).toArray().at(0).toObject(), rule);

    // The announcement is the same envelope ListRules serves.
    QCOMPARE(changed.size(), 1);
    const QJsonObject announced =
        QJsonDocument::fromJson(changed.front().front().toString().toUtf8()).object();
    QCOMPARE(announced, listing);

    // Restarting the daemon against the same config keeps the rule.
    recreateDaemon();
    const QJsonObject reloaded = jsonResult(waitForReply(m_proxy->ListRules()));
    QCOMPARE(reloaded.value(QStringLiteral("rules")).toArray().size(), 1);
    QCOMPARE(reloaded.value(QStringLiteral("rules")).toArray().at(0).toObject(), rule);
}

void DaemonDbusTest::addRuleRejectsClientOwnedAndMalformedPayloads() {
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);
    const QStringList rejected{
        QStringLiteral("not json at all"),
        // id and enabled are daemon-owned; supplying either is a contract breach.
        QStringLiteral(
            R"json({"id":"rCALLER","name":"X","trigger":{"type":"manual"},)json"
            R"json("actions":[{"type":"keypress","key":"Home"}]})json"),
        QStringLiteral(
            R"json({"enabled":false,"name":"X","trigger":{"type":"manual"},)json"
            R"json("actions":[{"type":"keypress","key":"Home"}]})json"),
        QStringLiteral(
            R"json({"name":"X","trigger":{"type":"telepathy"},)json"
            R"json("actions":[{"type":"keypress","key":"Home"}]})json"),
        QStringLiteral(R"json({"name":"X","trigger":{"type":"manual"}})json")};

    for (const QString& payload : rejected) {
        const QDBusMessage reply = waitForReply(m_proxy->AddRule(payload));
        QVERIFY2(reply.type() == QDBusMessage::ErrorMessage,
                 qPrintable(QStringLiteral("AddRule accepted %1").arg(payload)));
        QCOMPARE(reply.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    }

    // The daemon-owned field breaches name the violation; parse failures carry
    // their specific validator message instead.
    const QDBusMessage callerId =
        waitForReply(m_proxy->AddRule(rejected.at(1)));
    QVERIFY2(callerId.errorMessage().contains(QStringLiteral("invalidRule")),
             qPrintable(callerId.errorMessage()));
    const QDBusMessage unknownTrigger =
        waitForReply(m_proxy->AddRule(rejected.at(3)));
    QVERIFY2(unknownTrigger.errorMessage().contains(QStringLiteral("unknown trigger type")),
             qPrintable(unknownTrigger.errorMessage()));

    QCOMPARE(changed.size(), 0);
    QCOMPARE(jsonResult(waitForReply(m_proxy->ListRules()))
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .size(),
             0);
}

void DaemonDbusTest::startRunnerCompleter(const QList<int>& script) {
    if (!m_completerWired) {
        connect(&m_runnerCompleter, &QTimer::timeout, this, [this] {
            while (m_runner != nullptr && !m_runner->callbacks.isEmpty()) {
                const int code =
                    m_completerScript.isEmpty() ? 0 : m_completerScript.takeFirst();
                ProcessResult result;
                result.exitCode = code;
                result.standardOutput = code == 0
                    ? QByteArrayLiteral("{\"ok\":true}\n")
                    : QByteArrayLiteral(
                          "{\"success\":false,\"error\":\"Request timed out\"}\n");
                m_runner->complete(result);
            }
        });
        m_completerWired = true;
    }
    m_completerScript = script;
    m_runnerCompleter.start(5);
}

void DaemonDbusTest::stopRunnerCompleter() {
    m_runnerCompleter.stop();
    m_completerScript.clear();
    // A stray poll may have started while a firing's delivery wait pumped the
    // loop; drain it so later cycles sync on a clean pending queue. The junk
    // payload fails cleanly wherever it lands.
    while (m_runner != nullptr && !m_runner->callbacks.isEmpty()) {
        m_runner->complete({0, QByteArrayLiteral("{}\n"), {}, false});
    }
}

int DaemonDbusTest::homeKeypressCount() const {
    return keypressCount(QStringLiteral("Home"));
}

int DaemonDbusTest::keypressCount(const QString& key) const {
    int count = 0;
    for (const ProcessRequest& request : m_runner->requests) {
        if (request.arguments.size() >= 2
            && request.arguments.last(2)
                == QStringList({QStringLiteral("keypress"), key})) {
            ++count;
        }
    }
    return count;
}

void DaemonDbusTest::completePollVersion() {
    QVERIFY2(QTest::qWaitFor([&] { return !m_runner->callbacks.isEmpty(); }, 15s),
             "the rds --version probe never reached the fake runner");
    m_runner->complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
}

void DaemonDbusTest::completePollReadings(int volume, bool muted) {
    QVERIFY2(QTest::qWaitFor([&] { return !m_runner->callbacks.isEmpty(); }, 15s),
             "the next device-info request never reached the fake runner");
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Fixture Room","serialNumber":"SERIAL-A","powerMode":"PowerOn"})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<active-app><app id=\"fixture-app\" type=\"appl\">Fixture App</app></active-app>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<player state=\"idle\"/>","status":200,"headers":{}})json"),
         {},
         false});
    m_runner->complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>)json") +
         QByteArray::number(volume)
         + QByteArrayLiteral(R"json(</volume><mute-status>)json")
         + (muted ? QByteArrayLiteral("true") : QByteArrayLiteral("false"))
         + QByteArrayLiteral(
             R"json(</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});
}

QString DaemonDbusTest::addRuleReturningId(const QString& payload) {
    const QDBusMessage created = waitForReply(m_proxy->AddRule(payload));
    if (created.type() == QDBusMessage::ErrorMessage) {
        QTest::qFail(qPrintable(QStringLiteral("AddRule fixture failed: %1")
                                    .arg(created.errorMessage())),
                     __FILE__, __LINE__);
        return {};
    }
    return jsonResult(created).value(QStringLiteral("id")).toString();
}

QString DaemonDbusTest::addManualRule(const QString& name) {
    return addRuleReturningId(QStringLiteral(
        R"json({"name":"%1","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Select"}]})json").arg(name));
}

QJsonObject DaemonDbusTest::rulesListing() const {
    return jsonResult(waitForReply(m_proxy->ListRules()));
}

void DaemonDbusTest::updateRuleReplacesDefinitionInPlaceAndPersists() {
    const QString firstId = addManualRule(QStringLiteral("Movie time"));
    addManualRule(QStringLiteral("Second entry"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // A complete replacement document: absent optional blocks would clear.
    const QString renamed = QStringLiteral(
        R"json({"id":"%1","name":"Premiere night","enabled":true,"mode":"restart",)json"
        R"json("devices":[],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json").arg(firstId);
    const QDBusMessage reply = waitForReply(m_proxy->UpdateRule(renamed));
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));

    QCOMPARE(changed.size(), 1);
    QCOMPARE(listedRuleNames(rulesListing()),
             QStringList({QStringLiteral("Premiere night"), QStringLiteral("Second entry")}));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("mode"))
                 .toString(),
             QStringLiteral("restart"));

    recreateDaemon();
    QCOMPARE(listedRuleNames(rulesListing()),
             QStringList({QStringLiteral("Premiere night"), QStringLiteral("Second entry")}));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("id"))
                 .toString(),
             firstId);
}

void DaemonDbusTest::deleteRuleRemovesAndLaterMutationsError() {
    const QString doomedId = addManualRule(QStringLiteral("Doomed"));
    addManualRule(QStringLiteral("Survivor"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    const QDBusMessage removed = waitForReply(m_proxy->DeleteRule(doomedId));
    QVERIFY2(removed.type() != QDBusMessage::ErrorMessage, qPrintable(removed.errorMessage()));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(listedRuleNames(rulesListing()), QStringList{QStringLiteral("Survivor")});

    // Every mutation of a deleted id reports the typed unknown-rule error.
    const QString zombieUpdate = QStringLiteral(
        R"json({"id":"%1","name":"Zombie","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json").arg(doomedId);
    const QDBusMessage resurrected = waitForReply(m_proxy->UpdateRule(zombieUpdate));
    QCOMPARE(resurrected.type(), QDBusMessage::ErrorMessage);
    QVERIFY2(resurrected.errorMessage().contains(QStringLiteral("unknownRule")),
             qPrintable(resurrected.errorMessage()));

    const QDBusMessage redeleted = waitForReply(m_proxy->DeleteRule(doomedId));
    QCOMPARE(redeleted.type(), QDBusMessage::ErrorMessage);
    QVERIFY2(redeleted.errorMessage().contains(QStringLiteral("unknownRule")),
             qPrintable(redeleted.errorMessage()));

    const QDBusMessage retoggle =
        waitForReply(m_proxy->SetRuleEnabled(doomedId, false));
    QCOMPARE(retoggle.type(), QDBusMessage::ErrorMessage);
    QVERIFY2(retoggle.errorMessage().contains(QStringLiteral("unknownRule")),
             qPrintable(retoggle.errorMessage()));

    recreateDaemon();
    QCOMPARE(listedRuleNames(rulesListing()), QStringList{QStringLiteral("Survivor")});
}

void DaemonDbusTest::setRuleEnabledFlipsWithoutTouchingDefinition() {
    const QString ruleId = addManualRule(QStringLiteral("Toggle me"));
    const QJsonObject original =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();

    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);
    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));
    QCOMPARE(changed.size(), 1);

    // The toggle must not round-trip or mutate the definition itself.
    const QJsonObject flipped =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QJsonObject expected = original;
    expected.insert(QStringLiteral("enabled"), false);
    QCOMPARE(flipped, expected);

    recreateDaemon();
    const QJsonObject reloaded =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QCOMPARE(reloaded, expected);

    const QDBusMessage enabledAgain = waitForReply(m_proxy->SetRuleEnabled(ruleId, true));
    QVERIFY2(enabledAgain.type() != QDBusMessage::ErrorMessage,
             qPrintable(enabledAgain.errorMessage()));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("enabled"))
                 .toBool(),
             true);
}

void DaemonDbusTest::runRuleWithoutConsentingDevicesIsConsentBlocked() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.70")});
    const QDBusMessage denied =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"allowAutomations":false})json")});
    QVERIFY2(denied.type() != QDBusMessage::ErrorMessage, qPrintable(denied.errorMessage()));
    const QString ruleId = addManualRule(QStringLiteral("Quiet hour"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("consent-blocked"));

    // The last-run record lands on the stored rule and is announced.
    QCOMPARE(changed.size(), 1);
    const QJsonObject rule = rulesListing()
                                 .value(QStringLiteral("rules"))
                                 .toArray()
                                 .at(0)
                                 .toObject();
    QCOMPARE(rule.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(rule.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("consent-blocked"));
    QVERIFY2(!rule.value(QStringLiteral("lastRun"))
                  .toObject()
                  .value(QStringLiteral("lastFiredAt"))
                  .toString()
                  .isEmpty(),
             "a recorded run must carry its timestamp");

    // Consent gating happens before anything reaches the device.
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::runRuleDispatchesKeypressThroughTheCommandPipeline() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.71")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Movie time","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));

    // The macro travels the exact SendCommand pipeline: same rds invocation.
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));
    QVERIFY2(m_runner->lastRequest.arguments.contains(QStringLiteral("198.51.100.71")),
             "the macro must target the consenting device's address");
}

void DaemonDbusTest::runRuleReportsUndeliveredCommandsAsFailed() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.71")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Movie time","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    // The TV is off: rds exits non-zero and the run must not claim ok.
    startRunnerCompleter({1});
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("failed"));
}

void DaemonDbusTest::runRuleReportsMixedDeliveryAsPartial() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.71")});
    m_daemon->upsertDevice({QStringLiteral("SERIAL-B"), QStringLiteral("Kitchen TV"),
                            QStringLiteral("198.51.100.72")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Movie time","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    // First target's delivery fails, second succeeds.
    startRunnerCompleter({1, 0});
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("partial"));
}

void DaemonDbusTest::runRuleReportsUnsupportedActionsAsFailed() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.72")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Loud evening","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":20}]})json"));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("failed"));
    // An unsupported action must never reach a device.
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::runRuleWithUnknownIdErrors() {
    const QDBusMessage run = waitForReply(m_proxy->RunRule(QStringLiteral("rNOPE")));
    QCOMPARE(run.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(run.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    QVERIFY2(run.errorMessage().contains(QStringLiteral("unknownRule")),
             qPrintable(run.errorMessage()));
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::runRuleWithMatchingConditionsRunsAndReportsOk() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.33")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("cond-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // Seed the snapshot the gate reads: volume 18 sits under the threshold.
    completePollVersion();
    completePollReadings(18);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Quiet movie","trigger":{"type":"manual"},)json"
        R"json("conditions":{"leaf":{"device":"*","field":"volume","op":"lt","value":20}},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    startRunnerCompleter();  // the run waits for its delivery verdict
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(homeKeypressCount(), 1);
}

void DaemonDbusTest::runRuleWithFailingConditionsRecordsConditionFalse() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.34")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("cond-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    completePollVersion();
    completePollReadings(18);  // volume 18 fails a "louder than 50" gate

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Loud movie","trigger":{"type":"manual"},)json"
        R"json("conditions":{"leaf":{"device":"*","field":"volume","op":"gt","value":50}},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    // Before gating exists this call still dispatches, so keep the delivery
    // pump alive around it either way.
    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("condition-false"));

    // A false tree must stop the pipeline before anything reaches a device,
    // yet still land a last-run record on the announced rule.
    QCOMPARE(homeKeypressCount(), 0);
    const QJsonObject rule =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QCOMPARE(rule.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(rule.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("condition-false"));
    QVERIFY2(!rule.value(QStringLiteral("lastRun"))
                  .toObject()
                  .value(QStringLiteral("lastFiredAt"))
                  .toString()
                  .isEmpty(),
             "a recorded run must carry its timestamp");
}

void DaemonDbusTest::scheduledDailyRuleFiresThroughTheCommandPipeline() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.80")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nightly home","trigger":{"type":"time","kind":"daily","time":"09:00"})json"
        R"json(,"actions":[{"type":"keypress","key":"Home"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);
    const int announcementsBefore = changed.size();

    // A minute early: no firing.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 59));
    m_daemon->evaluateDueRules();
    QVERIFY(m_runner->requests.isEmpty());

    // At the wall-clock time the rule runs through the real pipeline.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 0));
    startRunnerCompleter();
    m_daemon->evaluateDueRules();
    stopRunnerCompleter();
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));
    QCOMPARE(changed.size(), announcementsBefore + 1);
    const QJsonObject stored = rulesListing()
                                   .value(QStringLiteral("rules"))
                                   .toArray()
                                   .at(0)
                                   .toObject();
    QCOMPARE(stored.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(stored.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));

    // The same evaluation instant must not fire the occurrence twice.
    m_daemon->evaluateDueRules();
    QCOMPARE(m_runner->requests.size(), 1);
}

void DaemonDbusTest::disablingARuleUnschedulesItsNextFire() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.81")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nightly home","trigger":{"type":"time","kind":"daily","time":"09:00"})json"
        R"json(,"actions":[{"type":"keypress","key":"Home"}]})json"));
    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));

    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 0));
    m_daemon->evaluateDueRules();
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::onceRuleMissedByLessThanADayFiresAtStartup() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.82")});
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Once fresh","trigger":{"type":"time","kind":"once","time":"08:00")json"
        R"json(,"date":"2026-01-05"},"actions":[{"type":"keypress","key":"Home"}]})json"));
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Once stale","trigger":{"type":"time","kind":"once","time":"08:00")json"
        R"json(,"date":"2026-01-03"},"actions":[{"type":"keypress","key":"Back"}]})json"));

    // Boot 90 minutes after the fresh rule's fire time.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 30));
    startRunnerCompleter();
    recreateDaemon(30s, {}, [this] { return m_now; });
    stopRunnerCompleter();

    // Fresh: caught up exactly once at startup. Stale: skipped forever.
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));

    // Restart again: the persisted lastRun marks the rule consumed.
    recreateDaemon(30s, {}, [this] { return m_now; });
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::intervalRuleReanchorsAcrossDaemonRestarts() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.83")});
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Nudge","trigger":{"type":"interval","minutes":5})json"
        R"json(,"actions":[{"type":"keypress","key":"Home"}]})json"));

    // Restart: the cadence starts over from boot — no burst.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 0));
    recreateDaemon(30s, {}, [this] { return m_now; });
    QVERIFY(m_runner->requests.isEmpty());

    startRunnerCompleter();
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 5));
    m_daemon->evaluateDueRules();
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));

    // The cadence re-anchored to the 08:05 fire instant: due again at exactly
    // 08:10, not five minutes after boot. The first command already finished
    // (the completer drained it), so the second firing dispatches freely.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 10));
    m_daemon->evaluateDueRules();
    stopRunnerCompleter();
    QCOMPARE(m_runner->requests.size(), 2);
}

void DaemonDbusTest::pendingScheduleKeepsTheDaemonAwake() {
    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());  // nothing to keep the daemon alive

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nightly off","trigger":{"type":"time","kind":"daily","time":"23:00"})json"
        R"json(,"actions":[{"type":"keypress","key":"Power"}]})json"));
    QVERIFY(!m_daemon->idleTimerActive());  // the pending schedule blocks idle exit

    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));
    QVERIFY(m_daemon->idleTimerActive());   // released
}

void DaemonDbusTest::stateChangeRuleFiresOnRisingEdgeOnly() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.30")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("sc-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);

    // First full poll: volume 25 sits above the rule's threshold, making it
    // the observation baseline outside the predicate; nothing may fire.
    completePollVersion();
    completePollReadings(25);
    QCOMPARE(snapshots.size(), 1);
    QCOMPARE(homeKeypressCount(), 0);

    // The rule joins after the baseline: dropping below 20 must fire it.
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Volume watchdog","trigger":{"type":"stateChange","field":"volume",)json"
        R"json("op":"lt","value":20},"actions":[{"type":"keypress","key":"Home"}]})json"));
    startRunnerCompleter();  // the firing waits for its delivery verdict
    completePollReadings(5);
    stopRunnerCompleter();
    QCOMPARE(homeKeypressCount(), 1);
    const QJsonObject fired =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QCOMPARE(fired.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(fired.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));

    // Same reading again: old == new never re-fires.
    completePollReadings(5);
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 1);

    // Back above the threshold: the falling edge must stay silent too.
    completePollReadings(25);
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 1);
    QCOMPARE(snapshots.size(), 4);
}

void DaemonDbusTest::firstObservationIsBaselineAndNeverFires() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.31")});
    // The rule exists before the daemon ever observes the device: even a first
    // reading that already matches stays silent (spec §4.1 baseline).
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Quiet watcher","trigger":{"type":"stateChange","field":"volume",)json"
        R"json("op":"lt","value":20},"actions":[{"type":"keypress","key":"Home"}]})json"));
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("baseline-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    auto neverRan = [this, &ruleId]() {
        const QJsonObject rule =
            rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
        QCOMPARE(rule.value(QStringLiteral("id")).toString(), ruleId);
        QVERIFY2(!rule.contains(QStringLiteral("lastRun")),
                 "a rule that never fired must carry no last-run record");
    };

    completePollVersion();
    completePollReadings(5);  // matches the predicate, but it is only the baseline
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 0);
    neverRan();

    completePollReadings(5);  // stable readings never fire
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 0);
    neverRan();

    completePollReadings(25);  // leaving the predicate is the falling edge: silent
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 0);

    startRunnerCompleter();  // re-entering below 20 is a genuine rising edge
    completePollReadings(5);
    stopRunnerCompleter();
    QCOMPARE(homeKeypressCount(), 1);
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
}

void DaemonDbusTest::muteActionPressesVolumeMuteOnce() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.36")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // Seed the snapshot the mute acts on: audible and unmuted.
    completePollVersion();
    completePollReadings(18);
    QCOMPARE(keypressCount(QStringLiteral("VolumeMute")), 0);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Silence please","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));

    startRunnerCompleter();  // the run waits for its delivery verdict
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));

    // One toggle press beyond the polling traffic, through the same pipeline.
    QCOMPARE(keypressCount(QStringLiteral("VolumeMute")), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("VolumeMute")}));
}

void DaemonDbusTest::muteActionMatchingObservedStateIsANoOp() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.37")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    completePollVersion();
    completePollReadings(18, true);  // already muted

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Stay quiet","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));

    // The observed state already matches: a successful no-op, zero presses.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeMute")), 0);
}

void DaemonDbusTest::setVolumeWalksDownWithBoundedPresses() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.38")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // Volume 18 down to 15 is a delta of -3: exactly three VolumeDown walks.
    completePollVersion();
    completePollReadings(18);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Softly","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":15}]})json"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 0);

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 3);
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 0);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("VolumeDown")}));
}

void DaemonDbusTest::volumeActionsWithoutObservedAudioFailWithoutDispatching() {
    // Registered but never polled: no audio reading was ever observed.
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.90")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Blind tuning","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"setVolume","volume":15},{"type":"mute","muted":true}]})json"));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));
    // Steering blind is forbidden: nothing may reach the device.
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::mixedAudioStateAcrossTargetsYieldsPartial() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.39")});
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-B"), QStringLiteral("Kitchen TV"), QStringLiteral("192.0.2.40")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // Only SERIAL-A ever reports audio; SERIAL-B stays unobserved.
    completePollVersion();
    completePollReadings(30);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Whole-house mute","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));

    // SERIAL-A toggles once; SERIAL-B's rejection makes the run partial.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("partial"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeMute")), 1);
}

void DaemonDbusTest::notYetExecutableActionsAbortTheWholeRunWithoutDispatching() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.91")});

    // A not-yet-executable step poisons its whole list: even the executable
    // Home ahead of it must stay put until runRule/ifThenElse land.
    const QString mixedId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Mixed macro","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"keypress","key":"Home"},)json"
        R"json({"type":"ifThenElse","cond":{"leaf":{"device":"*","field":"power",)json"
        R"json("op":"is","value":"on"}},"then":[{"type":"keypress","key":"Back"}]}]})json"));
    const QString macroId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Bare macro","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"runRule","id":"rCHILD"}]})json"));

    startRunnerCompleter();  // keep any stray delivery wait short either way
    const QDBusMessage mixedRun = waitForReply(m_proxy->RunRule(mixedId));
    const QDBusMessage macroRun = waitForReply(m_proxy->RunRule(macroId));
    stopRunnerCompleter();
    QVERIFY2(mixedRun.type() != QDBusMessage::ErrorMessage,
             qPrintable(mixedRun.errorMessage()));
    QVERIFY2(macroRun.type() != QDBusMessage::ErrorMessage, qPrintable(macroRun.errorMessage()));
    QCOMPARE(mixedRun.arguments().front().toString(), QStringLiteral("failed"));
    QCOMPARE(macroRun.arguments().front().toString(), QStringLiteral("failed"));

    // The deliberate abort guard: nothing reaches a device, not even the
    // executable steps sharing the list (ifThenElse lands in Task 6).
    QVERIFY(m_runner->requests.isEmpty());
    QCOMPARE(homeKeypressCount(), 0);
}

void DaemonDbusTest::macroChainRunsChildAndReportsOk() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.94")});
    // The child is added first so its generated id can seed the parent's
    // macro target without a rewrite.
    const QString childId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Child step","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    const QString parentId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Movie night","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"keypress","key":"Home"},)json"
        R"json({"type":"runRule","id":"%1"}]})json").arg(childId));

    startRunnerCompleter();  // the nested run waits for its delivery verdict
    const QDBusMessage run = waitForReply(m_proxy->RunRule(parentId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));

    // The parent's own Home plus the child's: two dispatches, one pipeline.
    QCOMPARE(homeKeypressCount(), 2);
    QCOMPARE(m_runner->requests.size(), 2);

    // Each run records its own outcome on its own rule.
    const QJsonArray rules = rulesListing().value(QStringLiteral("rules")).toArray();
    QCOMPARE(rules.at(0).toObject().value(QStringLiteral("id")).toString(), childId);
    QCOMPARE(rules.at(0).toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
    QCOMPARE(rules.at(1).toObject().value(QStringLiteral("id")).toString(), parentId);
    QCOMPARE(rules.at(1).toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
}

void DaemonDbusTest::selfReferencingMacroReportsCycleError() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.95")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Bootstrap","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    // A self-targeting macro is only expressible after creation: replace the
    // whole definition with one that names its own id.
    const QString updated = QStringLiteral(
        R"json({"id":"%1","name":"Ouroboros","enabled":true,"mode":"restart",)json"
        R"json("devices":[],"trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"runRule","id":"%1"}]})json").arg(ruleId);
    const QDBusMessage rewritten = waitForReply(m_proxy->UpdateRule(updated));
    QVERIFY2(rewritten.type() != QDBusMessage::ErrorMessage,
             qPrintable(rewritten.errorMessage()));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // The cycle is cut and reported honestly instead of looping forever.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("cycle-error"));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("cycle-error"));

    // Cutting the branch means nothing ever reaches a device.
    QVERIFY(m_runner->requests.isEmpty());
    QCOMPARE(homeKeypressCount(), 0);
}

void DaemonDbusTest::missingMacroTargetCountsAsFailure() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.96")});
    const QString parentId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Dangling macro","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"runRule","id":"rNOPE"}]})json"));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(parentId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // One attempted macro step, one rejection: the run cannot claim ok.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("failed"));
    // A dangling target must not reach any device.
    QVERIFY(m_runner->requests.isEmpty());
}

QTEST_GUILESS_MAIN(DaemonDbusTest)
#include "daemon_dbus_test.moc"

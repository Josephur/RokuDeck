// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_contract.h"
#include "core/process_runner.h"
#include "daemon/config_store.h"
#include "daemon/daemon_service.h"
#include "rokudeck1_interface.h"
#include "support/fake_process_runner.h"

#include <KSharedConfig>

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
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
#include <algorithm>
#include <memory>

using namespace RokuDeck;
using namespace RokuDeck::Test;
using namespace std::chrono_literals;

namespace {
// Defined with the other fixture helpers below; the bootstrap tests precede
// them and look This PC up by serial.
[[nodiscard]] QJsonObject deviceEntry(const QJsonObject& listing, const QString& serial);
} // namespace

class FakeMprisPlayer;

// The Player-interface adaptor that exposes PlaybackStatus to
// org.freedesktop.DBus.Properties readers.
class FakeMprisAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)

  public:
    explicit FakeMprisAdaptor(FakeMprisPlayer* player);
    [[nodiscard]] QString playbackStatus() const;

  private:
    FakeMprisPlayer* m_player;
};

class FakeMprisPlayer final : public QObject, protected QDBusContext {
    Q_OBJECT

  public:
    explicit FakeMprisPlayer(QObject* parent = nullptr);
    ~FakeMprisPlayer() override;

    QString status = QStringLiteral("Stopped");

    [[nodiscard]] bool registerOnBus(const QString& serviceName) {
        m_serviceName = serviceName;
        const bool objectRegistered =
            m_connection.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this);
        return objectRegistered && m_connection.registerService(serviceName);
    }

    void leaveBus() {
        if (!m_serviceName.isEmpty()) {
            static_cast<void>(m_connection.unregisterService(m_serviceName));
            m_serviceName.clear();
        }
    }

    void setStatus(const QString& newStatus) {
        status = newStatus;
        QDBusMessage signal = QDBusMessage::createSignal(
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        signal.setArguments({QStringLiteral("org.mpris.MediaPlayer2.Player"),
                             QVariantMap{{QStringLiteral("PlaybackStatus"), newStatus}},
                             QStringList{}});
        static_cast<void>(m_connection.send(signal));
    }

  private:
    QString m_connectionName;
    QDBusConnection m_connection;
    QString m_serviceName;
};

FakeMprisAdaptor::FakeMprisAdaptor(FakeMprisPlayer* player)
    : QDBusAbstractAdaptor(player), m_player(player) {}

QString FakeMprisAdaptor::playbackStatus() const {
    return m_player->status;
}

FakeMprisPlayer::FakeMprisPlayer(QObject* parent)
    : QObject(parent),
      m_connectionName(QStringLiteral("rokudeck-mpris-test-")
                       + QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_connection(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName)) {
    new FakeMprisAdaptor(this);
}

FakeMprisPlayer::~FakeMprisPlayer() {
    m_connection.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
    leaveBus();
    QDBusConnection::disconnectFromBus(m_connectionName);
}

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
    void daemonBootstrapsThisPcAndShieldsIt();
    void localPcRulesKeepTheDaemonAwake();
    void wildcardRulesDoNotKeepTheDaemonAwakeForTheLocalPc();
    void wildcardRulesDoNotTargetTheLocalPc();
    void wildcardStateChangeRuleIgnoresLocalPcEdges();
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
    void volumeCeilingPersistsAndEchoesThroughDeviceJson();
    void volumeCeilingRejectsOutOfRangeAndNonIntegralValues();
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
    void rokuSnapshotsReportPlayingAsNull();
    void pcPollerTracksPipeWireVolume();
    void pcVolumeEdgeFiresStateChangeRule();
    void pcPollerGoesOfflineAfterThreeFailuresAndRecovers();
    void mprisWatcherMergesPlaybackIntoPcSnapshots();
    void playingEdgeFiresStateChangeRuleOnLocalPc();
    void pcSetVolumeDispatchesExactWpctlShapeAndClampsToCeiling();
    void pcMuteDispatchesToggleShapesAndNoOpsWhenEqual();
    void keypressOnLocalPcIsRejectedWithoutTraffic();
    void muteActionPressesVolumeMuteOnce();
    void muteActionMatchingObservedStateIsANoOp();
    void setVolumeWalksDownWithBoundedPresses();
    void volumeActionsWithoutObservedAudioFailWithoutDispatching();
    void mixedAudioStateAcrossTargetsYieldsPartial();
    void ceilingClampsAnUpwardVolumeWalk();
    void volumeWalkBeyondQueueCapDeliversEveryPress();
    void volumeWalkWithBusyQueueDeliversEveryPress();
    void downwardWalksIgnoreTheCeiling();
    void setVolumeAtTheCeilingIsACleanNoOp();
    void mixedCeilingsKeepPartialOutcomesTruthful();
    void mixedListWithIfThenElseRunsToCompletion();
    void macroChainRunsChildAndReportsOk();
    void selfReferencingMacroReportsCycleError();
    void missingMacroTargetCountsAsFailure();
    void disabledMacroChildIsRejectedWithoutRunning();
    void condTrueRunsThenBranch();
    void condFalseRunsElseBranch();
    void falseCondWithoutElseIsCleanNoOp();
    void ifThenElseBranchRecursionCarriesCycleContext();
    void singleModeDropsOverlappingFiring();
    void restartModeSupersedesActiveRun();
    void queuedModeCoalescesThenDrains();
    void suppressedFiringAnnouncesDroppedRecord();
    void macroInvocationBypassesModeGate();
    void stateChangeFiringGatedByConditionsRecordsConditionFalse();
    void restartThenThirdFiringDuringReplacementIsGated();
    void singleModeThirdFiringDuringActiveRunIsDropped();
    void runRuleOnDisabledRuleIsTypedRejected();
    void stateChangeEdgeOnDisabledRuleDoesNotFire();
    void windowedRuleFiresInsideAndRecordsInactiveOutside();
    void windowBoundariesAreInclusiveStartExclusiveEnd();
    void scheduledWindowedRuleRecordsInactiveWindow();
    void runScriptHappyRunIsDeliveredAsOk();
    void runScriptNonZeroExitFailsTheRun();
    void runScriptRejectsBadFilesWithoutTraffic();
    void runScriptAcceptsSymlinkedScriptsDirectory();
    void addRuleRejectsInvalidScriptNames();

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
                        DaemonService::WallClock wallClock = {},
                        std::chrono::milliseconds localPcPollInterval = 1h,
                        const QString& scriptsDir = {});
    QString addManualRule(const QString& name);
    QString addRuleReturningId(const QString& payload);
    QJsonObject rulesListing() const;
    // The last-run outcome of the sole rule in every RulesChanged announcement
    // a spy captured, in order — the only way to observe an intermediate
    // record (a drop) that a later run overwrites in storage.
    QStringList announcedLastRunOutcomes(const QSignalSpy& spy) const;
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
    // True once any absolute local-audio control invocation (set-volume /
    // set-mute) reached the fake runner.
    [[nodiscard]] bool hasPcControlRequest(const QString& subcommand) const;
    // Serves the one-time rds --version probe that precedes the very first
    // device query of a daemon's lifetime.
    void completePollVersion();
    // Waits for the next device-info request, then serves it plus the
    // active-app/player/audio-device readings. Only one poll can be in flight
    // per serial, so this consumes exactly one cycle no matter how eagerly the
    // scheduler re-arms. A firing triggered by the audio response dispatches
    // synchronously inside this call.
    void completePollReadings(int volume, bool muted = false);
    // Waits for the next wpctl get-volume request from the local-PC poller and
    // completes it with the given raw stdout/exit code. Pins the invocation
    // shape on the way through.
    void completePcVolumePoll(const QByteArray& output, int exitCode = 0);

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
                                    DaemonService::WallClock wallClock,
                                    std::chrono::milliseconds localPcPollInterval,
                                    const QString& scriptsDir) {
    m_proxy.reset();
    m_daemon.reset();
    auto runner = std::make_unique<FakeProcessRunner>();
    m_runner = runner.get();
    auto config = KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig);
    auto configStore = std::make_unique<ConfigStore>(std::move(config));
    m_daemon = std::make_unique<DaemonService>(
        QDBusConnection::sessionBus(), [this] { return m_automationEnabled; }, std::move(runner),
        std::move(configStore), idleGrace, std::move(deadlineRemaining), std::move(wallClock),
        localPcPollInterval, scriptsDir);
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

void DaemonDbusTest::daemonBootstrapsThisPcAndShieldsIt() {
    // A fresh temp-config daemon owns This PC out of the box (spec §1).
    const QJsonObject record =
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("LOCAL-PC"));
    QCOMPARE(record.value(QStringLiteral("displayName")).toString(), QStringLiteral("This PC"));
    QVERIFY(record.value(QStringLiteral("allowAutomations")).toBool());

    // Undeletable: the machine the daemon runs on cannot be removed.
    const QDBusMessage removed = call(QStringLiteral("RemoveDevice"), {QStringLiteral("LOCAL-PC")});
    QCOMPARE(removed.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(removed.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    QVERIFY2(removed.errorMessage().contains(QStringLiteral("the local device cannot be removed")),
             qPrintable(removed.errorMessage()));

    // Consent is always-on for the local machine...
    const QDBusMessage consentOff =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("LOCAL-PC"), QStringLiteral(R"json({"allowAutomations":false})json")});
    QCOMPARE(consentOff.type(), QDBusMessage::ErrorMessage);
    QVERIFY2(
        consentOff.errorMessage().contains(
            QStringLiteral("the local device always allows automations")),
        qPrintable(consentOff.errorMessage()));

    // ...but the display name stays user-renameable, and everything persists.
    const QDBusMessage renamed =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("LOCAL-PC"), QStringLiteral(R"json({"displayName":"Dev Box"})json")});
    QVERIFY2(renamed.type() != QDBusMessage::ErrorMessage, qPrintable(renamed.errorMessage()));
    recreateDaemon();
    const QJsonObject persisted =
        deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("LOCAL-PC"));
    QCOMPARE(persisted.value(QStringLiteral("displayName")).toString(), QStringLiteral("Dev Box"));
    QVERIFY(persisted.value(QStringLiteral("allowAutomations")).toBool());
}

void DaemonDbusTest::localPcRulesKeepTheDaemonAwake() {
    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());  // owning This PC alone pins nothing

    const QString explicitId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Mute when playing","devices":["LOCAL-PC"],)json"
        R"json("trigger":{"type":"stateChange","field":"playing","op":"is","value":true},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));
    QVERIFY(!m_daemon->idleTimerActive());  // an enabled LOCAL-PC rule blocks idle exit

    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(explicitId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));
    QVERIFY(m_daemon->idleTimerActive());  // disabling releases the daemon again

    const QDBusMessage enabled = waitForReply(m_proxy->SetRuleEnabled(explicitId, true));
    QVERIFY2(enabled.type() != QDBusMessage::ErrorMessage, qPrintable(enabled.errorMessage()));
    QVERIFY(!m_daemon->idleTimerActive());
}

void DaemonDbusTest::wildcardRulesDoNotKeepTheDaemonAwakeForTheLocalPc() {
    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());

    // Wildcards mean "my TVs": an enabled wildcard rule does not reach This
    // PC (only explicit listing does), so it cannot hold the daemon awake.
    // The manual trigger keeps the schedule timer out of the picture.
    addManualRule(QStringLiteral("Wildcard macro"));
    QVERIFY(m_daemon->idleTimerActive());
}

void DaemonDbusTest::wildcardRulesDoNotTargetTheLocalPc() {
    recreateDaemon(30s, {}, {}, 30ms);
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.61")});
    // A healthy This PC reading exists, yet wildcards must not resolve it.
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.30\n"));
    const int pcControlBefore = 0;

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"TVs only","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(homeKeypressCount(), 1);  // the keypress landed on the TV alone

    int pcControls = pcControlBefore;
    for (const ProcessRequest& request : m_runner->requests) {
        if (request.program == QLatin1String("wpctl")
            && !request.arguments.isEmpty()
            && (request.arguments.first() == QLatin1String("set-volume")
                || request.arguments.first() == QLatin1String("set-mute"))) {
            ++pcControls;
        }
    }
    QCOMPARE(pcControls, pcControlBefore);  // zero wpctl traffic: PC untouched
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
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(1).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(devices.at(1).toObject().value(QStringLiteral("address")).toString(),
             QStringLiteral("192.0.2.11"));

    ConfigStore reloaded(KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig));
    const QList<DeviceRecord> stored = reloaded.loadDevices();
    QCOMPARE(stored.size(), 2);
    QVERIFY(std::any_of(stored.cbegin(), stored.cend(),
                        [](const DeviceRecord& device) {
                            return device.serial == QLatin1String("SERIAL-A")
                                && device.address == QLatin1String("192.0.2.11");
                        }));
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
                 .at(1)
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
    QCOMPARE(completedDevices.size(), 3);
    QCOMPARE(completedDevices.at(1).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(changed.size(), 1);
    QJsonArray devices =
        jsonResult(waitForReply(m_proxy->ListDevices())).value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 3);

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
                 .at(1)
                 .toObject()
                 .value(QStringLiteral("address"))
                 .toString(),
             QStringLiteral("198.51.100.42"));
    QCOMPARE(changed.size(), 2);
    devices =
        jsonResult(waitForReply(m_proxy->ListDevices())).value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 3);
    QCOMPARE(devices.at(1).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(devices.at(1).toObject().value(QStringLiteral("address")).toString(),
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
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList{QStringLiteral("LOCAL-PC")});

    const QDBusMessage removedAgain =
        call(QStringLiteral("RemoveDevice"), {QStringLiteral("SERIAL-A")});
    QCOMPARE(removedAgain.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(removedAgain.errorName(),
             QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownDevice"));

    recreateDaemon();
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList{QStringLiteral("LOCAL-PC")});
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

    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList{QStringLiteral("LOCAL-PC")});

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
             QStringList({QStringLiteral("LOCAL-PC"), QStringLiteral("SERIAL-A"),
                          QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-C")}));

    QSignalSpy changed(m_daemon.get(), &DaemonService::DeviceListChanged);
    const QDBusMessage reordered =
        call(QStringLiteral("SetDeviceOrder"),
             {QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-A"),
                           QStringLiteral("SERIAL-B")})});
    QVERIFY2(reordered.type() != QDBusMessage::ErrorMessage, qPrintable(reordered.errorMessage()));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-A"),
                          QStringLiteral("SERIAL-B"), QStringLiteral("LOCAL-PC")}));

    const QDBusMessage unknown =
        call(QStringLiteral("SetDeviceOrder"),
             {QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-MISSING")})});
    QCOMPARE(unknown.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-A"),
                          QStringLiteral("SERIAL-B"), QStringLiteral("LOCAL-PC")}));

    const QDBusMessage duplicated =
        call(QStringLiteral("SetDeviceOrder"),
             {QStringList({QStringLiteral("SERIAL-C"), QStringLiteral("SERIAL-C")})});
    QCOMPARE(duplicated.type(), QDBusMessage::ErrorMessage);

    const QDBusMessage partial =
        call(QStringLiteral("SetDeviceOrder"), {QStringList{QStringLiteral("SERIAL-B")}});
    QVERIFY2(partial.type() != QDBusMessage::ErrorMessage, qPrintable(partial.errorMessage()));
    QVERIFY2(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))) ==
                 QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-C"),
                              QStringLiteral("SERIAL-A"), QStringLiteral("LOCAL-PC")}),
             "devices the caller omitted must keep their relative order behind the named ones");

    recreateDaemon();
    QCOMPARE(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))),
             QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-C"),
                          QStringLiteral("SERIAL-A"), QStringLiteral("LOCAL-PC")}));
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
                 QStringList({QStringLiteral("SERIAL-B"), QStringLiteral("SERIAL-A"),
                              QStringLiteral("LOCAL-PC")}),
             "discovery must not reshuffle the order the user chose");
}

void DaemonDbusTest::volumeCeilingPersistsAndEchoesThroughDeviceJson() {
    const QDBusMessage added =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(added.type() != QDBusMessage::ErrorMessage, qPrintable(added.errorMessage()));
    // A fresh device is unrestricted: the ceiling echoes as 100.
    QCOMPARE(jsonResult(added).value(QStringLiteral("volumeCeiling")).toInt(), 100);
    QCOMPARE(deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
                 .value(QStringLiteral("volumeCeiling"))
                 .toInt(),
             100);

    const QDBusMessage updated =
        call(QStringLiteral("UpdateDevice"),
             {QStringLiteral("SERIAL-A"), QStringLiteral(R"json({"volumeCeiling":50})json")});
    QVERIFY2(updated.type() != QDBusMessage::ErrorMessage, qPrintable(updated.errorMessage()));
    QCOMPARE(jsonResult(updated).value(QStringLiteral("volumeCeiling")).toInt(), 50);

    recreateDaemon();
    QCOMPARE(deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
                 .value(QStringLiteral("volumeCeiling"))
                 .toInt(),
             50);
}

void DaemonDbusTest::volumeCeilingRejectsOutOfRangeAndNonIntegralValues() {
    const QDBusMessage added =
        call(QStringLiteral("AddDevice"),
             {deviceJson(QStringLiteral("SERIAL-A"), QStringLiteral("Living Room"),
                         QStringLiteral("198.51.100.10"))});
    QVERIFY2(added.type() != QDBusMessage::ErrorMessage, qPrintable(added.errorMessage()));

    const QStringList rejected{
        QStringLiteral(R"json({"volumeCeiling":101})json"),
        QStringLiteral(R"json({"volumeCeiling":-1})json"),
        QStringLiteral(R"json({"volumeCeiling":"abc"})json"),
        QStringLiteral(R"json({"volumeCeiling":50.5})json")};
    for (const QString& payload : rejected) {
        const QDBusMessage reply =
            call(QStringLiteral("UpdateDevice"), {QStringLiteral("SERIAL-A"), payload});
        QVERIFY2(reply.type() == QDBusMessage::ErrorMessage,
                 qPrintable(QStringLiteral("UpdateDevice accepted %1").arg(payload)));
        QCOMPARE(reply.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    }
    // Every rejected edit leaves the stored ceiling untouched.
    QCOMPARE(deviceEntry(jsonResult(waitForReply(m_proxy->ListDevices())), QStringLiteral("SERIAL-A"))
                 .value(QStringLiteral("volumeCeiling"))
                 .toInt(),
             100);
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

    QVERIFY2(listedSerials(jsonResult(waitForReply(m_proxy->ListDevices()))) ==
                 QStringList{QStringLiteral("LOCAL-PC")},
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
                ProcessResult result;
                // Background local-audio ticks must stay healthy no matter who
                // completes them: a consumed get-volume answers with a stable
                // reading instead of junk (which would read as poll failures).
                const qsizetype index =
                    m_runner->requests.size() - m_runner->callbacks.size();
                const ProcessRequest& pending = m_runner->requests.at(index);
                if (pending.program == QLatin1String("wpctl")
                    && !pending.arguments.isEmpty()
                    && pending.arguments.first() == QLatin1String("get-volume")) {
                    result.exitCode = 0;
                    result.standardOutput = QByteArrayLiteral("Volume: 0.30\n");
                } else {
                    const int code =
                        m_completerScript.isEmpty() ? 0 : m_completerScript.takeFirst();
                    result.exitCode = code;
                    result.standardOutput = code == 0
                        ? QByteArrayLiteral("{\"ok\":true}\n")
                        : QByteArrayLiteral(
                              "{\"success\":false,\"error\":\"Request timed out\"}\n");
                }
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

bool DaemonDbusTest::hasPcControlRequest(const QString& subcommand) const {
    for (const ProcessRequest& request : m_runner->requests) {
        if (request.program == QLatin1String("wpctl") && !request.arguments.isEmpty()
            && request.arguments.first() == subcommand) {
            return true;
        }
    }
    return false;
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

void DaemonDbusTest::completePcVolumePoll(const QByteArray& output, int exitCode) {
    QVERIFY2(QTest::qWaitFor([&] { return m_runner != nullptr && !m_runner->callbacks.isEmpty(); },
                             15s),
             "the wpctl get-volume request never reached the fake runner");
    QCOMPARE(m_runner->lastRequest.program, QStringLiteral("wpctl"));
    QCOMPARE(m_runner->lastRequest.arguments,
             QStringList({QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")}));
    m_runner->complete({exitCode, output, {}, false});
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
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Quiet hour","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Select"}]})json"));
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
        R"json({"name":"Movie time","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
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
        R"json({"name":"Movie time","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
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
        R"json({"name":"Movie time","devices":["SERIAL-A","SERIAL-B"],"trigger":{"type":"manual"},)json"
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
        R"json({"name":"Quiet movie","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
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
        R"json({"name":"Nightly home","devices":["SERIAL-A"],"trigger":{"type":"time","kind":"daily","time":"09:00"})json"
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
        R"json({"name":"Nightly home","devices":["SERIAL-A"],"trigger":{"type":"time","kind":"daily","time":"09:00"})json"
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
        R"json({"name":"Once fresh","devices":["SERIAL-A"],"trigger":{"type":"time","kind":"once","time":"08:00")json"
        R"json(,"date":"2026-01-05"},"actions":[{"type":"keypress","key":"Home"}]})json"));
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Once stale","devices":["SERIAL-A"],"trigger":{"type":"time","kind":"once","time":"08:00")json"
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
        R"json({"name":"Nudge","devices":["SERIAL-A"],"trigger":{"type":"interval","minutes":5})json"
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
        R"json({"name":"Volume watchdog","devices":["SERIAL-A"],"trigger":{"type":"stateChange","field":"volume",)json"
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
        R"json({"name":"Quiet watcher","devices":["SERIAL-A"],"trigger":{"type":"stateChange","field":"volume",)json"
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

void DaemonDbusTest::rokuSnapshotsReportPlayingAsNull() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.44")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("playing-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    completePollVersion();
    completePollReadings(18);
    const QJsonObject snapshot =
        jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("SERIAL-A")}));
    QCOMPARE(snapshot.value(QStringLiteral("volume")).toInt(), 18);
    // Roku snapshots never carry a playing reading: the key must exist as an
    // explicit null so clients can distinguish "no reading" from absence.
    QVERIFY2(snapshot.contains(QStringLiteral("playing")),
             "snapshot JSON omits the playing key entirely");
    QVERIFY(snapshot.value(QStringLiteral("playing")).isNull());
}

void DaemonDbusTest::pcPollerTracksPipeWireVolume() {
    recreateDaemon(30s, {}, {}, 30ms);
    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);
    // Before the first reading the device answers from the registry only.
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}))
                 .value(QStringLiteral("health"))
                 .toString(),
             QStringLiteral("unknown"));

    // First reading establishes the baseline: audible at 18%.
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.18\n"));
    QCOMPARE(snapshots.size(), 1);
    const QJsonObject first =
        QJsonDocument::fromJson(snapshots.at(0).at(1).toString().toUtf8()).object();
    QCOMPARE(first.value(QStringLiteral("volume")).toInt(), 18);
    QCOMPARE(first.value(QStringLiteral("muted")).toBool(), false);
    QCOMPARE(first.value(QStringLiteral("power")).toString(), QStringLiteral("on"));
    QCOMPARE(first.value(QStringLiteral("health")).toString(), QStringLiteral("online"));

    // A muted reading flows through the same pipeline: 5% and MUTED.
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.05 [MUTED]\n"));
    QCOMPARE(snapshots.size(), 2);
    const QJsonObject mutedSnapshot =
        QJsonDocument::fromJson(snapshots.at(1).at(1).toString().toUtf8()).object();
    QCOMPARE(mutedSnapshot.value(QStringLiteral("volume")).toInt(), 5);
    QCOMPARE(mutedSnapshot.value(QStringLiteral("muted")).toBool(), true);
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}))
                 .value(QStringLiteral("muted"))
                 .toBool(),
             true);
}

void DaemonDbusTest::pcVolumeEdgeFiresStateChangeRule() {
    recreateDaemon(30s, {}, {}, 30ms);
    // The action is absolute mute control: keypresses cannot target This PC.
    // The PC starts muted, so the firing is a successful no-op — provable
    // without any delivery bookkeeping.
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Night wind-down","devices":["LOCAL-PC"],)json"
        R"json("trigger":{"type":"stateChange","field":"volume","op":"lt","value":20},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));

    completePcVolumePoll(QByteArrayLiteral("Volume: 0.25\n"));  // baseline above threshold
    QTest::qWait(50);
    QVERIFY2(!rulesListing()
                  .value(QStringLiteral("rules"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .contains(QStringLiteral("lastRun")),
             "the baseline observation must stay silent");

    // Crossing below 20 fires synchronously inside this tick.
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.05 [MUTED]\n"));
    const QJsonObject fired =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QCOMPARE(fired.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
}

void DaemonDbusTest::pcPollerGoesOfflineAfterThreeFailuresAndRecovers() {
    recreateDaemon(30s, {}, {}, 30ms);
    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);

    completePcVolumePoll(QByteArrayLiteral("Volume: 0.40\n"));
    QCOMPARE(snapshots.size(), 1);  // baseline online

    // Two failures are tolerated: the last good reading stays on display.
    completePcVolumePoll({}, -1);
    completePcVolumePoll({}, -1);
    QCOMPARE(snapshots.size(), 1);
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}))
                 .value(QStringLiteral("health"))
                 .toString(),
             QStringLiteral("online"));

    // The third consecutive failure flips health to Offline until a success.
    completePcVolumePoll({}, -1);
    QCOMPARE(snapshots.size(), 2);
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}))
                 .value(QStringLiteral("health"))
                 .toString(),
             QStringLiteral("offline"));

    // One successful reading restores Online with a fresh volume.
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.58\n"));
    QCOMPARE(snapshots.size(), 3);
    const QJsonObject recovered =
        jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}));
    QCOMPARE(recovered.value(QStringLiteral("health")).toString(), QStringLiteral("online"));
    QCOMPARE(recovered.value(QStringLiteral("volume")).toInt(), 58);
}


void DaemonDbusTest::mprisWatcherMergesPlaybackIntoPcSnapshots() {
    recreateDaemon(30s, {}, {}, 30ms);
    FakeMprisPlayer player;
    QVERIFY(player.registerOnBus(QStringLiteral("org.mpris.MediaPlayer2.FakeTest")));
    player.setStatus(QStringLiteral("Playing"));

    // Poll until the watcher's initial Get lands and a tick merges the flag.
    bool sawPlaying = false;
    for (int attempts = 0; attempts < 100 && !sawPlaying; ++attempts) {
        completePcVolumePoll(QByteArrayLiteral("Volume: 0.30\n"));
        sawPlaying = jsonResult(call(QStringLiteral("GetSnapshot"),
                                     {QStringLiteral("LOCAL-PC")}))
                         .value(QStringLiteral("playing"))
                         .toBool();
    }
    QVERIFY(sawPlaying);

    // A PropertiesChanged flip to Paused must be reflected on the next tick.
    player.setStatus(QStringLiteral("Paused"));
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.30\n"));
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}))
                 .value(QStringLiteral("playing"))
                 .toBool(),
             false);

    // Vanishing players recompute too: nobody is playing any more.
    player.leaveBus();
    QTest::qWait(50);  // let the unregistration reach the daemon's watcher
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.30\n"));
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("LOCAL-PC")}))
                 .value(QStringLiteral("playing"))
                 .toBool(),
             false);
}

void DaemonDbusTest::playingEdgeFiresStateChangeRuleOnLocalPc() {
    recreateDaemon(30s, {}, {}, 30ms);
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Pause the tv","devices":["LOCAL-PC"],)json"
        R"json("trigger":{"type":"stateChange","field":"playing","op":"is","value":true},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));
    // Nobody is playing yet: the baseline snapshot carries playing=false, and
    // the already-muted PC makes the authored action a clean no-op when the
    // edge eventually fires.
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.40 [MUTED]\n"));
    QTest::qWait(50);
    QVERIFY2(!rulesListing()
                  .value(QStringLiteral("rules"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .contains(QStringLiteral("lastRun")),
             "nothing may fire before playback starts");

    FakeMprisPlayer player;
    QVERIFY(player.registerOnBus(QStringLiteral("org.mpris.MediaPlayer2.FakeTest")));
    player.setStatus(QStringLiteral("Playing"));

    // Manual ticks are fully serialized: once the watcher's initial Get
    // lands, the next snapshot flips playing and fires synchronously.
    bool fired = false;
    for (int attempts = 0; attempts < 100 && !fired; ++attempts) {
        completePcVolumePoll(QByteArrayLiteral("Volume: 0.40 [MUTED]\n"));
        fired = rulesListing()
                    .value(QStringLiteral("rules"))
                    .toArray()
                    .at(0)
                    .toObject()
                    .contains(QStringLiteral("lastRun"));
    }
    QVERIFY(fired);
    const QJsonObject rule =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QCOMPARE(rule.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(rule.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
}

void DaemonDbusTest::pcSetVolumeDispatchesExactWpctlShapeAndClampsToCeiling() {
    recreateDaemon(30s, {}, {}, 30ms);
    const QDBusMessage ceiling =
        waitForReply(m_proxy->UpdateDevice(
            QStringLiteral("LOCAL-PC"), QStringLiteral(R"json({"volumeCeiling":50})json")));
    QVERIFY2(ceiling.type() != QDBusMessage::ErrorMessage, qPrintable(ceiling.errorMessage()));
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.30\n"));  // observed 30, unmuted

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Half volume","devices":["LOCAL-PC"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":80}]})json"));
    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));

    // The authored 80 clamps to the 50 ceiling and dispatches as one absolute
    // wpctl call — no nudge walk.
    // Only control invocations count here; the background get-volume poller
    // shares the wpctl program name.
    const auto controlRequests = [this] {
        QList<ProcessRequest> out;
        for (const ProcessRequest& request : m_runner->requests) {
            if (request.program == QStringLiteral("wpctl") && !request.arguments.isEmpty()
                && (request.arguments.first() == QStringLiteral("set-volume")
                    || request.arguments.first() == QStringLiteral("set-mute"))) {
                out.append(request);
            }
        }
        return out;
    };
    QCOMPARE(controlRequests().size(), 1);
    QCOMPARE(controlRequests().first().arguments,
             QStringList({QStringLiteral("set-volume"),
                          QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                          QStringLiteral("0.50")}));

    // An exact match against the observed reading (30) is a clean no-op.
    const QString noOpId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Already there","devices":["LOCAL-PC"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":30}]})json"));
    startRunnerCompleter();
    const QDBusMessage noOpRun = waitForReply(m_proxy->RunRule(noOpId));
    stopRunnerCompleter();
    QCOMPARE(noOpRun.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(controlRequests().size(), 1);  // the no-op dispatched nothing new
}

void DaemonDbusTest::pcMuteDispatchesToggleShapesAndNoOpsWhenEqual() {
    recreateDaemon(30s, {}, {}, 30ms);
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.40\n"));  // audible

    const QString muteId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Hush","devices":["LOCAL-PC"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"mute","muted":true}]})json"));
    startRunnerCompleter();
    const QDBusMessage muted = waitForReply(m_proxy->RunRule(muteId));
    stopRunnerCompleter();
    QCOMPARE(muted.arguments().front().toString(), QStringLiteral("ok"));

    // Only control invocations count here; the background get-volume poller
    // shares the wpctl program name.
    const auto controlRequests = [this] {
        QList<ProcessRequest> out;
        for (const ProcessRequest& request : m_runner->requests) {
            if (request.program == QStringLiteral("wpctl") && !request.arguments.isEmpty()
                && (request.arguments.first() == QStringLiteral("set-volume")
                    || request.arguments.first() == QStringLiteral("set-mute"))) {
                out.append(request);
            }
        }
        return out;
    };
    QCOMPARE(controlRequests().size(), 1);
    QCOMPARE(controlRequests().first().arguments,
             QStringList({QStringLiteral("set-mute"),
                          QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                          QStringLiteral("1")}));

    // The daemon now observes the muted state: muting again is a no-op...
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.40 [MUTED]\n"));
    startRunnerCompleter();
    const QDBusMessage muteAgain = waitForReply(m_proxy->RunRule(muteId));
    stopRunnerCompleter();
    QCOMPARE(muteAgain.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(controlRequests().size(), 1);  // muting again dispatched nothing new

    // ...and unmuting flips the flag with the 0 argument.
    const QString unmuteId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Sound on","devices":["LOCAL-PC"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"mute","muted":false}]})json"));
    startRunnerCompleter();
    const QDBusMessage unmuted = waitForReply(m_proxy->RunRule(unmuteId));
    stopRunnerCompleter();
    QCOMPARE(unmuted.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(m_runner->lastRequest.program, QStringLiteral("wpctl"));
    QCOMPARE(m_runner->lastRequest.arguments,
             QStringList({QStringLiteral("set-mute"),
                          QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                          QStringLiteral("0")}));
}

void DaemonDbusTest::keypressOnLocalPcIsRejectedWithoutTraffic() {
    recreateDaemon(30s, {}, {}, 30ms);
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.30\n"));
    const int rdsTrafficBefore = static_cast<int>(std::count_if(
        m_runner->requests.cbegin(), m_runner->requests.cend(),
        [](const ProcessRequest& request) { return request.program == QLatin1String("rds"); }));

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nothing to press","devices":["LOCAL-PC"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
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

    // A rejected attempt never leaves the machine: zero new rds traffic.
    const int rdsTrafficAfter = static_cast<int>(std::count_if(
        m_runner->requests.cbegin(), m_runner->requests.cend(),
        [](const ProcessRequest& request) { return request.program == QLatin1String("rds"); }));
    QCOMPARE(rdsTrafficAfter, rdsTrafficBefore);
}

void DaemonDbusTest::wildcardStateChangeRuleIgnoresLocalPcEdges() {
    recreateDaemon(30s, {}, {}, 30ms);
    // Wildcard shape: no devices array at all.
    const QString wildcardId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Any TV watcher","trigger":{"type":"stateChange","field":"volume",)json"
        R"json("op":"lt","value":20},"actions":[{"type":"keypress","key":"Home"}]})json"));

    completePcVolumePoll(QByteArrayLiteral("Volume: 0.25\n"));   // baseline above threshold
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.05\n"));   // PC crosses below 20

    const QJsonArray rulesAfterWildcard =
        rulesListing().value(QStringLiteral("rules")).toArray();
    QCOMPARE(rulesAfterWildcard.size(), 1);
    QCOMPARE(rulesAfterWildcard.at(0).toObject().value(QStringLiteral("id")).toString(),
             wildcardId);
    QVERIFY2(!rulesAfterWildcard.at(0).toObject().contains(QStringLiteral("lastRun")),
             "a wildcard stateChange rule must not fire on Local PC edges");

    // The same rule shape, but naming This PC explicitly: identical crossings
    // DO fire it. Keypress cannot act on the PC, so the honest record is a
    // fired run whose lone attempt was rejected (spec §4).
    const QString explicitId = addRuleReturningId(QStringLiteral(
        R"json({"name":"PC watcher","devices":["LOCAL-PC"],"trigger":{"type":"stateChange",)json"
        R"json("field":"volume","op":"lt","value":20},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    completePcVolumePoll(QByteArrayLiteral("Volume: 0.25\n"));  // leave the predicate
    completePcVolumePoll(QByteArrayLiteral("Volume: 0.05\n"));  // rising edge into it

    const QJsonArray rulesAfterExplicit =
        rulesListing().value(QStringLiteral("rules")).toArray();
    QCOMPARE(rulesAfterExplicit.at(0).toObject().value(QStringLiteral("id")).toString(),
             wildcardId);
    QVERIFY2(!rulesAfterExplicit.at(0).toObject().contains(QStringLiteral("lastRun")),
             "the wildcard rule stays silent even across later crossings");
    QCOMPARE(rulesAfterExplicit.at(1).toObject().value(QStringLiteral("id")).toString(),
             explicitId);
    QCOMPARE(rulesAfterExplicit.at(1).toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("failed"));

    // Zero device traffic anywhere: nothing left the machine, for either rule.
    QCOMPARE(homeKeypressCount(), 0);
    for (const ProcessRequest& request : m_runner->requests) {
        if (request.program == QLatin1String("wpctl")) {
            QVERIFY(request.arguments.first() == QLatin1String("get-volume"));
        } else {
            QVERIFY2(request.arguments.first() != QLatin1String("keypress"),
                     "no keypress may reach any runner invocation");
        }
    }
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
        R"json({"name":"Silence please","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
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
        R"json({"name":"Stay quiet","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
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
        R"json({"name":"Softly","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
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

void DaemonDbusTest::ceilingClampsAnUpwardVolumeWalk() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.41")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    const QDBusMessage ceiling =
        waitForReply(m_proxy->UpdateDevice(QStringLiteral("SERIAL-A"),
                                           QStringLiteral(R"json({"volumeCeiling":50})json")));
    QVERIFY2(ceiling.type() != QDBusMessage::ErrorMessage, qPrintable(ceiling.errorMessage()));

    // Observed 46 walking toward 90 stops at the 50 ceiling: a delta of 4
    // presses, not the 30-press walk an unclamped target would attempt.
    completePollVersion();
    completePollReadings(46);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Loud but legal","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":90}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 4);
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 0);
}

void DaemonDbusTest::volumeWalkBeyondQueueCapDeliversEveryPress() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.44")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // Observed 10 walking toward 90: a delta of 80, bounded to 30 presses.
    // The per-device queue holds only 4 outstanding commands, so the walk
    // must deliver in batches — every press lands, none are rejected.
    completePollVersion();
    completePollReadings(10);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Crank it","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":90}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 30);
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 0);
}

void DaemonDbusTest::volumeWalkWithBusyQueueDeliversEveryPress() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.45")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("busy-audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    // Observed 10 walking toward 90: 30 upward presses across several
    // delivery batches.
    completePollVersion();
    completePollReadings(10);

    // Occupy the per-device queue with unrelated widget commands before the
    // run starts. The first walk batch can only claim the one free slot; the
    // presses that do not fit must wait for a later batch, not count as
    // rejected attempts (that would report a false "partial" even though
    // every press landed).
    const QString widgetCommand =
        QStringLiteral(R"json({"operation":"keypress","key":"Back"})json");
    for (int press = 0; press < 3; ++press) {
        const QDBusMessage queued =
            waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), widgetCommand));
        QVERIFY2(queued.type() != QDBusMessage::ErrorMessage, qPrintable(queued.errorMessage()));
    }

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Crank it while busy","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":90}]})json"));

    // Let the run begin with the queue already busy, then drain everything
    // from inside its delivery wait.
    QTimer::singleShot(10, this, [this] { startRunnerCompleter(); });
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 30);
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 0);
}

void DaemonDbusTest::downwardWalksIgnoreTheCeiling() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.42")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    const QDBusMessage ceiling =
        waitForReply(m_proxy->UpdateDevice(QStringLiteral("SERIAL-A"),
                                           QStringLiteral(R"json({"volumeCeiling":50})json")));
    QVERIFY2(ceiling.type() != QDBusMessage::ErrorMessage, qPrintable(ceiling.errorMessage()));

    // Lowering the volume is none of the ceiling's business.
    completePollVersion();
    completePollReadings(24);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Softly","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":20}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 4);
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 0);
}

void DaemonDbusTest::setVolumeAtTheCeilingIsACleanNoOp() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.43")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    const QDBusMessage ceiling =
        waitForReply(m_proxy->UpdateDevice(QStringLiteral("SERIAL-A"),
                                           QStringLiteral(R"json({"volumeCeiling":50})json")));
    QVERIFY2(ceiling.type() != QDBusMessage::ErrorMessage, qPrintable(ceiling.errorMessage()));

    // Observed at the ceiling, a request for 60 clamps to 50: nothing to do.
    completePollVersion();
    completePollReadings(50);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Already loud enough","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"setVolume","volume":60}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 0);
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 0);
}

void DaemonDbusTest::mixedCeilingsKeepPartialOutcomesTruthful() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.44")});
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-B"), QStringLiteral("Kitchen TV"), QStringLiteral("192.0.2.45")});
    // Interest tracks SERIAL-A only, so SERIAL-B never reports audio.
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("audio-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    const QDBusMessage ceiling =
        waitForReply(m_proxy->UpdateDevice(QStringLiteral("SERIAL-A"),
                                           QStringLiteral(R"json({"volumeCeiling":50})json")));
    QVERIFY2(ceiling.type() != QDBusMessage::ErrorMessage, qPrintable(ceiling.errorMessage()));

    completePollVersion();
    completePollReadings(50);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Whole house","trigger":{"type":"manual"},"devices":["SERIAL-A","SERIAL-B"],)json"
        R"json("actions":[{"type":"setVolume","volume":60},{"type":"keypress","key":"Select"}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // The ceiling no-op on A must not paper over B's rejection: the keypresses
    // landed (two devices), the blind setVolume did not.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("partial"));
    QCOMPARE(keypressCount(QStringLiteral("Select")), 2);
    QCOMPARE(keypressCount(QStringLiteral("VolumeUp")), 0);
}

void DaemonDbusTest::mixedListWithIfThenElseRunsToCompletion() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.91")});

    // Every parseable action type is now executable, so a mixed list no
    // longer aborts: the Home ahead of the branch must land, and the
    // unobservable power gate cleanly skips to its (empty) else.
    const QString mixedId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Mixed macro","devices":["SERIAL-A"],"trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"keypress","key":"Home"},)json"
        R"json({"type":"ifThenElse","cond":{"leaf":{"device":"*","field":"power",)json"
        R"json("op":"is","value":"on"}},"then":[{"type":"keypress","key":"Back"}]}]})json"));

    startRunnerCompleter();
    const QDBusMessage mixedRun = waitForReply(m_proxy->RunRule(mixedId));
    stopRunnerCompleter();
    QVERIFY2(mixedRun.type() != QDBusMessage::ErrorMessage,
             qPrintable(mixedRun.errorMessage()));
    QCOMPARE(mixedRun.arguments().front().toString(), QStringLiteral("ok"));

    // The plain step dispatches; the missing-snapshot gate matches nothing,
    // so the then-branch Back never does.
    QCOMPARE(homeKeypressCount(), 1);
    QCOMPARE(keypressCount(QStringLiteral("Back")), 0);
}

void DaemonDbusTest::macroChainRunsChildAndReportsOk() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.94")});
    // The child is added first so its generated id can seed the parent's
    // macro target without a rewrite.
    const QString childId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Child step","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    const QString parentId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Movie night","devices":["SERIAL-A"],"trigger":{"type":"manual"},"actions":[)json"
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

void DaemonDbusTest::disabledMacroChildIsRejectedWithoutRunning() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.97")});
    const QString childId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Sleeping step","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    const QString parentId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Disabled gate","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"runRule","id":"%1"}]})json").arg(childId));
    const QDBusMessage disabled = waitForReply(
        m_proxy->SetRuleEnabled(childId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(parentId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // A disabled child is exactly a dangling one: one rejected attempt.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));

    // The gated macro step must not reach any device.
    QVERIFY(m_runner->requests.isEmpty());
    QCOMPARE(homeKeypressCount(), 0);

    // And the child itself never ran, so it records no outcome.
    const QJsonObject child = rulesListing()
                                  .value(QStringLiteral("rules"))
                                  .toArray()
                                  .at(0)
                                  .toObject();
    QCOMPARE(child.value(QStringLiteral("id")).toString(), childId);
    QVERIFY2(!child.value(QStringLiteral("lastRun")).isObject(),
             "a disabled macro target must never record a lastRun");
}

void DaemonDbusTest::condTrueRunsThenBranch() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.41")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("branch-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // Seed the snapshot both the volume walk and the branch gate read:
    // 18 sits under the threshold, so the then-branch must win.
    completePollVersion();
    completePollReadings(18);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Quiet branch","devices":["SERIAL-A"],"trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"setVolume","volume":15},)json"
        R"json({"type":"ifThenElse","cond":{"leaf":{"device":"*","field":"volume",)json"
        R"json("op":"lt","value":20}},)json"
        R"json("then":[{"type":"keypress","key":"Home"}],)json"
        R"json("else":[{"type":"keypress","key":"Back"}]}]})json"));

    startRunnerCompleter();  // the run waits for its delivery verdict
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));

    // Three VolumeDown presses land the walk, then exactly one Home from the
    // taken branch; the untaken else never dispatches anything.
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 3);
    QCOMPARE(homeKeypressCount(), 1);
    QCOMPARE(keypressCount(QStringLiteral("Back")), 0);
}

void DaemonDbusTest::condFalseRunsElseBranch() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.42")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("branch-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    completePollVersion();
    completePollReadings(18);  // volume 18 fails a "louder than 50" gate

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Loud branch","devices":["SERIAL-A"],"trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"setVolume","volume":15},)json"
        R"json({"type":"ifThenElse","cond":{"leaf":{"device":"*","field":"volume",)json"
        R"json("op":"gt","value":50}},)json"
        R"json("then":[{"type":"keypress","key":"Home"}],)json"
        R"json("else":[{"type":"keypress","key":"Back"}]}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));

    // The false gate routes to else: one Back, and no Home ever fires.
    QCOMPARE(keypressCount(QStringLiteral("VolumeDown")), 3);
    QCOMPARE(keypressCount(QStringLiteral("Back")), 1);
    QCOMPARE(homeKeypressCount(), 0);
}

void DaemonDbusTest::falseCondWithoutElseIsCleanNoOp() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.43")});

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Silent branch","trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"ifThenElse","cond":{"leaf":{"device":"*","field":"volume",)json"
        R"json("op":"gt","value":50}},)json"
        R"json("then":[{"type":"keypress","key":"Home"}]}]})json"));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // A false condition with no else is a successful nothing: no attempts, so
    // the run earns ok without touching a device. No client is registered, so
    // the snapshot is empty and the gate can never pass.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(homeKeypressCount(), 0);
}

void DaemonDbusTest::ifThenElseBranchRecursionCarriesCycleContext() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.44")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("branch-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    // The branch gate needs an observed snapshot to evaluate true; without
    // one the else path would silently skip the macro step under test.
    completePollVersion();
    completePollReadings(18);

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Bootstrap","trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    // Self-reference is only expressible after creation: nest the macro call
    // inside a then-branch so recursion must carry the cycle context in.
    const QString rewritten = QStringLiteral(
        R"json({"id":"%1","name":"Ouroboros branch","enabled":true,"mode":"restart",)json"
        R"json("devices":["SERIAL-A"],"trigger":{"type":"manual"},"actions":[)json"
        R"json({"type":"ifThenElse","cond":{"leaf":{"device":"SERIAL-A","field":"volume",)json"
        R"json("op":"lt","value":20}},)json"
        R"json("then":[{"type":"runRule","id":"%1"}]}]})json").arg(ruleId);
    const QDBusMessage update = waitForReply(m_proxy->UpdateRule(rewritten));
    QVERIFY2(update.type() != QDBusMessage::ErrorMessage,
             qPrintable(update.errorMessage()));

    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // The branch's macro call names its own enclosing rule: only honest
    // call-stack plumbing through the recursive dispatcher ends this run as
    // cycle-error instead of looping forever.
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

    // Cutting the branch means nothing ever reaches a device; stray poll
    // traffic may share the request log, but never a keypress.
    QCOMPARE(homeKeypressCount(), 0);
    QCOMPARE(keypressCount(QStringLiteral("Back")), 0);
}

QStringList DaemonDbusTest::announcedLastRunOutcomes(const QSignalSpy& spy) const {
    QStringList outcomes;
    for (const QList<QVariant>& arguments : spy) {
        const QJsonArray rules =
            QJsonDocument::fromJson(arguments.front().toString().toUtf8())
                .object()
                .value(QStringLiteral("rules"))
                .toArray();
        outcomes.append(rules.isEmpty()
                            ? QString()
                            : rules.last().toObject()
                                  .value(QStringLiteral("lastRun"))
                                  .toObject()
                                  .value(QStringLiteral("outcome"))
                                  .toString());
    }
    return outcomes;
}

void DaemonDbusTest::singleModeDropsOverlappingFiring() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.40")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Solo","mode":"single","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"},)json"
        R"json({"type":"delay","seconds":1}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // The nested firing lands inside the delay's event pump while the first
    // run is Active: single mode must drop it without disturbing that run.
    QTimer::singleShot(50ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    startRunnerCompleter();  // the run waits for its delivery verdict
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();

    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    // Exactly one device pass: the suppressed firing dispatched nothing.
    QCOMPARE(homeKeypressCount(), 1);
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QCOMPARE(outcomes.count(QStringLiteral("dropped")), 1);
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::restartModeSupersedesActiveRun() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.41")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Chaser","mode":"restart","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"},)json"
        R"json({"type":"delay","seconds":1},{"type":"keypress","key":"Select"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // Mid-delay the restart arrives: the active run is cancelled (its Select
    // step never dispatches and it records nothing), then a fresh full run
    // executes — both Homes land, exactly one Select does.
    QTimer::singleShot(150ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    startRunnerCompleter();
    m_daemon->requestRuleFire(ruleId);
    stopRunnerCompleter();

    QCOMPARE(homeKeypressCount(), 2);
    QCOMPARE(keypressCount(QStringLiteral("Select")), 1);
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    // The cancelled run records nothing: only the fresh run's ok appears.
    QCOMPARE(outcomes.count(QStringLiteral("ok")), 1);
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::queuedModeCoalescesThenDrains() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.42")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Cohort","mode":"queued","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"},)json"
        R"json({"type":"delay","seconds":1}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // Fire #2 during the active delay takes the single queued slot; fire #3
    // finds both slots busy and is dropped; when the active run finishes the
    // queued slot drains as a second full run with no further prompting.
    QTimer::singleShot(50ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    QTimer::singleShot(120ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    startRunnerCompleter();
    m_daemon->requestRuleFire(ruleId);
    stopRunnerCompleter();

    QCOMPARE(homeKeypressCount(), 2);
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QCOMPARE(outcomes.count(QStringLiteral("dropped")), 1);
    QCOMPARE(outcomes.count(QStringLiteral("ok")), 2);
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::suppressedFiringAnnouncesDroppedRecord() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.43")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Gatekeeper","mode":"single","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"},)json"
        R"json({"type":"delay","seconds":1}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    QTimer::singleShot(50ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));

    // The suppression is a first-class record: its own announcement carries
    // outcome "dropped" with a timestamp, and it dispatches nothing itself.
    QCOMPARE(homeKeypressCount(), 1);
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    const int droppedAt = outcomes.indexOf(QStringLiteral("dropped"));
    QVERIFY2(droppedAt >= 0, "a suppressed firing must be announced as dropped");
    const QJsonObject droppedAnnouncement =
        QJsonDocument::fromJson(changed.at(droppedAt).at(0).toString().toUtf8())
            .object()
            .value(QStringLiteral("rules"))
            .toArray()
            .last()
            .toObject();
    QCOMPARE(droppedAnnouncement.value(QStringLiteral("id")).toString(), ruleId);
    QVERIFY2(!droppedAnnouncement.value(QStringLiteral("lastRun"))
                  .toObject()
                  .value(QStringLiteral("lastFiredAt"))
                  .toString()
                  .isEmpty(),
             "a dropped record must carry its timestamp");
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::macroInvocationBypassesModeGate() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.44")});
    const QString slowId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Slowpoke","mode":"single","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Back"},)json"
        R"json({"type":"delay","seconds":1}]})json"));
    const QString invokerId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Invoker","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"runRule","id":"%1"}]})json").arg(slowId));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    startRunnerCompleter();
    // While Slowpoke's own firing sits Active in its delay, Invoker's macro
    // step names it: an action is not a firing, so the gate must not apply —
    // the nested invocation dispatches even though the target is Active.
    QTimer::singleShot(150ms, this, [this, invokerId] {
        const QDBusMessage mid = waitForReply(m_proxy->RunRule(invokerId));
        QCOMPARE(mid.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(mid.arguments().front().toString(), QStringLiteral("ok"));
    });
    m_daemon->requestRuleFire(slowId);  // blocks until Slowpoke's own run ends
    stopRunnerCompleter();

    // Two Back presses: Slowpoke's own run plus the ungated macro invocation.
    QCOMPARE(keypressCount(QStringLiteral("Back")), 2);
    QCOMPARE(homeKeypressCount(), 0);
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QVERIFY2(!outcomes.contains(QStringLiteral("dropped")),
             "a macro child must never be gated into a drop");
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::stateChangeFiringGatedByConditionsRecordsConditionFalse() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.45")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("sc-cond-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));

    completePollVersion();
    completePollReadings(25);  // baseline above the threshold: no firing

    // The stateChange trigger and the condition tree compose: an edge into
    // the predicate while unmuted fires the pipeline, but the condition gate
    // stops it as condition-false before anything reaches a device.
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Muted watchdog","devices":["SERIAL-A"],"trigger":{"type":"stateChange","field":"volume",)json"
        R"json("op":"lt","value":20},"conditions":{"leaf":{"device":"*","field":"muted",)json"
        R"json("op":"is","value":true}},"actions":[{"type":"keypress","key":"Home"}]})json"));

    completePollReadings(5);
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 0);
    QCOMPARE(rulesListing()
                 .value(QStringLiteral("rules"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("condition-false"));

    // Falling edges stay silent; the next rising edge arrives muted, the gate
    // passes, and the rule runs to a real dispatch.
    completePollReadings(25, true);
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 0);

    startRunnerCompleter();  // the gated-in firing waits for its delivery verdict
    completePollReadings(8, true);
    stopRunnerCompleter();
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 1);
    const QJsonObject fired =
        rulesListing().value(QStringLiteral("rules")).toArray().at(0).toObject();
    QCOMPARE(fired.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(fired.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
}

void DaemonDbusTest::restartThenThirdFiringDuringReplacementIsGated() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.46")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Relay","mode":"restart","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"},)json"
        R"json({"type":"delay","seconds":5},{"type":"keypress","key":"Select"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // F1 runs and is superseded by F2 at 50 ms; F3 lands at 150 ms inside
    // F2's pumps — exactly the window where a stale frame's cleanup used to
    // be able to erase the replacement's live state. The machine must stay
    // coherent: F3 finds the Active state and valid token of F2 and, per the
    // restart mode, supersedes it; only F3 completes with a record.
    QTimer::singleShot(50ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    QTimer::singleShot(150ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    startRunnerCompleter();
    static_cast<void>(m_daemon->requestRuleFire(ruleId));
    stopRunnerCompleter();

    QCOMPARE(homeKeypressCount(), 3);  // one Home per firing: no duplicate runs
    QCOMPARE(keypressCount(QStringLiteral("Select")), 1);  // only the final run's
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QCOMPARE(outcomes.count(QStringLiteral("ok")), 1);
    QCOMPARE(outcomes.count(QStringLiteral("dropped")), 0);
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::singleModeThirdFiringDuringActiveRunIsDropped() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.47")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Anchor","mode":"single","devices":["SERIAL-A"],)json"
        R"json("trigger":{"type":"manual"},"actions":[{"type":"keypress","key":"Home"},)json"
        R"json({"type":"delay","seconds":1}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // Two overlapping firings during one active run: both must be dropped,
    // never started, whatever cleanup ordering the unwinding frames use.
    QTimer::singleShot(50ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    QTimer::singleShot(150ms, this, [this, ruleId] { m_daemon->requestRuleFire(ruleId); });
    startRunnerCompleter();
    static_cast<void>(m_daemon->requestRuleFire(ruleId));
    stopRunnerCompleter();

    QCOMPARE(homeKeypressCount(), 1);
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QCOMPARE(outcomes.count(QStringLiteral("dropped")), 2);
    QCOMPARE(outcomes.last(), QStringLiteral("ok"));
}

void DaemonDbusTest::runRuleOnDisabledRuleIsTypedRejected() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.48")});
    const QString ruleId = addManualRule(QStringLiteral("Sleeping"));
    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();

    // Spec §4.3: only enabled rules fire. The manual caller gets the typed
    // rejection, nothing reaches a device, and no last-run record appears.
    QCOMPARE(run.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(run.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
    QVERIFY2(run.errorMessage().contains(QStringLiteral("disabledRule")),
             qPrintable(run.errorMessage()));
    QVERIFY(m_runner->requests.isEmpty());
    QVERIFY2(!rulesListing()
                  .value(QStringLiteral("rules"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .contains(QStringLiteral("lastRun")),
             "a refused firing must not record a last-run entry");
}

void DaemonDbusTest::stateChangeEdgeOnDisabledRuleDoesNotFire() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.49")});
    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("sc-off-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    completePollVersion();
    completePollReadings(25);  // baseline above the threshold

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Muted watcher","trigger":{"type":"stateChange","field":"volume",)json"
        R"json("op":"lt","value":20},"actions":[{"type":"keypress","key":"Home"}]})json"));
    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));

    // An edge into the predicate after the rule was disabled fires nothing.
    completePollReadings(5);
    QTest::qWait(50);
    QCOMPARE(homeKeypressCount(), 0);
    QVERIFY2(!rulesListing()
                  .value(QStringLiteral("rules"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .contains(QStringLiteral("lastRun")),
             "a skipped firing must not record a last-run entry");
}

void DaemonDbusTest::windowedRuleFiresInsideAndRecordsInactiveOutside() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.50")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Overnight guard","devices":["SERIAL-A"],"trigger":{"type":"manual",)json"
        R"json("activeWindow":{"start":"22:00","end":"08:00"}},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    // Midday: the firing is recorded, announced and dispatches nothing.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(12, 0));
    const QDBusMessage noon = waitForReply(m_proxy->RunRule(ruleId));
    QVERIFY2(noon.type() != QDBusMessage::ErrorMessage, qPrintable(noon.errorMessage()));
    QCOMPARE(noon.arguments().front().toString(), QStringLiteral("inactive-window"));
    QVERIFY(m_runner->requests.isEmpty());
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QVERIFY2(outcomes.contains(QStringLiteral("inactive-window")),
             "an out-of-window firing must be announced as inactive-window");
    QJsonObject stored = rulesListing()
                             .value(QStringLiteral("rules"))
                             .toArray()
                             .at(0)
                             .toObject();
    QCOMPARE(stored.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(stored.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("inactive-window"));
    QVERIFY2(!stored.value(QStringLiteral("lastRun"))
                  .toObject()
                  .value(QStringLiteral("lastFiredAt"))
                  .toString()
                  .isEmpty(),
             "an inactive-window record must carry its timestamp");

    // 23:00 sits inside a window that wraps midnight.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(23, 0));
    startRunnerCompleter();
    const QDBusMessage night = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(night.type() != QDBusMessage::ErrorMessage, qPrintable(night.errorMessage()));
    QCOMPARE(night.arguments().front().toString(), QStringLiteral("ok"));

    // 07:00 is on the far side of the wrap, still inside.
    m_now = QDateTime(QDate(2026, 1, 6), QTime(7, 0));
    startRunnerCompleter();
    const QDBusMessage morning = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(morning.type() != QDBusMessage::ErrorMessage,
             qPrintable(morning.errorMessage()));
    QCOMPARE(homeKeypressCount(), 2);
}

void DaemonDbusTest::windowBoundariesAreInclusiveStartExclusiveEnd() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.51")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Evening only","devices":["SERIAL-A"],"trigger":{"type":"manual",)json"
        R"json("activeWindow":{"start":"09:00","end":"10:00"}},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));

    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 0));  // exactly start: fires
    startRunnerCompleter();
    const QDBusMessage opening = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QCOMPARE(opening.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(homeKeypressCount(), 1);

    m_now = QDateTime(QDate(2026, 1, 5), QTime(10, 0));  // exactly end: gated
    const QDBusMessage closing = waitForReply(m_proxy->RunRule(ruleId));
    QCOMPARE(closing.arguments().front().toString(), QStringLiteral("inactive-window"));
    QCOMPARE(homeKeypressCount(), 1);
}

void DaemonDbusTest::scheduledWindowedRuleRecordsInactiveWindow() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.52")});
    // The schedule fires daily at 09:00; its own window forbids that instant.
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Never at nine","devices":["SERIAL-A"],"trigger":{"type":"time",)json"
        R"json("kind":"daily","time":"09:00","activeWindow":{"start":"22:00","end":"08:00"}},)json"
        R"json("actions":[{"type":"keypress","key":"Home"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 0));
    m_daemon->evaluateDueRules();

    // The scheduled origin hits the same gate: nothing dispatches, yet the
    // skipped occurrence is recorded and announced like a manual one.
    QVERIFY(m_runner->requests.isEmpty());
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QVERIFY2(outcomes.contains(QStringLiteral("inactive-window")),
             "the scheduled origin must record inactive-window too");
}

void DaemonDbusTest::runScriptHappyRunIsDeliveredAsOk() {
    // Contract pin: scripts get their own generous completion budget.
    QCOMPARE(CommandContract::scriptTimeoutMs, 30000);

    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.53")});
    QTemporaryDir scripts;
    QVERIFY(scripts.isValid());
    const QString scriptPath = scripts.filePath(QStringLiteral("movie-night"));
    {
        QFile script(scriptPath);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(script.write("#!/bin/sh\nexit 0\n") > 0);
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    recreateDaemon(30s, {}, [this] { return m_now; }, 1h, scripts.path());
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Movie night","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"runScript","script":"movie-night"}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    // The script runs as itself — program-routed, no arguments, no shell.
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.program,
             QFileInfo(scriptPath).canonicalFilePath());
    QVERIFY(m_runner->lastRequest.arguments.isEmpty());
}

void DaemonDbusTest::runScriptNonZeroExitFailsTheRun() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.54")});
    QTemporaryDir scripts;
    QVERIFY(scripts.isValid());
    const QString scriptPath = scripts.filePath(QStringLiteral("grumpy"));
    {
        QFile script(scriptPath);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(script.write("#!/bin/sh\nexit 1\n") > 0);
        script.setPermissions(QFile::ReadOwner | QFile::ExeOwner);
    }
    recreateDaemon(30s, {}, [this] { return m_now; }, 1h, scripts.path());
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Grumpy script","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"runScript","script":"grumpy"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);

    startRunnerCompleter({1});
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    // A non-zero exit is a delivery failure, so the run reports failed and
    // records it.
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));
    const QStringList outcomes = announcedLastRunOutcomes(changed);
    QCOMPARE(outcomes.last(), QStringLiteral("failed"));
}

void DaemonDbusTest::runScriptRejectsBadFilesWithoutTraffic() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.55")});
    QTemporaryDir scripts;
    QVERIFY(scripts.isValid());
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    // A regular but non-executable file...
    {
        QFile locked(scripts.filePath(QStringLiteral("locked")));
        QVERIFY(locked.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(locked.write("#!/bin/sh\nexit 0\n") > 0);
    }
    // ...and an executable outside the directory reachable only via symlink:
    // the canonical-containment check must defeat the escape.
    const QString sneaky = outside.filePath(QStringLiteral("sneaky"));
    {
        QFile target(sneaky);
        QVERIFY(target.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(target.write("#!/bin/sh\nexit 0\n") > 0);
        target.setPermissions(QFile::ReadOwner | QFile::ExeOwner);
    }
    QVERIFY(QFile::link(sneaky, scripts.filePath(QStringLiteral("escape"))));

    recreateDaemon(30s, {}, [this] { return m_now; }, 1h, scripts.path());
    auto scriptRule = [](const QString& name, const QString& script) {
        return QStringLiteral(
                   R"json({"name":"%1","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
                   R"json("actions":[{"type":"runScript","script":"%2"}]})json")
            .arg(name, script);
    };

    const QStringList cases{
        QStringLiteral("ghost"),   // missing file
        QStringLiteral("locked"),  // not executable
        QStringLiteral("escape")   // symlink out of the directory
    };
    QList<QString> ruleIds;
    for (const QString& script : cases) {
        ruleIds.append(addRuleReturningId(
            scriptRule(QStringLiteral("Broken %1").arg(script), script)));
    }

    // Every rejection is dispatch-time: zero traffic, one rejected attempt
    // per action, hence "failed".
    for (const QString& ruleId : ruleIds) {
        const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
        QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
        QCOMPARE(run.arguments().front().toString(), QStringLiteral("failed"));
    }
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::runScriptAcceptsSymlinkedScriptsDirectory() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.57")});
    QTemporaryDir realScripts;
    QVERIFY(realScripts.isValid());
    const QString scriptPath = realScripts.filePath(QStringLiteral("movie-night"));
    {
        QFile script(scriptPath);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(script.write("#!/bin/sh\nexit 0\n") > 0);
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    // A scripts directory that is itself a symlink (dotfile managers and
    // secondary drives do this): containment must compare canonical paths on
    // BOTH sides, otherwise every legitimate script is rejected.
    QTemporaryDir links;
    QVERIFY(links.isValid());
    const QString linkedDirectory = links.filePath(QStringLiteral("scripts"));
    QVERIFY(QFile::link(realScripts.path(), linkedDirectory));

    recreateDaemon(30s, {}, [this] { return m_now; }, 1h, linkedDirectory);
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Linked scripts","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"runScript","script":"movie-night"}]})json"));

    startRunnerCompleter();
    const QDBusMessage run = waitForReply(m_proxy->RunRule(ruleId));
    stopRunnerCompleter();
    QVERIFY2(run.type() != QDBusMessage::ErrorMessage, qPrintable(run.errorMessage()));
    QCOMPARE(run.arguments().front().toString(), QStringLiteral("ok"));
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.program, QFileInfo(scriptPath).canonicalFilePath());
    QVERIFY(m_runner->lastRequest.arguments.isEmpty());
}

void DaemonDbusTest::addRuleRejectsInvalidScriptNames() {
    m_daemon->upsertDevice(
        {QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"), QStringLiteral("192.0.2.56")});
    const QString escapePayload = QStringLiteral(
        R"json({"name":"Escape","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"runScript","script":"../evil"}]})json");
    const QDBusMessage escape = waitForReply(m_proxy->AddRule(escapePayload));
    QCOMPARE(escape.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(escape.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));

    const QString upperPayload = QStringLiteral(
        R"json({"name":"Upper","devices":["SERIAL-A"],"trigger":{"type":"manual"},)json"
        R"json("actions":[{"type":"runScript","script":"Movie-Night"}]})json");
    const QDBusMessage upper = waitForReply(m_proxy->AddRule(upperPayload));
    QCOMPARE(upper.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(upper.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));

    QVERIFY(m_runner->requests.isEmpty());
}

QTEST_GUILESS_MAIN(DaemonDbusTest)
#include "daemon_dbus_test.moc"

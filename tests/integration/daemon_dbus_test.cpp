// SPDX-License-Identifier: GPL-3.0-or-later
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
    void queryFailureDoesNotBlockAnotherDevice();
    void commandSchemaRejectsUnboundedAndUnsupportedWork();
    void commandsSerializePerDevice();
    void commandsOnDifferentDevicesRunIndependently();
    void pendingCommandDefersActualIdleExpiry();

  private:
    [[nodiscard]] QDBusMessage call(const QString& method,
                                    const QVariantList& arguments = {}) const;
    [[nodiscard]] static QDBusMessage callOnConnection(const QDBusConnection& connection,
                                                       const QString& method,
                                                       const QVariantList& arguments = {});
    [[nodiscard]] static QDBusMessage waitForReply(const QDBusPendingCall& pending);
    [[nodiscard]] static QJsonObject jsonResult(const QDBusMessage& reply);
    [[nodiscard]] static bool waitForHelperRegistration(QProcess& client);
    void recreateDaemon(std::chrono::milliseconds idleGrace = 30s);

    QTemporaryDir m_configDirectory;
    QString m_configPath;
    bool m_automationEnabled = true;
    FakeProcessRunner* m_runner = nullptr;
    std::unique_ptr<DaemonService> m_daemon;
    std::unique_ptr<IoGithubJosephurRokuDeck1Interface> m_proxy;
};

void DaemonDbusTest::init() {
    QVERIFY2(QDBusConnection::sessionBus().isConnected(),
             "daemon-dbus must run on a private session bus");
    QVERIFY(m_configDirectory.isValid());
    m_automationEnabled = true;
    m_configPath = m_configDirectory.filePath(QString::fromLatin1(QTest::currentTestFunction()) +
                                              QStringLiteral(".rc"));

    recreateDaemon();
}

void DaemonDbusTest::recreateDaemon(std::chrono::milliseconds idleGrace) {
    m_proxy.reset();
    m_daemon.reset();
    auto runner = std::make_unique<FakeProcessRunner>();
    m_runner = runner.get();
    auto config = KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig);
    auto configStore = std::make_unique<ConfigStore>(std::move(config));
    m_daemon = std::make_unique<DaemonService>(
        QDBusConnection::sessionBus(), [this] { return m_automationEnabled; }, std::move(runner),
        std::move(configStore), idleGrace);
    QVERIFY2(m_daemon->registerOnBus(),
             "the test daemon could not own its private session-bus name");
    m_proxy = std::make_unique<IoGithubJosephurRokuDeck1Interface>(
        QStringLiteral("io.github.josephur.RokuDeck1"),
        QStringLiteral("/io/github/josephur/RokuDeck1"), QDBusConnection::sessionBus());
}

void DaemonDbusTest::cleanup() {
    m_proxy.reset();
    m_daemon.reset();
    m_runner = nullptr;
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

    QHash<QString, QStringList> methods;
    QHash<QString, QStringList> signals;
    QXmlStreamReader xml(introspected.arguments().front().toString());
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
    QCOMPARE(methods.size(), 7);
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
    QCOMPARE(signals.size(), 3);
    QCOMPARE(signals.value(QStringLiteral("DeviceSnapshotChanged")),
             QStringList({QStringLiteral("s"), QStringLiteral("s")}));
    QCOMPARE(signals.value(QStringLiteral("DeviceListChanged")), QStringList{QStringLiteral("s")});
    QCOMPARE(signals.value(QStringLiteral("CommandFinished")),
             QStringList({QStringLiteral("s"), QStringLiteral("s")}));

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
    const QByteArray onlineEnvelope = R"json({
        "results": [
            {"endpoint":"/query/device-info","status":200,"body":"<device-info><serial-number>SERIAL-A</serial-number><user-device-name>Fixture Room</user-device-name><power-mode>PowerOn</power-mode><supports-find-remote>true</supports-find-remote></device-info>"}
        ]
    })json";
    m_runner->complete({0, onlineEnvelope, {}, false});
    QCOMPARE(snapshots.size(), 1);
    QCOMPARE(jsonResult(call(QStringLiteral("GetSnapshot"), {QStringLiteral("SERIAL-A")}))
                 .value(QStringLiteral("address"))
                 .toString(),
             QStringLiteral("192.0.2.30"));

    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 2, 1000);
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

    m_runner->complete({0, onlineEnvelope, {}, false});
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
    const QDBusMessage started = waitForReply(m_proxy->DiscoverDevices());
    QVERIFY2(started.type() != QDBusMessage::ErrorMessage, qPrintable(started.errorMessage()));
    QCOMPARE(jsonResult(started).value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.program, QStringLiteral("rds"));
    QCOMPARE(m_runner->lastRequest.arguments,
             QStringList({QStringLiteral("--json"), QStringLiteral("--quiet"),
                          QStringLiteral("--timeout"), QStringLiteral("3000"),
                          QStringLiteral("discover")}));

    const QByteArray discovered = R"json([
        {"serialNumber":"SERIAL-A","deviceName":"First Fixture","ip":"198.51.100.40"},
        {"serialNumber":"SERIAL-B","deviceName":"Second Fixture","ip":"198.51.100.41"}
    ])json";
    m_runner->complete({0, discovered, {}, false});
    QCOMPARE(changed.size(), 1);
    QJsonArray devices =
        jsonResult(waitForReply(m_proxy->ListDevices())).value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 2);

    const QDBusMessage rediscovery = waitForReply(m_proxy->DiscoverDevices());
    QVERIFY2(rediscovery.type() != QDBusMessage::ErrorMessage,
             qPrintable(rediscovery.errorMessage()));
    const QByteArray moved = R"json([
        {"serialNumber":"SERIAL-A","deviceName":"First Fixture","ip":"198.51.100.42"}
    ])json";
    m_runner->complete({0, moved, {}, false});
    QCOMPARE(changed.size(), 2);
    devices =
        jsonResult(waitForReply(m_proxy->ListDevices())).value(QStringLiteral("devices")).toArray();
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-A"));
    QCOMPARE(devices.at(0).toObject().value(QStringLiteral("address")).toString(),
             QStringLiteral("198.51.100.42"));
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
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 2, 1000);

    QSignalSpy snapshots(m_daemon.get(), &DaemonService::DeviceSnapshotChanged);
    m_runner->complete({1, {}, QByteArrayLiteral("fixture failure"), false});
    const QByteArray standbyEnvelope = R"json({
        "results": [
            {"endpoint":"/query/device-info","status":200,"body":"<device-info><serial-number>SERIAL-B</serial-number><user-device-name>Standby Fixture</user-device-name><power-mode>Ready</power-mode></device-info>"}
        ]
    })json";
    m_runner->complete({0, standbyEnvelope, {}, false});

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
        QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
        QVERIFY(!reply.arguments().front().toString().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), index + 1, 500);
        const QJsonObject result =
            QJsonDocument::fromJson(finished.at(index).at(1).toString().toUtf8()).object();
        QCOMPARE(result.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("success")).toBool(), false);
    }
    QVERIFY(m_runner->requests.isEmpty());

    const QDBusMessage registered = waitForReply(m_proxy->RegisterClient(
        QStringLiteral("capability-widget"), QStringList{QStringLiteral("SERIAL-A")}, 1u));
    QVERIFY2(registered.type() != QDBusMessage::ErrorMessage,
             qPrintable(registered.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);
    const QByteArray capableEnvelope = R"json({
        "results": [
            {"endpoint":"/query/device-info","status":200,"body":"<device-info><serial-number>SERIAL-A</serial-number><user-device-name>Fixture Room</user-device-name><power-mode>PowerOn</power-mode><supports-find-remote>true</supports-find-remote></device-info>"}
        ]
    })json";
    m_runner->complete({0, capableEnvelope, {}, false});
    const QDBusMessage unregistered =
        waitForReply(m_proxy->UnregisterClient(QStringLiteral("capability-widget")));
    QVERIFY2(unregistered.type() != QDBusMessage::ErrorMessage,
             qPrintable(unregistered.errorMessage()));

    const QString findRemote =
        QStringLiteral(R"json({"operation":"keypress","key":"FindRemote"})json");
    const QDBusMessage accepted =
        waitForReply(m_proxy->SendCommand(QStringLiteral("SERIAL-A"), findRemote));
    QVERIFY2(accepted.type() != QDBusMessage::ErrorMessage, qPrintable(accepted.errorMessage()));
    QCOMPARE(m_runner->requests.size(), 2);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("FindRemote")}));
    m_runner->complete({0, QByteArrayLiteral(R"json({"ok":true})json"), {}, false});
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

QTEST_GUILESS_MAIN(DaemonDbusTest)
#include "daemon_dbus_test.moc"

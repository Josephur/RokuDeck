// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/process_runner.h"
#include "daemon/config_store.h"
#include "daemon/daemon_service.h"
#include "support/fake_process_runner.h"

#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace RokuDeck;
using namespace RokuDeck::Test;

class DaemonDbusTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();

    void twoClientsShareOneDeviceInterest();
    void disconnectingBusOwnerRemovesItsInterest();
    void lifecycleTracksClientsAndEnabledAutomations();
    void deviceJsonIsVersionedAndAddressChangesPreserveIdentity();
    void queryFailureDoesNotBlockAnotherDevice();
    void commandsSerializePerDevice();

  private:
    [[nodiscard]] QDBusMessage call(const QString& method,
                                    const QVariantList& arguments = {}) const;
    [[nodiscard]] static QDBusMessage callOnConnection(const QDBusConnection& connection,
                                                       const QString& method,
                                                       const QVariantList& arguments = {});
    [[nodiscard]] static QJsonObject jsonResult(const QDBusMessage& reply);

    QTemporaryDir m_configDirectory;
    QString m_configPath;
    bool m_automationEnabled = true;
    FakeProcessRunner* m_runner = nullptr;
    std::unique_ptr<DaemonService> m_daemon;
};

void DaemonDbusTest::init() {
    QVERIFY2(QDBusConnection::sessionBus().isConnected(),
             "daemon-dbus must run on a private session bus");
    QVERIFY(m_configDirectory.isValid());
    m_automationEnabled = true;
    m_configPath = m_configDirectory.filePath(QString::fromLatin1(QTest::currentTestFunction()) +
                                              QStringLiteral(".rc"));

    auto runner = std::make_unique<FakeProcessRunner>();
    m_runner = runner.get();
    auto config = KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig);
    auto configStore = std::make_unique<ConfigStore>(std::move(config));
    m_daemon = std::make_unique<DaemonService>(
        QDBusConnection::sessionBus(), [this] { return m_automationEnabled; }, std::move(runner),
        std::move(configStore));
    QVERIFY2(m_daemon->registerOnBus(),
             "the test daemon could not own its private session-bus name");
}

void DaemonDbusTest::cleanup() {
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
    QDBusPendingCall pending = connection.asyncCall(request, 2000);
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

void DaemonDbusTest::disconnectingBusOwnerRemovesItsInterest() {
    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());

    QProcess client;
    client.setProgram(QCoreApplication::applicationDirPath() +
                      QStringLiteral("/daemon-client-helper"));
    client.start();
    QVERIFY(client.waitForStarted());
    QByteArray clientOutput;
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (!clientOutput.contains("registered") && waitTimer.elapsed() < 1000) {
        QTest::qWait(10);
        clientOutput.append(client.readAllStandardOutput());
    }
    if (!clientOutput.contains("registered")) {
        const QString failure = QStringLiteral("helper did not register: %1")
                                    .arg(QString::fromUtf8(client.readAllStandardError()));
        client.kill();
        client.waitForFinished();
        QFAIL(qPrintable(failure));
    }
    QCOMPARE(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 1200u);
    QVERIFY(!m_daemon->idleTimerActive());

    client.terminate();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(m_daemon->effectiveInterval(QStringLiteral("SERIAL-A")), 0u, 1000);
    QVERIFY(m_daemon->idleTimerActive());
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

void DaemonDbusTest::commandsSerializePerDevice() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.20")});
    const QString command = QStringLiteral(R"json({"arguments":["keypress","Home"]})json");

    const QDBusMessage first =
        call(QStringLiteral("SendCommand"), {QStringLiteral("SERIAL-A"), command});
    const QDBusMessage second =
        call(QStringLiteral("SendCommand"), {QStringLiteral("SERIAL-A"), command});
    QVERIFY2(first.type() != QDBusMessage::ErrorMessage, qPrintable(first.errorMessage()));
    QVERIFY2(second.type() != QDBusMessage::ErrorMessage, qPrintable(second.errorMessage()));
    QVERIFY(first.arguments().front().toString() != second.arguments().front().toString());
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->callbacks.size(), 1);

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

QTEST_GUILESS_MAIN(DaemonDbusTest)
#include "daemon_dbus_test.moc"

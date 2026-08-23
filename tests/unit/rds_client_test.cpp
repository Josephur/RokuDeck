// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/qprocess_runner.h"
#include "core/rds_client.h"
#include "support/fake_process_runner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace RokuDeck;
using namespace RokuDeck::Test;
using namespace std::chrono_literals;

class RdsClientTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void usesInstalledGrammarAndMergesAllEndpointOutcomes();
    void timeoutsLeaveHeadroomForRdsOwnDeadline();
    void missingRequestedEndpointDegradesDeviceInfoOnlySnapshot();
    void incompatibleVersionDisablesPollingAndExposesDiagnostic();
    void incompatibleGrammarDisablesPollingAndExposesDiagnostic();
    void reportsMalformedEnvelopeAsTypedError();
    void processRunnerCapsCapturedOutput();
    void processRunnerKillsOnTimeoutAndCompletesOnce();
    void destroyingRunnerCancelsAndCompletesOutstandingRequest();
};

void RdsClientTest::usesInstalledGrammarAndMergesAllEndpointOutcomes() {
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    const QString address = QStringLiteral("192.0.2.18");
    client.queryDevice(address, [&](RdsQueryResult result) { received = std::move(result); });

    QCOMPARE(runner.requests.size(), 1);
    QCOMPARE(runner.requests.at(0).program, QStringLiteral("rds"));
    QCOMPARE(runner.requests.at(0).arguments, QStringList({QStringLiteral("--version")}));
    runner.complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});

    QCOMPARE(runner.requests.size(), 2);
    QCOMPARE(runner.requests.at(1).program, QStringLiteral("rds"));
    QCOMPARE(
        runner.requests.at(1).arguments,
        QStringList({QStringLiteral("--json"), QStringLiteral("--quiet"),
                     QStringLiteral("--timeout"), QStringLiteral("4000"), QStringLiteral("--ip"),
                     address, QStringLiteral("device"), QStringLiteral("info")}));
    const QByteArray deviceInfo = R"json({
        "deviceName":"Fixture Room",
        "serialNumber":"SERIAL-FIXTURE-A",
        "powerMode":"PowerOn",
        "supportsSuspend":"true",
        "supportsPrivateListening":"false"
    })json";
    runner.complete({0, deviceInfo, {}, false});

    QCOMPARE(runner.requests.size(), 5);
    const QStringList prefix{QStringLiteral("--json"),    QStringLiteral("--quiet"),
                             QStringLiteral("--timeout"), QStringLiteral("4000"),
                             QStringLiteral("--ip"),      address,
                             QStringLiteral("ecp"),       QStringLiteral("query")};
    QCOMPARE(runner.requests.at(2).arguments,
             prefix + QStringList{QStringLiteral("/query/active-app")});
    QCOMPARE(runner.requests.at(3).arguments,
             prefix + QStringList{QStringLiteral("/query/media-player")});
    QCOMPARE(runner.requests.at(4).arguments,
             prefix + QStringList{QStringLiteral("/query/audio-device")});
    QVERIFY(!received.has_value());

    runner.complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<active-app><app id=\"fixture-app\" type=\"appl\">Fixture App</app></active-app>","status":200,"headers":{}})json"),
         {},
         false});
    runner.complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<player state=\"play\"/>","status":200,"headers":{}})json"),
         {},
         false});
    QVERIFY(!received.has_value());
    runner.complete(
        {0,
         QByteArrayLiteral(
             R"json({"success":true,"data":"<audio-device><volume>23</volume><mute-status>false</mute-status></audio-device>","status":200,"headers":{}})json"),
         {},
         false});

    QVERIFY(received.has_value());
    QVERIFY(received->has_value());
    const DeviceSnapshot& snapshot = received->value();
    QCOMPARE(snapshot.serial, QStringLiteral("SERIAL-FIXTURE-A"));
    QCOMPARE(snapshot.address, address);
    QCOMPARE(snapshot.power, PowerState::On);
    QCOMPARE(snapshot.health, DeviceHealth::Online);
    QVERIFY(!snapshot.partialFailure);
    QVERIFY(snapshot.activeApp.has_value());
    QCOMPARE(snapshot.media, MediaPlaybackState::Playing);
    QCOMPARE(snapshot.volume, std::optional<int>(23));
    QCOMPARE(snapshot.muted, std::optional<bool>(false));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("supports-suspend")));
    // Keypress-based controls exist on every Roku, so the daemon reports them
    // as capabilities; the widget offers nothing the daemon does not vouch for.
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("power")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("navigation")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("playback")));
    // Audio controls are only offered once an audio reading was observed.
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("volume")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("mute")));
}

void RdsClientTest::timeoutsLeaveHeadroomForRdsOwnDeadline() {
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    client.queryDevice(QStringLiteral("192.0.2.30"),
                       [&](RdsQueryResult result) { received = std::move(result); });
    runner.complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    runner.complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Fixture Room","serialNumber":"SERIAL-FIXTURE-A","powerMode":"PowerOn"})json"),
         {},
         false});

    // A borderline WiFi stall must be answered by rds's own HTTP deadline,
    // never by our kill timer racing it: killing rds mid-flight turns a
    // recoverable slow poll into a hard failure and flaps the widget between
    // online and degraded.
    const auto rdsDeadline = [&runner](int index) {
        const int at = runner.requests.at(index).arguments.indexOf(QStringLiteral("--timeout"));
        return runner.requests.at(index).arguments.at(at + 1).toInt();
    };
    for (int index = 1; index < runner.requests.size(); ++index) {
        const int waitMs = rdsDeadline(index);
        QVERIFY2(runner.requests.at(index).timeout.count() > waitMs,
                 qPrintable(QStringLiteral("request %1 kills rds at %2ms but rds waits %3ms")
                                .arg(index)
                                .arg(runner.requests.at(index).timeout.count())
                                .arg(waitMs)));
    }
    QCOMPARE(rdsDeadline(1), 4000);
    QCOMPARE(rdsDeadline(2), 4000);
}

void RdsClientTest::missingRequestedEndpointDegradesDeviceInfoOnlySnapshot() {
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    client.queryDevice(QStringLiteral("198.51.100.24"),
                       [&](RdsQueryResult result) { received = std::move(result); });

    runner.complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    runner.complete(
        {0,
         QByteArrayLiteral(
             R"json({"deviceName":"Fixture Room","serialNumber":"SERIAL-FIXTURE-A","powerMode":"PowerOn"})json"),
         {},
         false});
    QCOMPARE(runner.requests.size(), 5);
    runner.complete({1, {}, QByteArrayLiteral("active app unavailable"), false});
    runner.complete({1, {}, QByteArrayLiteral("media unavailable"), false});
    runner.complete({1, {}, QByteArrayLiteral("audio unavailable"), false});

    QVERIFY(received.has_value());
    QVERIFY(received->has_value());
    const DeviceSnapshot& snapshot = received->value();
    QCOMPARE(snapshot.serial, QStringLiteral("SERIAL-FIXTURE-A"));
    QCOMPARE(snapshot.address, QStringLiteral("198.51.100.24"));
    QCOMPARE(snapshot.power, PowerState::On);
    QCOMPARE(snapshot.health, DeviceHealth::Degraded);
    QVERIFY(snapshot.partialFailure);
    QVERIFY(!snapshot.activeApp.has_value());
    QCOMPARE(snapshot.media, MediaPlaybackState::Unknown);
    QVERIFY(!snapshot.volume.has_value());
    QVERIFY(!snapshot.muted.has_value());
    // Keypress controls survive a degraded poll, but audio controls must not
    // be offered when no audio reading was obtained.
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("power")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("navigation")));
    QVERIFY(snapshot.capabilities.contains(QStringLiteral("playback")));
    QVERIFY(!snapshot.capabilities.contains(QStringLiteral("volume")));
    QVERIFY(!snapshot.capabilities.contains(QStringLiteral("mute")));
}

void RdsClientTest::incompatibleVersionDisablesPollingAndExposesDiagnostic() {
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> first;
    client.queryDevice(QStringLiteral("203.0.113.20"),
                       [&](RdsQueryResult result) { first = std::move(result); });
    runner.complete({0, QByteArrayLiteral("2.0.0\n"), {}, false});

    QVERIFY(first.has_value());
    QVERIFY(!first->has_value());
    QCOMPARE(first->error().code, RdsError::Code::IncompatibleCli);
    QVERIFY(!client.pollingAvailable());
    QVERIFY(client.diagnostic().contains(QStringLiteral("1.0.0")));

    std::optional<RdsQueryResult> second;
    client.queryDevice(QStringLiteral("203.0.113.21"),
                       [&](RdsQueryResult result) { second = std::move(result); });
    QVERIFY(second.has_value());
    QCOMPARE(second->error().code, RdsError::Code::IncompatibleCli);
    QCOMPARE(runner.requests.size(), 1);
}

void RdsClientTest::incompatibleGrammarDisablesPollingAndExposesDiagnostic() {
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    client.queryDevice(QStringLiteral("203.0.113.22"),
                       [&](RdsQueryResult result) { received = std::move(result); });
    runner.complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    runner.complete({1, {}, QByteArrayLiteral("error: unknown command 'device'"), false});

    QVERIFY(received.has_value());
    QVERIFY(!received->has_value());
    QCOMPARE(received->error().code, RdsError::Code::IncompatibleCli);
    QVERIFY(!client.pollingAvailable());
    QVERIFY(client.diagnostic().contains(QStringLiteral("grammar"), Qt::CaseInsensitive));
    QCOMPARE(runner.requests.size(), 2);
}

void RdsClientTest::reportsMalformedEnvelopeAsTypedError() {
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    client.queryDevice(QStringLiteral("203.0.113.31"),
                       [&](RdsQueryResult result) { received = std::move(result); });
    runner.complete({0, QByteArrayLiteral("1.0.0\n"), {}, false});
    runner.complete({0, QByteArrayLiteral("not-json"), {}, false});

    QVERIFY(received.has_value());
    QVERIFY(!received->has_value());
    QCOMPARE(received->error().code, RdsError::Code::MalformedPayload);
}

void RdsClientTest::processRunnerCapsCapturedOutput() {
    QProcessRunner runner;
    ProcessRequest request;
    request.program =
        QCoreApplication::applicationDirPath() + QStringLiteral("/rds-process-helper");
    request.arguments = {QStringLiteral("--emit-output")};
    request.timeout = 2s;

    std::optional<ProcessResult> received;
    QEventLoop loop;
    runner.start(request, [&](ProcessResult result) {
        received = std::move(result);
        loop.quit();
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(received.has_value());
    QCOMPARE(received->exitCode, 0);
    QCOMPARE(received->standardOutput.size(), QProcessRunner::maximumCapturedBytes());
    QCOMPARE(received->standardError.size(), QProcessRunner::maximumCapturedBytes());
}

void RdsClientTest::processRunnerKillsOnTimeoutAndCompletesOnce() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString pidFilePath = temporaryDirectory.filePath(QStringLiteral("timeout-helper.pid"));
    QProcessRunner runner;
    ProcessRequest request;
    request.program =
        QCoreApplication::applicationDirPath() + QStringLiteral("/rds-process-helper");
    request.arguments = {QStringLiteral("--hang"), QStringLiteral("--pid-file"), pidFilePath};
    request.timeout = 100ms;

    int callbackCount = 0;
    bool childExitedAtCallback = false;
    std::optional<ProcessResult> received;
    QEventLoop loop;
    runner.start(request, [&](ProcessResult result) {
        ++callbackCount;
        QFile pidFile(pidFilePath);
        QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const QByteArray pid = pidFile.readAll().trimmed();
        childExitedAtCallback =
            !QFileInfo::exists(QStringLiteral("/proc/") + QString::fromLatin1(pid));
        received = std::move(result);
        loop.quit();
    });
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFilePath), 400);
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QTest::qWait(100);

    QVERIFY(received.has_value());
    QVERIFY(received->timedOut);
    QVERIFY(childExitedAtCallback);
    QCOMPARE(callbackCount, 1);
    QVERIFY2(elapsed.elapsed() < 1000, "timeout completion exceeded its finite termination bound");
}

void RdsClientTest::destroyingRunnerCancelsAndCompletesOutstandingRequest() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString pidFilePath = temporaryDirectory.filePath(QStringLiteral("destroy-helper.pid"));
    int callbackCount = 0;
    bool childExitedAtCallback = false;
    std::optional<ProcessResult> received;

    auto runner = std::make_unique<QProcessRunner>();
    ProcessRequest request;
    request.program =
        QCoreApplication::applicationDirPath() + QStringLiteral("/rds-process-helper");
    request.arguments = {QStringLiteral("--hang"), QStringLiteral("--pid-file"), pidFilePath};
    request.timeout = 60s;
    runner->start(request, [&](ProcessResult result) {
        ++callbackCount;
        QFile pidFile(pidFilePath);
        QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const QByteArray pid = pidFile.readAll().trimmed();
        childExitedAtCallback =
            !QFileInfo::exists(QStringLiteral("/proc/") + QString::fromLatin1(pid));
        received = std::move(result);
    });
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFilePath), 1000);

    QElapsedTimer elapsed;
    elapsed.start();
    runner.reset();

    QCOMPARE(callbackCount, 1);
    QVERIFY(received.has_value());
    QCOMPARE(received->exitCode, -1);
    QVERIFY(!received->timedOut);
    QVERIFY(childExitedAtCallback);
    QVERIFY2(elapsed.elapsed() <= QProcessRunner::terminationGracePeriod().count() + 500,
             "runner destruction exceeded its finite termination bound");
}

QTEST_GUILESS_MAIN(RdsClientTest)
#include "rds_client_test.moc"

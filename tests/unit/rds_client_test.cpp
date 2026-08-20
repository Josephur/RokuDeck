// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/qprocess_runner.h"
#include "core/rds_client.h"
#include "support/fake_process_runner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QtTest>

using namespace RokuDeck;
using namespace RokuDeck::Test;
using namespace std::chrono_literals;

class RdsClientTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void usesArgumentsWithoutShell();
    void mergesPartialQueryResultsBeforeNormalizing();
    void reportsMalformedEnvelopeAsTypedError();
    void processRunnerCapsCapturedOutput();
    void processRunnerKillsOnTimeoutAndCompletesOnce();
};

void RdsClientTest::usesArgumentsWithoutShell()
{
    FakeProcessRunner runner;
    RdsClient client(&runner);
    client.queryDevice(QStringLiteral("192.0.2.18"), [](auto) {});

    QCOMPARE(runner.lastRequest.program, QStringLiteral("rds"));
    QCOMPARE(runner.lastRequest.arguments.first(4),
             QStringList({QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--timeout"),
                          QStringLiteral("3000")}));
    QVERIFY(runner.lastRequest.arguments.contains(QStringLiteral("192.0.2.18")));
}

void RdsClientTest::mergesPartialQueryResultsBeforeNormalizing()
{
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    client.queryDevice(QStringLiteral("198.51.100.24"), [&](RdsQueryResult result) { received = std::move(result); });

    const QByteArray envelope = R"json({
        "results": [
            {"endpoint":"/query/device-info","status":200,"body":"<device-info><serial-number>SERIAL-FIXTURE-A</serial-number><user-device-name>Fixture Room</user-device-name><power-mode>Ready</power-mode></device-info>"},
            {"endpoint":"/query/active-app","status":200,"body":"<active-app><app id=\"fixture-app\" type=\"appl\">Fixture App</app></active-app>"},
            {"endpoint":"/query/media-player","status":200,"body":"<player state=\"play\"/>"},
            {"endpoint":"/query/audio-device","status":200,"body":"<audio-device><volume>23</volume><mute-status>false</mute-status></audio-device>"}
        ]
    })json";
    runner.complete({0, envelope, {}, false});

    QVERIFY(received.has_value());
    QVERIFY(received->has_value());
    const DeviceSnapshot &snapshot = received->value();
    QCOMPARE(snapshot.serial, QStringLiteral("SERIAL-FIXTURE-A"));
    QCOMPARE(snapshot.address, QStringLiteral("198.51.100.24"));
    QCOMPARE(snapshot.power, PowerState::Standby);
    QCOMPARE(snapshot.health, DeviceHealth::Standby);
    QVERIFY(!snapshot.activeApp.has_value());
    QCOMPARE(snapshot.media, MediaPlaybackState::Unknown);
    QVERIFY(!snapshot.volume.has_value());
    QVERIFY(!snapshot.muted.has_value());
}

void RdsClientTest::reportsMalformedEnvelopeAsTypedError()
{
    FakeProcessRunner runner;
    RdsClient client(&runner);
    std::optional<RdsQueryResult> received;
    client.queryDevice(QStringLiteral("203.0.113.31"), [&](RdsQueryResult result) { received = std::move(result); });
    runner.complete({0, QByteArrayLiteral("not-json"), {}, false});

    QVERIFY(received.has_value());
    QVERIFY(!received->has_value());
    QCOMPARE(received->error().code, RdsError::Code::MalformedEnvelope);
}

void RdsClientTest::processRunnerCapsCapturedOutput()
{
    QProcessRunner runner;
    ProcessRequest request;
    request.program = QCoreApplication::applicationDirPath() + QStringLiteral("/rds-process-helper");
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

void RdsClientTest::processRunnerKillsOnTimeoutAndCompletesOnce()
{
    QProcessRunner runner;
    ProcessRequest request;
    request.program = QCoreApplication::applicationDirPath() + QStringLiteral("/rds-process-helper");
    request.arguments = {QStringLiteral("--hang")};
    request.timeout = 20ms;

    int callbackCount = 0;
    std::optional<ProcessResult> received;
    QEventLoop loop;
    runner.start(request, [&](ProcessResult result) {
        ++callbackCount;
        received = std::move(result);
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QTest::qWait(100);

    QVERIFY(received.has_value());
    QVERIFY(received->timedOut);
    QCOMPARE(callbackCount, 1);
}

QTEST_GUILESS_MAIN(RdsClientTest)
#include "rds_client_test.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/config_store.h"
#include "daemon/daemon_service.h"
#include "dbus_client.h"
#include "support/fake_process_runner.h"

#include <KSharedConfig>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace RokuDeck;
using namespace RokuDeck::Test;

namespace {

struct CliResult {
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

[[nodiscard]] bool waitForProcess(QProcess& process, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    return process.state() == QProcess::NotRunning;
}

[[nodiscard]] QJsonDocument parseJson(const QByteArray& bytes) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }
    return document;
}

} // namespace

class RokuDeckCtlTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();

    void jsonDeviceListIsMachineReadable();
    void jsonStatusPreservesSchemaVersion();
    void jsonDiscoveryUsesDaemonContract();
    void jsonCommandWaitsForDaemonCompletion();
    void quietSuppressesSuccessfulOutput();
    void invalidUsageHasDocumentedExitCode();
    void unavailableServiceHasDocumentedExitCode();
    void unknownDeviceHasDocumentedExitCode();
    void unsupportedCommandHasDocumentedExitCode();
    void transportFailureHasDocumentedExitCode();
    void typedDbusErrorsHaveDocumentedExitCodes();

  private:
    [[nodiscard]] static QString rokudeckctlPath();
    [[nodiscard]] static CliResult finishCli(QProcess& process);
    [[nodiscard]] static CliResult runCli(const QStringList& arguments);
    [[nodiscard]] std::unique_ptr<QProcess> startCli(const QStringList& arguments);

    QTemporaryDir m_configDirectory;
    FakeProcessRunner* m_runner = nullptr;
    std::unique_ptr<DaemonService> m_daemon;
};

void RokuDeckCtlTest::init() {
    QVERIFY2(QDBusConnection::sessionBus().isConnected(),
             "rokudeckctl tests must run on a private session bus");
    QVERIFY(m_configDirectory.isValid());

    auto runner = std::make_unique<FakeProcessRunner>();
    m_runner = runner.get();
    const QString configPath = m_configDirectory.filePath(
        QString::fromLatin1(QTest::currentTestFunction()) + QStringLiteral(".rc"));
    auto config = KSharedConfig::openConfig(configPath, KConfig::SimpleConfig);
    m_daemon = std::make_unique<DaemonService>(
        QDBusConnection::sessionBus(), [] { return true; }, std::move(runner),
        std::make_unique<ConfigStore>(std::move(config)));
    QVERIFY2(m_daemon->registerOnBus(), "test daemon could not own the private D-Bus service");
}

void RokuDeckCtlTest::cleanup() {
    m_daemon.reset();
    m_runner = nullptr;
    QCoreApplication::processEvents();
}

QString RokuDeckCtlTest::rokudeckctlPath() {
    return QStringLiteral(ROKUDECKCTL_PATH);
}

CliResult RokuDeckCtlTest::finishCli(QProcess& process) {
    if (!waitForProcess(process)) {
        process.kill();
        process.waitForFinished();
        return {-1, process.readAllStandardOutput(),
                QByteArrayLiteral("rokudeckctl timed out\n") + process.readAllStandardError()};
    }
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

CliResult RokuDeckCtlTest::runCli(const QStringList& arguments) {
    QProcess process;
    process.start(rokudeckctlPath(), arguments);
    if (!process.waitForStarted()) {
        return {-1, {}, process.errorString().toUtf8()};
    }
    return finishCli(process);
}

std::unique_ptr<QProcess> RokuDeckCtlTest::startCli(const QStringList& arguments) {
    auto process = std::make_unique<QProcess>();
    process->start(rokudeckctlPath(), arguments);
    if (!process->waitForStarted()) {
        return {};
    }
    return process;
}

void RokuDeckCtlTest::jsonDeviceListIsMachineReadable() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-TEST"), QStringLiteral("Fixture Room"),
                            QStringLiteral("192.0.2.10")});

    const CliResult result = runCli({QStringLiteral("--json"), QStringLiteral("devices")});

    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.standardError, QByteArray{});
    const QJsonDocument document = parseJson(result.standardOutput);
    QVERIFY(document.isArray());
    QCOMPARE(document.array().size(), 1);
    QCOMPARE(document.array().at(0).toObject().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-TEST"));
}

void RokuDeckCtlTest::jsonStatusPreservesSchemaVersion() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-TEST"), QStringLiteral("Fixture Room"),
                            QStringLiteral("192.0.2.10")});

    const CliResult result =
        runCli({QStringLiteral("--json"), QStringLiteral("status"), QStringLiteral("SERIAL-TEST")});

    QCOMPARE(result.exitCode, 0);
    const QJsonDocument document = parseJson(result.standardOutput);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(document.object().value(QStringLiteral("serial")).toString(),
             QStringLiteral("SERIAL-TEST"));
}

void RokuDeckCtlTest::jsonDiscoveryUsesDaemonContract() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-TEST"), QStringLiteral("Fixture Room"),
                            QStringLiteral("192.0.2.10")});

    const CliResult result = runCli({QStringLiteral("--json"), QStringLiteral("discover")});

    QCOMPARE(result.exitCode, 0);
    const QJsonDocument document = parseJson(result.standardOutput);
    QVERIFY(document.isArray());
    QCOMPARE(document.array().size(), 1);
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.program, QStringLiteral("rds"));
    QCOMPARE(m_runner->lastRequest.arguments,
             QStringList({QStringLiteral("--json"), QStringLiteral("--quiet"),
                          QStringLiteral("--timeout"), QStringLiteral("3000"),
                          QStringLiteral("discover")}));
}

void RokuDeckCtlTest::jsonCommandWaitsForDaemonCompletion() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-TEST"), QStringLiteral("Fixture Room"),
                            QStringLiteral("192.0.2.10")});
    std::unique_ptr<QProcess> cli =
        startCli({QStringLiteral("--json"), QStringLiteral("command"),
                  QStringLiteral("SERIAL-TEST"), QStringLiteral("Home")});
    QVERIFY2(cli, "rokudeckctl did not start");
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);

    m_runner->complete({0, QByteArrayLiteral("{}"), {}, false});
    const CliResult result = finishCli(*cli);

    QCOMPARE(result.exitCode, 0);
    const QJsonDocument document = parseJson(result.standardOutput);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(document.object().value(QStringLiteral("success")).toBool(), true);
    QVERIFY(!document.object().value(QStringLiteral("requestId")).toString().isEmpty());
}

void RokuDeckCtlTest::quietSuppressesSuccessfulOutput() {
    const CliResult result = runCli({QStringLiteral("--quiet"), QStringLiteral("devices")});

    QCOMPARE(result.exitCode, 0);
    QCOMPARE(result.standardOutput, QByteArray{});
    QCOMPARE(result.standardError, QByteArray{});
}

void RokuDeckCtlTest::invalidUsageHasDocumentedExitCode() {
    const CliResult result = runCli({QStringLiteral("status")});

    QCOMPARE(result.exitCode, 2);
    QVERIFY(!result.standardError.isEmpty());
}

void RokuDeckCtlTest::unavailableServiceHasDocumentedExitCode() {
    m_daemon.reset();
    m_runner = nullptr;
    QCoreApplication::processEvents();

    const CliResult result = runCli({QStringLiteral("devices")});

    QCOMPARE(result.exitCode, 3);
    QVERIFY(!result.standardError.isEmpty());
}

void RokuDeckCtlTest::unknownDeviceHasDocumentedExitCode() {
    const CliResult result = runCli(
        {QStringLiteral("--json"), QStringLiteral("status"), QStringLiteral("MISSING-DEVICE")});

    QCOMPARE(result.exitCode, 4);
    QVERIFY(result.standardOutput.isEmpty());
    QVERIFY(!result.standardError.isEmpty());
}

void RokuDeckCtlTest::unsupportedCommandHasDocumentedExitCode() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-TEST"), QStringLiteral("Fixture Room"),
                            QStringLiteral("192.0.2.10")});

    const CliResult result = runCli({QStringLiteral("command"), QStringLiteral("SERIAL-TEST"),
                                     QStringLiteral("DefinitelyNotSupported")});

    QCOMPARE(result.exitCode, 5);
    QVERIFY(result.standardOutput.isEmpty());
    QVERIFY(!result.standardError.isEmpty());
}

void RokuDeckCtlTest::transportFailureHasDocumentedExitCode() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-TEST"), QStringLiteral("Fixture Room"),
                            QStringLiteral("192.0.2.10")});
    std::unique_ptr<QProcess> cli = startCli(
        {QStringLiteral("command"), QStringLiteral("SERIAL-TEST"), QStringLiteral("Home")});
    QVERIFY2(cli, "rokudeckctl did not start");
    QTRY_COMPARE_WITH_TIMEOUT(m_runner->requests.size(), 1, 1000);

    m_runner->complete({1, {}, QByteArrayLiteral("fixture transport failure"), false});
    const CliResult result = finishCli(*cli);

    QCOMPARE(result.exitCode, 7);
    QVERIFY(result.standardOutput.isEmpty());
    QVERIFY(!result.standardError.isEmpty());
}

void RokuDeckCtlTest::typedDbusErrorsHaveDocumentedExitCodes() {
    QCOMPARE(
        Cli::exitCodeForError(QStringLiteral("io.github.josephur.RokuDeck1.Error.UnknownDevice")),
        Cli::ExitCode::UnknownDevice);
    QCOMPARE(Cli::exitCodeForError(
                 QStringLiteral("io.github.josephur.RokuDeck1.Error.UnsupportedCommand")),
             Cli::ExitCode::UnsupportedCommand);
    QCOMPARE(
        Cli::exitCodeForError(QStringLiteral("io.github.josephur.RokuDeck1.Error.SafetyRejected")),
        Cli::ExitCode::SafetyRejected);
}

QTEST_GUILESS_MAIN(RokuDeckCtlTest)

#include "rokudeckctl_test.moc"

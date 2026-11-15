// SPDX-License-Identifier: GPL-3.0-or-later

// Loopback tests for the M9a peer listener (network-peers design §2-§4): the
// pairing handshake over a real TCP socket, token-gated snapshots, and every
// typed failure the design commits to. The client side of each exchange is a
// raw QTcpSocket, so the wire format itself is what gets pinned here.

#include "daemon/config_store.h"
#include "daemon/peer_client.h"
#include "daemon/peer_server.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace RokuDeck;

namespace {

constexpr auto kMachineId = "aa11bb22cc33dd44ee55ff6677889900";
constexpr auto kHostName = "test-host";
constexpr auto kControllerId = "controller-machine-id";
constexpr auto kControllerHost = "controller-host";

[[nodiscard]] QJsonObject parseLine(const QByteArray& line) {
    return QJsonDocument::fromJson(line).object();
}

} // namespace

class PeerServerTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void init();
    void cleanup();

    void pairIssuesAStoredTokenAndAnswersSnapshots();
    void pairingCodesAreSingleUseAndExpiring();
    void badRequestsAreTypedErrorsAndTheConnectionSurvives();
    void revokedControllersLoseAccess();
    void rePairingRotatesTheToken();
    void oversizedLinesDropTheConnection();
    void unknownSnapshotStateIsHonestInEnvelope();
    void pairClientReceivesIdentityAndToken();

  private:
    void startServer();
    // One request/response exchange on the reused loopback connection (typed
    // errors deliberately keep it open, so one socket serves the whole test).
    [[nodiscard]] QJsonObject exchange(const QJsonObject& request);
    [[nodiscard]] QJsonObject pairAndStoreController();
    // The QtTest macros expand to bare `return;`, so every one of them lives
    // in a void helper; the value-returning exchange() only composes them.
    void ensureConnected();
    void ensureResponseLine();

    QTemporaryDir m_directory;
    std::unique_ptr<ConfigStore> m_store;
    std::unique_ptr<PeerServer> m_server;
    std::unique_ptr<QTcpSocket> m_socket;
    QDateTime m_now = QDateTime(QDate(2026, 11, 14), QTime(9, 0));
};

void PeerServerTest::init() {
    QVERIFY(m_directory.isValid());
    startServer();
}

void PeerServerTest::cleanup() {
    m_socket.reset();
    m_server.reset();
    m_store.reset();
}

void PeerServerTest::startServer() {
    auto config = KSharedConfig::openConfig(
        m_directory.path() + QStringLiteral("/peer.rc"), KConfig::SimpleConfig);
    m_store = std::make_unique<ConfigStore>(std::move(config));
    DeviceSnapshot fixture;
    fixture.serial = QStringLiteral("LOCAL-PC");
    fixture.displayName = QStringLiteral("This PC");
    fixture.health = DeviceHealth::Online;
    fixture.volume = 42;
    fixture.muted = false;
    fixture.playing = true;
    fixture.capturedAt = m_now;
    fixture.capabilities = QSet<QString>{QStringLiteral("volume"), QStringLiteral("mute")};
    m_server = std::make_unique<PeerServer>(
        m_store.get(), [fixture] { return fixture; },
        [this] { return m_now; }, QLatin1String(kMachineId), QLatin1String(kHostName));
    QVERIFY2(m_server->start(0), "the loopback listener must bind an OS-assigned port");
    QVERIFY(m_server->listeningPort() > 0);
}

void PeerServerTest::ensureConnected() {
    if (m_socket == nullptr) {
        m_socket = std::make_unique<QTcpSocket>();
        m_socket->connectToHost(QHostAddress::LocalHost, m_server->listeningPort());
    }
    QTRY_COMPARE_WITH_TIMEOUT(m_socket->state(), QAbstractSocket::ConnectedState, 2000);
}

void PeerServerTest::ensureResponseLine() {
    QTRY_VERIFY_WITH_TIMEOUT(m_socket->canReadLine(), 2000);
}

QJsonObject PeerServerTest::exchange(const QJsonObject& request) {
    ensureConnected();
    m_socket->write(QJsonDocument(request).toJson(QJsonDocument::Compact) +
                    QByteArrayLiteral("\n"));
    ensureResponseLine();
    return parseLine(m_socket->readLine());
}

QJsonObject PeerServerTest::pairAndStoreController() {
    const QString code = m_server->mintPairingCode();
    const QJsonObject response =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("req-pair")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("machineId"), QLatin1String(kControllerId)},
                             {QStringLiteral("hostname"), QLatin1String(kControllerHost)}});
    return response;
}

void PeerServerTest::pairIssuesAStoredTokenAndAnswersSnapshots() {
    const QJsonObject paired = pairAndStoreController();
    QCOMPARE(paired.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(paired.value(QStringLiteral("id")).toString(), QStringLiteral("req-pair"));
    const QJsonObject peer = paired.value(QStringLiteral("peer")).toObject();
    QCOMPARE(peer.value(QStringLiteral("machineId")).toString(), QLatin1String(kMachineId));
    QCOMPARE(peer.value(QStringLiteral("hostname")).toString(), QLatin1String(kHostName));
    QVERIFY(!paired.value(QStringLiteral("token")).toString().isEmpty());

    // The controller is persisted with exactly one token.
    const QList<PairedController> controllers = m_store->loadPairedControllers();
    QCOMPARE(controllers.size(), 1);
    QCOMPARE(controllers.at(0).machineId, QLatin1String(kControllerId));
    QCOMPARE(controllers.at(0).token, paired.value(QStringLiteral("token")).toString());

    // The token pulls the snapshot envelope (design §4 shape).
    const QJsonObject snapshot =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("req-snap")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")},
                             {QStringLiteral("token"),
                              paired.value(QStringLiteral("token")).toString()}});
    QCOMPARE(snapshot.value(QStringLiteral("ok")).toBool(), true);
    const QJsonObject envelope = snapshot.value(QStringLiteral("snapshot")).toObject();
    QCOMPARE(envelope.value(QStringLiteral("serial")).toString(),
             QStringLiteral("PC-aa11bb22"));
    QCOMPARE(envelope.value(QStringLiteral("displayName")).toString(),
             QLatin1String(kHostName));
    QCOMPARE(envelope.value(QStringLiteral("health")).toString(), QStringLiteral("online"));
    QCOMPARE(envelope.value(QStringLiteral("volume")).toInt(), 42);
    QCOMPARE(envelope.value(QStringLiteral("muted")).toBool(), false);
    QCOMPARE(envelope.value(QStringLiteral("playing")).toBool(), true);
    QCOMPARE(envelope.value(QStringLiteral("capturedAt")).toString(),
             m_now.toString(Qt::ISODateWithMs));
    const QJsonArray capabilities =
        envelope.value(QStringLiteral("capabilities")).toArray();
    QCOMPARE(capabilities.size(), 2);
    QCOMPARE(capabilities.at(0).toString(), QStringLiteral("mute"));
    QCOMPARE(capabilities.at(1).toString(), QStringLiteral("volume"));
}

void PeerServerTest::pairingCodesAreSingleUseAndExpiring() {
    const QString code = m_server->mintPairingCode();
    const QJsonObject paired =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("one")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("machineId"), QStringLiteral("first-machine")}});
    QCOMPARE(paired.value(QStringLiteral("ok")).toBool(), true);

    // Burned on use.
    const QJsonObject reused =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("two")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), code},
                             {QStringLiteral("machineId"), QStringLiteral("second-machine")}});
    QCOMPARE(reused.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(reused.value(QStringLiteral("error")).toString(), QStringLiteral("invalidCode"));

    // Expired by the injected clock.
    const QString expiredCode = m_server->mintPairingCode();
    m_now = m_now.addSecs(11 * 60);
    const QJsonObject late =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("three")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), expiredCode},
                             {QStringLiteral("machineId"), QStringLiteral("third-machine")}});
    QCOMPARE(late.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(late.value(QStringLiteral("error")).toString(), QStringLiteral("codeExpired"));
}

void PeerServerTest::badRequestsAreTypedErrorsAndTheConnectionSurvives() {
    const QJsonObject unauthorized =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("a")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")},
                             {QStringLiteral("token"), QStringLiteral("bogus-token")}});
    QCOMPARE(unauthorized.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(unauthorized.value(QStringLiteral("error")).toString(),
             QStringLiteral("unauthorized"));

    const QJsonObject unknownOp =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("b")},
                             {QStringLiteral("op"), QStringLiteral("launchApp")}});
    QCOMPARE(unknownOp.value(QStringLiteral("error")).toString(),
             QStringLiteral("unknownOperation"));

    const QJsonObject wrongSchema =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 2},
                             {QStringLiteral("id"), QStringLiteral("c")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")}});
    QCOMPARE(wrongSchema.value(QStringLiteral("error")).toString(),
             QStringLiteral("wrongSchemaVersion"));

    const QJsonObject badPair =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("d")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), QStringLiteral("ABCD2345")},
                             {QStringLiteral("machineId"), QString()}});
    QCOMPARE(badPair.value(QStringLiteral("error")).toString(), QStringLiteral("malformed"));

    m_socket->write(QByteArrayLiteral("this is not json\n"));
    QTRY_VERIFY_WITH_TIMEOUT(m_socket->canReadLine(), 2000);
    QCOMPARE(parseLine(m_socket->readLine()).value(QStringLiteral("error")).toString(),
             QStringLiteral("malformed"));

    // The connection survived every typed error; a valid snapshot with a
    // stored token still works on the same socket.
    const QJsonObject paired = pairAndStoreController();
    const QJsonObject snapshot =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("e")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")},
                             {QStringLiteral("token"),
                              paired.value(QStringLiteral("token")).toString()}});
    QCOMPARE(snapshot.value(QStringLiteral("ok")).toBool(), true);
    m_socket->disconnectFromHost();
    m_socket.reset();
}

void PeerServerTest::revokedControllersLoseAccess() {
    const QJsonObject paired = pairAndStoreController();
    const QString token = paired.value(QStringLiteral("token")).toString();

    // Revocation is a config edit (the D-Bus layer does the same): the token
    // stops working immediately, with no grace period.
    m_store->savePairedControllers({});
    const QJsonObject snapshot =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("after-revoke")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")},
                             {QStringLiteral("token"), token}});
    QCOMPARE(snapshot.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(snapshot.value(QStringLiteral("error")).toString(), QStringLiteral("unauthorized"));
}

void PeerServerTest::rePairingRotatesTheToken() {
    const QString firstCode = m_server->mintPairingCode();
    const QJsonObject first =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("first")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), firstCode},
                             {QStringLiteral("machineId"), QLatin1String(kControllerId)}});
    const QString firstToken = first.value(QStringLiteral("token")).toString();
    QVERIFY(!firstToken.isEmpty());

    const QString secondCode = m_server->mintPairingCode();
    const QJsonObject second =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("second")},
                             {QStringLiteral("op"), QStringLiteral("pair")},
                             {QStringLiteral("code"), secondCode},
                             {QStringLiteral("machineId"), QLatin1String(kControllerId)}});
    const QString secondToken = second.value(QStringLiteral("token")).toString();
    QVERIFY(!secondToken.isEmpty());
    QVERIFY(firstToken != secondToken);

    // Still one stored controller — the machine identity is the key.
    QCOMPARE(m_store->loadPairedControllers().size(), 1);
    QCOMPARE(m_store->loadPairedControllers().at(0).token, secondToken);

    // The rotated-out token no longer authorizes anything.
    const QJsonObject withOldToken =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("old")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")},
                             {QStringLiteral("token"), firstToken}});
    QCOMPARE(withOldToken.value(QStringLiteral("error")).toString(),
             QStringLiteral("unauthorized"));
}

void PeerServerTest::oversizedLinesDropTheConnection() {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, m_server->listeningPort());
    QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QAbstractSocket::ConnectedState, 2000);
    // One line far past the framing cap: no typed error is possible, so the
    // listener drops the peer instead of buffering unboundedly.
    socket.write(QByteArray(static_cast<int>(80 * 1024), 'x') + QByteArrayLiteral("\n"));
    QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QAbstractSocket::UnconnectedState, 5000);
}

void PeerServerTest::unknownSnapshotStateIsHonestInEnvelope() {
    // A peer whose LOCAL-PC has not polled yet (all-Unknown snapshot) still
    // answers: the envelope says "unknown"/null instead of hiding.
    m_server->stop();
    auto config = KSharedConfig::openConfig(
        m_directory.path() + QStringLiteral("/peer.rc"), KConfig::SimpleConfig);
    m_store = std::make_unique<ConfigStore>(std::move(config));
    m_server = std::make_unique<PeerServer>(
        m_store.get(), [] { return DeviceSnapshot{}; },
        [this] { return m_now; }, QLatin1String(kMachineId), QLatin1String(kHostName));
    QVERIFY(m_server->start(0));

    const QJsonObject paired = pairAndStoreController();
    const QJsonObject snapshot =
        exchange(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("id"), QStringLiteral("fresh")},
                             {QStringLiteral("op"), QStringLiteral("snapshot")},
                             {QStringLiteral("token"),
                              paired.value(QStringLiteral("token")).toString()}});
    const QJsonObject envelope = snapshot.value(QStringLiteral("snapshot")).toObject();
    QCOMPARE(envelope.value(QStringLiteral("health")).toString(), QStringLiteral("unknown"));
    QCOMPARE(envelope.value(QStringLiteral("volume")), QJsonValue());
    QCOMPARE(envelope.value(QStringLiteral("muted")), QJsonValue());
    QCOMPARE(envelope.value(QStringLiteral("playing")), QJsonValue());
    QVERIFY(!envelope.contains(QStringLiteral("capturedAt")));
}

void PeerServerTest::pairClientReceivesIdentityAndToken() {
    // The controller-side client drives the same loopback listener end to end.
    PeerClient client;
    const QString code = m_server->mintPairingCode();
    const PeerPairOutcome outcome = client.pair(QStringLiteral("127.0.0.1"),
                                                m_server->listeningPort(), code,
                                                QLatin1String(kControllerId),
                                                QLatin1String(kControllerHost));
    QVERIFY2(outcome.ok, qPrintable(outcome.errorMessage));
    QCOMPARE(outcome.machineId, QLatin1String(kMachineId));
    QCOMPARE(outcome.displayName, QLatin1String(kHostName));
    QVERIFY(!outcome.token.isEmpty());
    QCOMPARE(m_store->loadPairedControllers().size(), 1);

    // A wrong code resolves to the peer's typed refusal through the client.
    const PeerPairOutcome refused =
        client.pair(QStringLiteral("127.0.0.1"), m_server->listeningPort(),
                    QStringLiteral("WRONGCOD"), QLatin1String(kControllerId),
                    QLatin1String(kControllerHost));
    QVERIFY(!refused.ok);
    QCOMPARE(refused.errorCode, QStringLiteral("invalidCode"));

    // An unreachable peer resolves to a transport failure, not a hang.
    const PeerPairOutcome unreachable =
        client.pair(QStringLiteral("127.0.0.1"), 1, QStringLiteral("ABCD2345"),
                    QLatin1String(kControllerId), QLatin1String(kControllerHost));
    QVERIFY(!unreachable.ok);
    QCOMPARE(unreachable.errorCode, QStringLiteral("connectFailed"));
}

QTEST_GUILESS_MAIN(PeerServerTest)
#include "peer_server_test.moc"

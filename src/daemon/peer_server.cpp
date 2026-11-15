// SPDX-License-Identifier: GPL-3.0-or-later
#include "peer_server.h"

#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace RokuDeck {
namespace {

constexpr qsizetype kMaximumLineBytes = 64 * 1024;
constexpr int kIdleTimeoutMs = 60 * 1000;
constexpr int kPairingCodeLength = 8;
constexpr qint64 kPairingCodeTtlMs = 10 * 60 * 1000;
constexpr qsizetype kMaxPairedControllers = 16;
constexpr int kMaximumIdentityLength = 64;
// Unambiguous alphabet: no 0/O/1/I — codes are read aloud across a room.
const QLatin1String kPairingCodeAlphabet("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");

// /etc/machine-id is the Linux stable identity; read once and cached for the
// process lifetime — an identity that changed mid-run would orphan every
// pairing decided against the old one.
QString cachedMachineId() {
    static const QString cached = [] {
        QFile machineIdFile(QStringLiteral("/etc/machine-id"));
        if (machineIdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content =
                QString::fromUtf8(machineIdFile.readAll()).trimmed();
            if (!content.isEmpty()) {
                return content;
            }
        }
        // Session-local fallback: pairing works, but does not survive a
        // reboot. See peer_server.h for the honesty note.
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }();
    return cached;
}

// Identities travel inside JSON envelopes and D-Bus method calls; anything
// outside printable non-space ASCII is refused rather than escaped through.
[[nodiscard]] bool isUsableIdentity(const QString& value) {
    if (value.isEmpty() || value.size() > kMaximumIdentityLength) {
        return false;
    }
    return std::all_of(value.cbegin(), value.cend(), [](QChar character) {
        return character.toLatin1() > 0x20 && character.toLatin1() < 0x7F;
    });
}

[[nodiscard]] QString healthName(DeviceHealth health) {
    switch (health) {
    case DeviceHealth::Online:
        return QStringLiteral("online");
    case DeviceHealth::Standby:
        return QStringLiteral("standby");
    case DeviceHealth::Degraded:
        return QStringLiteral("degraded");
    case DeviceHealth::Offline:
        return QStringLiteral("offline");
    case DeviceHealth::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

QString localMachineId() {
    return cachedMachineId();
}

QString localHostName() {
    const QString name = QHostInfo::localHostName();
    return name.isEmpty() ? QStringLiteral("unnamed-host") : name;
}

QString peerSerialForMachineId(const QString& machineId) {
    return QStringLiteral("PC-") + machineId.left(8);
}

PeerServer::PeerServer(ConfigStore* config, SnapshotProvider snapshotProvider,
                       WallClock wallClock, QString machineId, QString hostName,
                       ActionExecutor actionExecutor, CeilingProvider ceilingProvider,
                       QObject* parent)
    : QObject(parent), m_config(config),
      m_snapshotProvider(std::move(snapshotProvider)),
      m_actionExecutor(std::move(actionExecutor)),
      m_ceilingProvider(std::move(ceilingProvider)),
      m_wallClock(wallClock ? std::move(wallClock)
                            : [] { return QDateTime::currentDateTime(); }),
      m_machineId(machineId.isEmpty() ? cachedMachineId() : std::move(machineId)),
      m_hostName(hostName.isEmpty() ? localHostName() : std::move(hostName)) {
    connect(&m_server, &QTcpServer::newConnection, this, &PeerServer::acceptConnection);
}

PeerServer::~PeerServer() {
    stop();
}

bool PeerServer::start(quint16 port) {
    stop();
    if (!m_server.listen(QHostAddress::Any, port)) {
        return false;
    }
    return true;
}

void PeerServer::stop() {
    m_server.close();
    // Copy before dropping: dropSocket mutates m_sockets.
    const QSet<QTcpSocket*> sockets = m_sockets;
    for (QTcpSocket* socket : sockets) {
        socket->disconnectFromHost();
        dropSocket(socket);
    }
    // Expired codes are pruned here too: a stopped listener must not keep
    // codes alive that outlive their TTL clock being asked about.
    const QDateTime now = m_wallClock();
    m_pairingCodes.removeIf(
        [&now](const PairingCode& code) { return code.expiresAt <= now; });
}

bool PeerServer::isListening() const {
    return m_server.isListening();
}

quint16 PeerServer::listeningPort() const {
    return m_server.serverPort();
}

QString PeerServer::mintPairingCode() {
    const QDateTime now = m_wallClock();
    m_pairingCodes.removeIf(
        [&now](const PairingCode& code) { return code.expiresAt <= now; });

    QString code;
    code.reserve(kPairingCodeLength);
    QRandomGenerator* generator = QRandomGenerator::system();
    for (int index = 0; index < kPairingCodeLength; ++index) {
        code.append(kPairingCodeAlphabet.at(
            static_cast<int>(generator->bounded(kPairingCodeAlphabet.size()))));
    }
    m_pairingCodes.append({code, now.addMSecs(kPairingCodeTtlMs)});
    return code;
}

void PeerServer::acceptConnection() {
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        socket->setParent(this);
        m_sockets.insert(socket);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { socketReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket] { socketDisconnected(socket); });
        // An idle connection is a forgotten one: controllers poll on a
        // schedule, so sixty seconds of silence means the peer went away.
        auto* idleTimer = new QTimer(socket);
        idleTimer->setSingleShot(true);
        connect(idleTimer, &QTimer::timeout, this,
                [this, socket] { socketIdleTimeout(socket); });
        idleTimer->start(kIdleTimeoutMs);
    }
}

void PeerServer::socketReadyRead(QTcpSocket* socket) {
    QTimer* idleTimer = socket->findChild<QTimer*>();
    if (idleTimer != nullptr) {
        idleTimer->start(kIdleTimeoutMs);
    }
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine();
        if (line.size() > kMaximumLineBytes) {
            // Broken framing: cannot be trusted to carry a request id, so
            // this gets no typed error — the connection is simply dropped.
            dropSocket(socket);
            return;
        }
        handleLine(socket, line);
    }
}

void PeerServer::socketDisconnected(QObject* socket) {
    m_sockets.remove(static_cast<QTcpSocket*>(socket));
}

void PeerServer::socketIdleTimeout(QTcpSocket* socket) {
    dropSocket(socket);
}

void PeerServer::dropSocket(QTcpSocket* socket) {
    m_sockets.remove(socket);
    socket->abort();
    socket->deleteLater();
}

void PeerServer::handleLine(QTcpSocket* socket, const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendError(socket, QString(), QStringLiteral("malformed"),
                  QStringLiteral("each request must be one JSON object per line"));
        return;
    }
    const QJsonObject request = document.object();
    const QString requestId = request.value(QStringLiteral("id")).toString();
    if (request.value(QStringLiteral("schemaVersion")).toInt(0) != 1) {
        sendError(socket, requestId, QStringLiteral("wrongSchemaVersion"),
                  QStringLiteral("this listener speaks schemaVersion 1"));
        return;
    }
    const QString operation = request.value(QStringLiteral("op")).toString();
    if (operation == QLatin1String("pair")) {
        handlePair(socket, requestId, request);
    } else if (operation == QLatin1String("snapshot")) {
        handleSnapshot(socket, requestId, request);
    } else if (operation == QLatin1String("setVolume") || operation == QLatin1String("setMute")) {
        handleAction(socket, requestId, operation, request);
    } else {
        sendError(socket, requestId, QStringLiteral("unknownOperation"),
                  QStringLiteral("supported operations: pair, snapshot, setVolume, setMute"));
    }
}

void PeerServer::handlePair(QTcpSocket* socket, const QString& requestId,
                            const QJsonObject& request) {
    const QString code = request.value(QStringLiteral("code")).toString();
    const QString machineId = request.value(QStringLiteral("machineId")).toString();
    QString hostName = request.value(QStringLiteral("hostname")).toString();
    if (!isUsableIdentity(machineId)) {
        sendError(socket, requestId, QStringLiteral("malformed"),
                  QStringLiteral("machineId must be 1-64 printable non-space characters"));
        return;
    }
    if (!hostName.isEmpty() && !isUsableIdentity(hostName)) {
        sendError(socket, requestId, QStringLiteral("malformed"),
                  QStringLiteral("hostname must be 1-64 printable non-space characters"));
        return;
    }
    if (hostName.isEmpty()) {
        hostName = QStringLiteral("unnamed-peer");
    }

    const QDateTime now = m_wallClock();
    const auto pairing = std::find_if(m_pairingCodes.cbegin(), m_pairingCodes.cend(),
                                      [&code](const PairingCode& candidate) {
                                          return candidate.code == code;
                                      });
    if (pairing == m_pairingCodes.cend()) {
        sendError(socket, requestId, QStringLiteral("invalidCode"),
                  QStringLiteral("the pairing code is unknown or already used"));
        return;
    }
    if (pairing->expiresAt <= now) {
        m_pairingCodes.removeIf(
            [&now](const PairingCode& candidate) { return candidate.expiresAt <= now; });
        sendError(socket, requestId, QStringLiteral("codeExpired"),
                  QStringLiteral("the pairing code expired; request a new one"));
        return;
    }
    // Burn on use: one code onboards exactly one machine.
    m_pairingCodes.erase(pairing);

    QList<PairedController> controllers = m_config->loadPairedControllers();
    auto existing =
        std::find_if(controllers.begin(), controllers.end(),
                     [&machineId](const PairedController& controller) {
                         return controller.machineId == machineId;
                     });
    if (existing == controllers.end()) {
        if (controllers.size() >= kMaxPairedControllers) {
            sendError(socket, requestId, QStringLiteral("limitReached"),
                      QStringLiteral("this machine already has the maximum number of paired controllers"));
            return;
        }
        PairedController controller;
        controller.machineId = machineId;
        controller.addedAt = now;
        controllers.append(std::move(controller));
        existing = std::prev(controllers.end());
    }
    // Re-pairing rotates the token: a controller that lost its token (or is
    // re-arming after a revoke on its own side) gets a fresh secret without
    // a stale one continuing to work anywhere else.
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    existing->token = token;
    existing->addedAt = now;
    m_config->savePairedControllers(controllers);

    QJsonObject payload;
    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("peer"),
                   QJsonObject{{QStringLiteral("machineId"), m_machineId},
                               {QStringLiteral("hostname"), m_hostName}});
    payload.insert(QStringLiteral("token"), token);
    sendResponse(socket, requestId, payload);
}

void PeerServer::handleSnapshot(QTcpSocket* socket, const QString& requestId,
                                const QJsonObject& request) {
    const QString token = request.value(QStringLiteral("token")).toString();
    const QList<PairedController> controllers = m_config->loadPairedControllers();
    const bool authorized = std::any_of(
        controllers.cbegin(), controllers.cend(),
        [&token](const PairedController& controller) { return controller.token == token; });
    if (token.isEmpty() || !authorized) {
        sendError(socket, requestId, QStringLiteral("unauthorized"),
                  QStringLiteral("pair with this machine to receive a token"));
        return;
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("snapshot"), snapshotEnvelope());
    sendResponse(socket, requestId, payload);
}

void PeerServer::handleAction(QTcpSocket* socket, const QString& requestId,
                              const QString& operation, const QJsonObject& request) {
    if (!m_actionExecutor) {
        sendError(socket, requestId, QStringLiteral("unsupportedOperation"),
                  QStringLiteral("this listener does not execute actions"));
        return;
    }
    const QString token = request.value(QStringLiteral("token")).toString();
    const QList<PairedController> controllers = m_config->loadPairedControllers();
    const bool authorized = std::any_of(
        controllers.cbegin(), controllers.cend(),
        [&token](const PairedController& controller) { return controller.token == token; });
    if (token.isEmpty() || !authorized) {
        sendError(socket, requestId, QStringLiteral("unauthorized"),
                  QStringLiteral("pair with this machine to receive a token"));
        return;
    }

    const QJsonValue value = request.value(QStringLiteral("value"));
    if (operation == QLatin1String("setVolume")) {
        if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > 100) {
            sendError(socket, requestId, QStringLiteral("invalidValue"),
                      QStringLiteral("setVolume takes an absolute 0-100 integer"));
            return;
        }
    } else if (operation == QLatin1String("setMute") && !value.isBool()) {
        // setMute is explicit (design §5): a toggle over a latency-bearing
        // path is ambiguous, so a non-bool is a typed rejection.
        sendError(socket, requestId, QStringLiteral("invalidValue"),
                  QStringLiteral("setMute takes an explicit boolean"));
        return;
    }

    m_actionExecutor(operation, value, [this, socket, requestId](bool ok, const QString& errorCode) {
        if (!m_sockets.contains(socket)) {
            // The peer went away mid-action (pointer comparison only — never
            // dereferenced): the verdict is dropped, the controller's own
            // timeout governs.
            return;
        }
        if (ok) {
            sendResponse(socket, requestId,
                         QJsonObject{{QStringLiteral("ok"), true},
                                     {QStringLiteral("result"), QStringLiteral("accepted")}});
        } else {
            sendError(socket, requestId,
                      errorCode.isEmpty() ? QStringLiteral("rejected") : errorCode,
                      QStringLiteral("the action could not be executed locally"));
        }
    });
}

QJsonObject PeerServer::snapshotEnvelope() const {
    // The provider may return an all-Unknown snapshot (LOCAL-PC before its
    // first poll); the envelope stays honest about that rather than hiding
    // the request behind an error.
    const DeviceSnapshot snapshot = m_snapshotProvider();
    const QString serial = peerSerialForMachineId(m_machineId);
    QJsonObject envelope{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("serial"), serial},
        {QStringLiteral("displayName"), m_hostName},
        {QStringLiteral("health"), healthName(snapshot.health)},
    };
    envelope.insert(QStringLiteral("volume"),
                    snapshot.volume ? QJsonValue(*snapshot.volume) : QJsonValue::Null);
    envelope.insert(QStringLiteral("muted"),
                    snapshot.muted ? QJsonValue(*snapshot.muted) : QJsonValue::Null);
    envelope.insert(QStringLiteral("playing"),
                    snapshot.playing ? QJsonValue(*snapshot.playing) : QJsonValue::Null);
    QStringList capabilities(snapshot.capabilities.cbegin(), snapshot.capabilities.cend());
    if (capabilities.isEmpty()) {
        // A PC's own contract (design §4/§5): volume and mute are always
        // offered, even before the first local poll populates the snapshot.
        capabilities = {QStringLiteral("volume"), QStringLiteral("mute")};
    }
    capabilities.sort();
    QJsonArray capabilityArray;
    for (const QString& capability : capabilities) {
        capabilityArray.append(capability);
    }
    envelope.insert(QStringLiteral("capabilities"), capabilityArray);
    // The ceiling travels with the state (design §5): the peer is the source
    // of truth for the policy, and the controller echoes it for its UI.
    if (m_ceilingProvider) {
        envelope.insert(QStringLiteral("volumeCeiling"), m_ceilingProvider());
    }
    if (snapshot.capturedAt.isValid()) {
        envelope.insert(QStringLiteral("capturedAt"),
                        snapshot.capturedAt.toString(Qt::ISODateWithMs));
    }
    return envelope;
}

void PeerServer::sendResponse(QTcpSocket* socket, const QString& requestId,
                              const QJsonObject& payload) {
    QJsonObject response = payload;
    response.insert(QStringLiteral("schemaVersion"), 1);
    response.insert(QStringLiteral("id"), requestId);
    socket->write(compactJson(response).toUtf8() + QByteArrayLiteral("\n"));
}

void PeerServer::sendError(QTcpSocket* socket, const QString& requestId,
                           const QString& errorCode, const QString& message) {
    QJsonObject response{{QStringLiteral("schemaVersion"), 1},
                         {QStringLiteral("id"), requestId},
                         {QStringLiteral("ok"), false},
                         {QStringLiteral("error"), errorCode},
                         {QStringLiteral("message"), message}};
    socket->write(compactJson(response).toUtf8() + QByteArrayLiteral("\n"));
}

} // namespace RokuDeck

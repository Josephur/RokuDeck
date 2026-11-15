// SPDX-License-Identifier: GPL-3.0-or-later
#include "peer_client.h"

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

namespace RokuDeck {
namespace {

constexpr qsizetype kMaximumLineBytes = 64 * 1024;

[[nodiscard]] QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

PeerClient::PeerClient(QObject* parent) : QObject(parent) {}

PeerPairOutcome PeerClient::pair(const QString& address, quint16 port,
                                 const QString& code, const QString& machineId,
                                 const QString& hostName,
                                 std::chrono::milliseconds timeout) {
    PeerPairOutcome outcome;
    QTcpSocket socket;
    QEventLoop pump;
    QTimer backstop;
    backstop.setSingleShot(true);
    backstop.start(static_cast<int>(timeout.count()) + 500);

    // The exchange is one request line and one response line; anything else
    // (connection refusal, silence, garbage) resolves into the typed failure
    // below once the pump unwinds.
    QByteArray responseLine;
    bool gotResponse = false;
    bool transportFailed = false;
    QString transportError;

    const QMetaObject::Connection onReadyRead = connect(&socket, &QTcpSocket::readyRead,
                                                        &pump, [&]() {
            while (socket.canReadLine()) {
                const QByteArray line = socket.readLine();
                if (line.size() > kMaximumLineBytes) {
                    transportFailed = true;
                    transportError = QStringLiteral("the peer sent an oversized line");
                    pump.quit();
                    return;
                }
                if (!gotResponse) {
                    responseLine = line;
                    gotResponse = true;
                }
                pump.quit();
            }
        });
    const QMetaObject::Connection onConnected =
        connect(&socket, &QTcpSocket::connected, &pump, [&]() {
            const QJsonObject request{
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {QStringLiteral("op"), QStringLiteral("pair")},
                {QStringLiteral("code"), code},
                {QStringLiteral("machineId"), machineId},
                {QStringLiteral("hostname"), hostName}};
            socket.write(compactJson(request).toUtf8() + QByteArrayLiteral("\n"));
        });
    const QMetaObject::Connection onError = connect(
        &socket, &QTcpSocket::errorOccurred, &pump,
        [&](QAbstractSocket::SocketError) {
            // connectionRefused after a completed handshake is just the peer
            // closing; everything before a response is a transport failure.
            if (!gotResponse) {
                transportFailed = true;
                transportError = socket.errorString();
                pump.quit();
            }
        });
    connect(&backstop, &QTimer::timeout, &pump, &QEventLoop::quit);

    socket.connectToHost(address, port);
    pump.exec();
    disconnect(onReadyRead);
    disconnect(onConnected);
    disconnect(onError);

    if (!gotResponse) {
        outcome.errorCode = transportFailed ? QStringLiteral("connectFailed")
                                            : QStringLiteral("timeout");
        outcome.errorMessage = transportFailed
            ? transportError
            : QStringLiteral("the peer did not answer within %1 ms").arg(timeout.count());
        return outcome;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        outcome.errorCode = QStringLiteral("malformed");
        outcome.errorMessage = QStringLiteral("the peer sent an unparseable response");
        return outcome;
    }
    const QJsonObject response = document.object();
    if (response.value(QStringLiteral("ok")).toBool(false)) {
        const QJsonObject peer = response.value(QStringLiteral("peer")).toObject();
        const QString peerMachineId = peer.value(QStringLiteral("machineId")).toString();
        const QString token = response.value(QStringLiteral("token")).toString();
        if (peerMachineId.isEmpty() || token.isEmpty()) {
            outcome.errorCode = QStringLiteral("malformed");
            outcome.errorMessage =
                QStringLiteral("the peer accepted the pairing but sent no identity or token");
            return outcome;
        }
        outcome.ok = true;
        outcome.machineId = peerMachineId;
        outcome.displayName = peer.value(QStringLiteral("hostname")).toString();
        outcome.token = token;
        return outcome;
    }
    outcome.errorCode = response.value(QStringLiteral("error")).toString();
    outcome.errorMessage = response.value(QStringLiteral("message")).toString();
    return outcome;
}

void PeerClient::requestSnapshot(const QString& key, const QString& address, quint16 port,
                                 const QString& token, std::chrono::milliseconds timeout) {
    startExchange(key, address, port,
                  QJsonObject{{QStringLiteral("op"), QStringLiteral("snapshot")},
                              {QStringLiteral("token"), token}},
                  timeout);
}

void PeerClient::requestAction(const QString& key, const QString& address, quint16 port,
                               const QString& token, const QString& operation,
                               const QJsonValue& value, std::chrono::milliseconds timeout) {
    startExchange(key, address, port,
                  QJsonObject{{QStringLiteral("op"), operation},
                              {QStringLiteral("token"), token},
                              {QStringLiteral("value"), value}},
                  timeout);
}

// Shared exchange plumbing: one request line, one response line, one peerReply
// emission. finish() detaches every handler first so exactly one reply fires
// even when it runs from inside one of the socket's own handlers.
void PeerClient::startExchange(const QString& key, const QString& address, quint16 port,
                               QJsonObject request, std::chrono::milliseconds timeout) {
    request.insert(QStringLiteral("schemaVersion"), 1);
    request.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));

    auto* socket = new QTcpSocket(this);
    auto* backstop = new QTimer(socket);
    backstop->setSingleShot(true);
    backstop->start(static_cast<int>(timeout.count()));

    const auto finish = [socket, backstop, key, this](bool ok, const QString& errorCode,
                                                      const QJsonObject& response) {
        // Fire exactly once per request: detaching every handler (including
        // the backstop) makes late signals after the first impossible even
        // when finish runs from inside one of them.
        socket->disconnect();
        backstop->disconnect();
        Q_EMIT peerReply(key, ok, errorCode,
                         ok ? QString::fromUtf8(QJsonDocument(response).toJson(
                                  QJsonDocument::Compact))
                            : QString());
        socket->disconnectFromHost();
        socket->deleteLater();
    };

    QObject::connect(socket, &QTcpSocket::connected, socket, [socket, request]() {
        socket->write(compactJson(request).toUtf8() + QByteArrayLiteral("\n"));
    });
    QObject::connect(socket, &QTcpSocket::readyRead, socket,
                     [socket, finish]() {
                         while (socket->canReadLine()) {
                             const QByteArray line = socket->readLine();
                             const QJsonDocument document = QJsonDocument::fromJson(line);
                             if (!document.isObject()) {
                                 finish(false, QStringLiteral("malformed"), {});
                                 return;
                             }
                             const QJsonObject response = document.object();
                             if (response.value(QStringLiteral("ok")).toBool(false)) {
                                 finish(true, QString(), response);
                             } else {
                                 finish(false,
                                        response.value(QStringLiteral("error")).toString(),
                                        {});
                             }
                             return;
                         }
                     });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket,
                     [finish](QAbstractSocket::SocketError) {
                         // A refusal before any response is transport; after a
                         // response the finish() already ran and the socket is
                         // disconnected from these handlers.
                         finish(false, QStringLiteral("connectFailed"), {});
                     });
    QObject::connect(backstop, &QTimer::timeout, socket, [finish]() {
        finish(false, QStringLiteral("timeout"), {});
    });

    socket->connectToHost(address, port);
}

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#include "peer_client.h"

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

} // namespace RokuDeck

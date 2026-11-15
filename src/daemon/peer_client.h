// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <chrono>

class QTcpSocket;
class QTimer;

namespace RokuDeck {

// Outcome of the controller-side pairing handshake (network-peers design §3).
// On success machineId/displayName/token describe the peer and the credential
// to store. On failure errorCode is a stable machine-readable cause —
// "connectFailed", "timeout", "malformed", or the peer's own error code
// ("invalidCode", "codeExpired", "limitReached", …) — and errorMessage is the
// human-readable detail suitable for a D-Bus error reply.
struct PeerPairOutcome {
    bool ok = false;
    QString machineId;
    QString displayName;
    QString token;
    QString errorCode;
    QString errorMessage;
};

class PeerClient final : public QObject {
    Q_OBJECT

  public:
    explicit PeerClient(QObject* parent = nullptr);

    // Submit a one-time pairing code to a peer's listener. Blocking with a
    // nested event pump, like the daemon's delivery waits: the D-Bus
    // interface stays responsive during the bounded wait, and the same
    // function works unchanged from tests driving a loopback listener.
    [[nodiscard]] PeerPairOutcome pair(const QString& address, quint16 port,
                                       const QString& code, const QString& machineId,
                                       const QString& hostName,
                                       std::chrono::milliseconds timeout =
                                           std::chrono::seconds(5));

    // Async snapshot pull for the poll sweep: one request per call, the
    // result lands on peerReply() carrying the caller's key. Never blocks
    // the event loop — the poller runs on the daemon's main thread and a
    // stalled peer must not stall anything else.
    void requestSnapshot(const QString& key, const QString& address, quint16 port,
                         const QString& token,
                         std::chrono::milliseconds timeout = std::chrono::seconds(4));

    // Async action request (M9c: setVolume/setMute against a peer's
    // listener). Same transport contract as requestSnapshot: exactly one
    // peerReply() per call, keyed for the caller's bookkeeping.
    void requestAction(const QString& key, const QString& address, quint16 port,
                       const QString& token, const QString& operation,
                       const QJsonValue& value,
                       std::chrono::milliseconds timeout = std::chrono::seconds(4));

    Q_SIGNALS:
    // ok=false carries a stable errorCode ("connectFailed", "timeout",
    // "unauthorized", the peer's own typed refusals, …) and an empty payload;
    // ok=true carries the FULL response envelope JSON (the caller extracts
    // its "snapshot"/result payload and validates it).
    void peerReply(const QString& key, bool ok, const QString& errorCode,
                   const QString& replyJson);

  private:
    void startExchange(const QString& key, const QString& address, quint16 port,
                       QJsonObject request, std::chrono::milliseconds timeout);
};

} // namespace RokuDeck

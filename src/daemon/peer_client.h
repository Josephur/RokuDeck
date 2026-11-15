// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <chrono>

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
};

} // namespace RokuDeck

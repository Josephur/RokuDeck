// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config_store.h"
#include "core/device_state.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>

#include <functional>

class QTcpSocket;
class QTimer;

namespace RokuDeck {

// The stable identity of this machine (network-peers design §1): the content
// of /etc/machine-id, read once and cached. A machine without one falls back
// to a random identity for this session — pairing still works, but the
// identity will not survive a reboot. Real deployments (Linux) always have
// the file.
[[nodiscard]] QString localMachineId();
// The hostname advertised to paired controllers and used as the default
// display name of this machine on their side (design §1).
[[nodiscard]] QString localHostName();
// The registry serial of a peer machine (design §1): "PC-" plus the first
// eight hex characters of its machine id. Shared by the listener's envelope
// and the controller-side registry so both sides spell it identically.
[[nodiscard]] QString peerSerialForMachineId(const QString& machineId);

// M9a peer listener: newline-delimited JSON request/response over LAN-local
// TCP (network-peers design §2).
//
// Two operations exist. "pair" consumes a one-time minted code and, on
// success, stores the caller as a paired controller in [Peers] and hands back
// a long-lived connection token. "snapshot" exchanges a stored token for this
// machine's PC state envelope (design §4) — exactly the subset the design
// commits to sharing. Everything else — wrong schema, unknown operation, bad
// or expired code, unknown token — is a typed JSON error, never a hang and
// never a partial success.
//
// Traffic is plaintext by decision (§3/§10): LAN-local only, ECP-equivalent
// threat model, the token gates access, and the framing is versioned so a
// later TLS swap does not touch callers. A line longer than the cap is
// treated as a broken peer and dropped rather than buffered unboundedly.
class PeerServer final : public QObject {
    Q_OBJECT

  public:
    // The LOCAL-PC snapshot this listener exposes (design §4). Consulted per
    // request, so the envelope always reflects the machine's current state.
    using SnapshotProvider = std::function<DeviceSnapshot()>;
    // This machine's own audio ceiling (design §5: the peer is the source of
    // truth and clamps incoming actions itself). Consulted per request.
    using CeilingProvider = std::function<int()>;
    // Executes one local audio action on this machine (M9c): setVolume
    // (absolute, clamped by the ceiling) or setMute (explicit). The executor
    // is asynchronous — done() fires exactly once with the verdict.
    using ActionExecutor =
        std::function<void(const QString& operation, const QJsonValue& value,
                           std::function<void(bool ok, const QString& errorCode)> done)>;
    using WallClock = std::function<QDateTime()>;

    static constexpr quint16 kDefaultPort = 49160;

    // `machineId`/`hostName` are injectable for tests; empty means "use the
    // real local identity" (read once, at construction). The action executor
    // may be empty in tests that only exercise pairing/snapshots.
    explicit PeerServer(ConfigStore* config, SnapshotProvider snapshotProvider,
                        WallClock wallClock = {}, QString machineId = {},
                        QString hostName = {}, ActionExecutor actionExecutor = {},
                        CeilingProvider ceilingProvider = {}, QObject* parent = nullptr);
    ~PeerServer() override;

    // Binds the listener. A port change while running restarts the socket;
    // start(0) lets the OS assign one (tests use this).
    [[nodiscard]] bool start(quint16 port);
    void stop();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] quint16 listeningPort() const;

    // A one-time pairing code: eight characters from an unambiguous alphabet
    // (no 0/O/1/I — codes are read aloud across a room), valid for ten
    // minutes, burned on first successful use. Minting never invalidates an
    // older unexpired code: two machines may be onboarding at once.
    [[nodiscard]] QString mintPairingCode();

  private:
    struct PairingCode {
        QString code;
        QDateTime expiresAt;
    };

    void acceptConnection();
    void socketReadyRead(QTcpSocket* socket);
    void socketDisconnected(QObject* socket);
    void socketIdleTimeout(QTcpSocket* socket);
    void dropSocket(QTcpSocket* socket);
    void handleLine(QTcpSocket* socket, const QByteArray& line);
    void handlePair(QTcpSocket* socket, const QString& requestId,
                    const QJsonObject& request);
    void handleSnapshot(QTcpSocket* socket, const QString& requestId,
                        const QJsonObject& request);
    void handleAction(QTcpSocket* socket, const QString& requestId,
                      const QString& operation, const QJsonObject& request);
    void sendResponse(QTcpSocket* socket, const QString& requestId,
                      const QJsonObject& payload);
    void sendError(QTcpSocket* socket, const QString& requestId,
                   const QString& errorCode, const QString& message);
    [[nodiscard]] QJsonObject snapshotEnvelope() const;

    ConfigStore* m_config;
    SnapshotProvider m_snapshotProvider;
    ActionExecutor m_actionExecutor;
    CeilingProvider m_ceilingProvider;
    WallClock m_wallClock;
    QString m_machineId;
    QString m_hostName;
    QTcpServer m_server;
    QSet<QTcpSocket*> m_sockets;
    QList<PairingCode> m_pairingCodes;
};

} // namespace RokuDeck

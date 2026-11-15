// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/device_registry.h"
#include "core/rule_model.h"

#include <KSharedConfig>

#include <QDateTime>
#include <QList>

namespace RokuDeck {

// This machine's peer-listener settings (network-peers design §10): the
// listener is explicitly enabled only, and peers connect to this port.
struct PeerListenerSettings {
    bool enabled = false;
    quint16 port = 49160;
};

// A machine we have paired WITH AS THE LISTENER: it presented a valid one-time
// pairing code and received the connection token below. Possession of the
// token lets it pull our snapshot state (and, once M9c lands, drive our
// audio). The token is stored in the user's own config file, matching the
// LAN-local plaintext threat model of the design (§3).
struct PairedController {
    QString machineId;
    QString token;
    QDateTime addedAt;
};

// A machine we have paired with AS THE CONTROLLER: we hold its token so a
// later connection can authenticate without a fresh code. Registry
// integration (this becoming a PC- device) is M9b; M9a only stores it.
struct PeerTarget {
    QString machineId;
    QString displayName;
    QString address;
    quint16 port = 0;
    QString token;
};

class ConfigStore final {
  public:
    explicit ConfigStore(
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("rokudeckrc")));

    [[nodiscard]] QList<DeviceRecord> loadDevices() const;
    void saveDevices(const QList<DeviceRecord>& devices);

    // Automation rules persist under a [Rules] parent group, one subgroup per
    // rule in insertion order. Entries that fail to parse are skipped on load
    // so one corrupt record cannot hide the rest.
    [[nodiscard]] QList<RuleDocument> loadRules() const;
    void saveRules(const QList<RuleDocument>& rules);

    // Peer-network persistence ([PeerListener], [Peers], [PeerTargets]).
    // Entries with an empty machine id are skipped on load, mirroring the
    // device/rule guards: a hand-edited file cannot inject a nameless peer.
    [[nodiscard]] PeerListenerSettings loadPeerListener() const;
    void savePeerListener(const PeerListenerSettings& settings);
    [[nodiscard]] QList<PairedController> loadPairedControllers() const;
    void savePairedControllers(const QList<PairedController>& controllers);
    [[nodiscard]] QList<PeerTarget> loadPeerTargets() const;
    void savePeerTargets(const QList<PeerTarget>& targets);

  private:
    KSharedConfig::Ptr m_config;
};

} // namespace RokuDeck

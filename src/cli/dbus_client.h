// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "command_line.h"

#include <QJsonDocument>
#include <QString>

namespace RokuDeck::Cli {

struct ClientResult {
    ExitCode exitCode = ExitCode::Success;
    QJsonDocument document;
    QString errorMessage;

    [[nodiscard]] bool succeeded() const {
        return exitCode == ExitCode::Success;
    }
};

class DbusClient final {
  public:
    [[nodiscard]] ClientResult devices() const;
    [[nodiscard]] ClientResult status(const QString& device) const;
    [[nodiscard]] ClientResult discover() const;
    [[nodiscard]] ClientResult sendCommand(const QString& device, const QString& action) const;
    // Peer management (M9a). "peers pair" blocks for the bounded handshake.
    [[nodiscard]] ClientResult peersList() const;
    [[nodiscard]] ClientResult peersCode() const;
    [[nodiscard]] ClientResult peersPair(const QString& address, uint port,
                                         const QString& code) const;
    [[nodiscard]] ClientResult peersRemove(const QString& machineId) const;
    [[nodiscard]] ClientResult peersRevoke(const QString& machineId) const;
    [[nodiscard]] ClientResult peersSetListener(bool enabled) const;
};

[[nodiscard]] ExitCode exitCodeForError(const QString& errorCode);

} // namespace RokuDeck::Cli

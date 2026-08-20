// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace RokuDeck::Cli {

enum class Command {
    Devices,
    Status,
    Discover,
    SendCommand,
};

enum class ExitCode : int {
    Success = 0,
    Usage = 2,
    ServiceUnavailable = 3,
    UnknownDevice = 4,
    UnsupportedCommand = 5,
    SafetyRejected = 6,
    TransportFailure = 7,
};

struct Options {
    Command command = Command::Devices;
    bool json = false;
    bool quiet = false;
    QString device;
    QString action;
};

struct ParseResult {
    bool shouldRun = false;
    ExitCode exitCode = ExitCode::Success;
    Options options;
    QString standardOutput;
    QString standardError;
};

[[nodiscard]] ParseResult parseCommandLine(const QStringList& arguments);

} // namespace RokuDeck::Cli

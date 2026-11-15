// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_line.h"
#include "dbus_client.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace RokuDeck::Cli {
namespace {

void writeText(QTextStream& stream, const QString& text) {
    stream << text;
    stream.flush();
}

[[nodiscard]] QString deviceListText(const QJsonArray& devices) {
    if (devices.isEmpty()) {
        return QStringLiteral("No devices found.\n");
    }
    QString output;
    QTextStream stream(&output);
    for (const QJsonValue& value : devices) {
        const QJsonObject device = value.toObject();
        stream << device.value(QStringLiteral("serial")).toString() << QChar(u'\t')
               << device.value(QStringLiteral("displayName")).toString() << QChar(u'\t')
               << device.value(QStringLiteral("address")).toString() << QChar(u'\n');
    }
    return output;
}

[[nodiscard]] QString statusText(const QJsonObject& status) {
    QString output;
    QTextStream stream(&output);
    stream << QStringLiteral("Device: ") << status.value(QStringLiteral("serial")).toString()
           << QChar(u'\n') << QStringLiteral("Name: ")
           << status.value(QStringLiteral("displayName")).toString() << QChar(u'\n')
           << QStringLiteral("Power: ") << status.value(QStringLiteral("power")).toString()
           << QChar(u'\n') << QStringLiteral("Screensaver: ")
           << status.value(QStringLiteral("screensaver")).toString() << QChar(u'\n')
           << QStringLiteral("Health: ") << status.value(QStringLiteral("health")).toString()
           << QChar(u'\n');
    return output;
}

[[nodiscard]] QString peersText(const QJsonObject& document) {
    // Each peers subcommand returns a differently-shaped document; the shape
    // present decides the rendering.
    if (document.contains(QStringLiteral("code"))) {
        return document.value(QStringLiteral("code")).toString() + QChar(u'\n');
    }
    if (document.contains(QStringLiteral("peer"))) {
        const QJsonObject peer = document.value(QStringLiteral("peer")).toObject();
        return QStringLiteral("Paired with %1 (%2) at %3:%4\n")
            .arg(peer.value(QStringLiteral("displayName")).toString(),
                 peer.value(QStringLiteral("machineId")).toString(),
                 peer.value(QStringLiteral("address")).toString())
            .arg(peer.value(QStringLiteral("port")).toInt());
    }
    if (document.contains(QStringLiteral("listener"))) {
        QString output;
        QTextStream stream(&output);
        const QJsonObject listener = document.value(QStringLiteral("listener")).toObject();
        if (listener.value(QStringLiteral("listening")).toBool()) {
            stream << QStringLiteral("Peer listener: listening on port %1\n")
                          .arg(listener.value(QStringLiteral("boundPort")).toInt());
        } else if (listener.value(QStringLiteral("enabled")).toBool()) {
            stream << QStringLiteral("Peer listener: enabled but not listening\n");
        } else {
            stream << QStringLiteral("Peer listener: off\n");
        }
        const QJsonArray controllers =
            document.value(QStringLiteral("controllers")).toArray();
        if (controllers.isEmpty()) {
            stream << QStringLiteral("No machines may connect to this one.\n");
        } else {
            stream << QStringLiteral("Paired controllers:\n");
            for (const QJsonValue& value : controllers) {
                const QJsonObject controller = value.toObject();
                stream << QChar(u'\t')
                       << controller.value(QStringLiteral("machineId")).toString()
                       << QChar(u'\t')
                       << controller.value(QStringLiteral("addedAt")).toString() << QChar(u'\n');
            }
        }
        const QJsonArray targets = document.value(QStringLiteral("targets")).toArray();
        if (targets.isEmpty()) {
            stream << QStringLiteral("No paired peers.\n");
        } else {
            stream << QStringLiteral("Paired peers:\n");
            for (const QJsonValue& value : targets) {
                const QJsonObject target = value.toObject();
                stream << QChar(u'\t')
                       << target.value(QStringLiteral("displayName")).toString() << QChar(u'\t')
                       << target.value(QStringLiteral("machineId")).toString() << QChar(u'\t')
                       << target.value(QStringLiteral("address")).toString() << QChar(u':')
                       << target.value(QStringLiteral("port")).toInt() << QChar(u'\n');
            }
        }
        return output;
    }
    return QStringLiteral("Done.\n");
}

[[nodiscard]] QString successText(Command command, const QJsonDocument& document) {
    switch (command) {
    case Command::Devices:
    case Command::Discover:
        return deviceListText(document.array());
    case Command::Status:
        return statusText(document.object());
    case Command::SendCommand: {
        const QJsonObject result = document.object();
        return QStringLiteral("Command %1 completed.\n")
            .arg(result.value(QStringLiteral("requestId")).toString());
    }
    case Command::Peers:
        return peersText(document.object());
    }
    return {};
}

} // namespace
} // namespace RokuDeck::Cli

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rokudeckctl"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ROKUDECK_VERSION));

    QTextStream standardOutput(stdout);
    QTextStream standardError(stderr);
    const RokuDeck::Cli::ParseResult parsed =
        RokuDeck::Cli::parseCommandLine(QCoreApplication::arguments());
    if (!parsed.standardOutput.isEmpty()) {
        RokuDeck::Cli::writeText(standardOutput, parsed.standardOutput);
    }
    if (!parsed.standardError.isEmpty()) {
        RokuDeck::Cli::writeText(standardError, parsed.standardError);
    }
    if (!parsed.shouldRun) {
        return static_cast<int>(parsed.exitCode);
    }

    RokuDeck::Cli::DbusClient client;
    RokuDeck::Cli::ClientResult result;
    switch (parsed.options.command) {
    case RokuDeck::Cli::Command::Devices:
        result = client.devices();
        break;
    case RokuDeck::Cli::Command::Status:
        result = client.status(parsed.options.device);
        break;
    case RokuDeck::Cli::Command::Discover:
        result = client.discover();
        break;
    case RokuDeck::Cli::Command::SendCommand:
        result = client.sendCommand(parsed.options.device, parsed.options.action);
        break;
    case RokuDeck::Cli::Command::Peers: {
        const QStringList& peerArguments = parsed.options.peerArguments;
        const QString& subcommand = peerArguments.front();
        if (subcommand == QLatin1String("list")) {
            result = client.peersList();
        } else if (subcommand == QLatin1String("code")) {
            result = client.peersCode();
        } else if (subcommand == QLatin1String("pair")) {
            bool portValid = false;
            const uint port = peerArguments.at(2).toUInt(&portValid);
            if (!portValid || port == 0) {
                result = RokuDeck::Cli::ClientResult{
                    RokuDeck::Cli::ExitCode::Usage, {},
                    QStringLiteral("PORT must be a number between 1 and 65535")};
            } else {
                result = client.peersPair(peerArguments.at(1), port, peerArguments.at(3));
            }
        } else if (subcommand == QLatin1String("remove")) {
            result = client.peersRemove(peerArguments.at(1));
        } else if (subcommand == QLatin1String("revoke")) {
            result = client.peersRevoke(peerArguments.at(1));
        } else if (subcommand == QLatin1String("listener")) {
            result = client.peersSetListener(peerArguments.at(1) == QLatin1String("on"));
        }
        break;
    }
    }

    if (!result.succeeded()) {
        RokuDeck::Cli::writeText(standardError,
                                 QStringLiteral("rokudeckctl: %1\n").arg(result.errorMessage));
        return static_cast<int>(result.exitCode);
    }
    if (!parsed.options.quiet) {
        if (parsed.options.json) {
            standardOutput << result.document.toJson(QJsonDocument::Compact) << Qt::endl;
        } else {
            RokuDeck::Cli::writeText(standardOutput, RokuDeck::Cli::successText(
                                                         parsed.options.command, result.document));
        }
    }
    return static_cast<int>(RokuDeck::Cli::ExitCode::Success);
}

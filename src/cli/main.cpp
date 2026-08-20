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
    }
    return {};
}

} // namespace
} // namespace RokuDeck::Cli

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rokudeckctl"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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

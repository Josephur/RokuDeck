// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_line.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

namespace RokuDeck::Cli {
namespace {

[[nodiscard]] ParseResult usageError(const QString& message) {
    ParseResult result;
    result.exitCode = ExitCode::Usage;
    result.standardError =
        QStringLiteral("rokudeckctl: %1\nTry 'rokudeckctl --help' for usage.\n").arg(message);
    return result;
}

} // namespace

ParseResult parseCommandLine(const QStringList& arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Control RokuDeck through io.github.josephur.RokuDeck1 D-Bus."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption jsonOption(QStringLiteral("json"),
                                        QStringLiteral("Write machine-readable JSON."));
    const QCommandLineOption quietOption(QStringLiteral("quiet"),
                                         QStringLiteral("Suppress successful output."));
    parser.addOption(jsonOption);
    parser.addOption(quietOption);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("One of: devices, status, discover, command."));
    parser.addPositionalArgument(QStringLiteral("arguments"),
                                 QStringLiteral("Arguments required by the selected command."),
                                 QStringLiteral("[arguments...]"));

    if (!parser.parse(arguments)) {
        return usageError(parser.errorText());
    }
    if (parser.isSet(QStringLiteral("help"))) {
        ParseResult result;
        result.standardOutput = parser.helpText();
        return result;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        ParseResult result;
        result.standardOutput = QCoreApplication::applicationName() + QChar(u' ') +
                                QCoreApplication::applicationVersion() + QChar(u'\n');
        return result;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        return usageError(QStringLiteral("a command is required"));
    }

    ParseResult result;
    result.shouldRun = true;
    result.options.json = parser.isSet(jsonOption);
    result.options.quiet = parser.isSet(quietOption);
    const QString command = positional.front();
    const QStringList commandArguments = positional.sliced(1);

    if (command == QStringLiteral("devices")) {
        if (!commandArguments.isEmpty()) {
            return usageError(QStringLiteral("devices does not accept arguments"));
        }
        result.options.command = Command::Devices;
    } else if (command == QStringLiteral("status")) {
        if (commandArguments.size() != 1) {
            return usageError(QStringLiteral("status requires exactly one DEVICE"));
        }
        result.options.command = Command::Status;
        result.options.device = commandArguments.front();
    } else if (command == QStringLiteral("discover")) {
        if (!commandArguments.isEmpty()) {
            return usageError(QStringLiteral("discover does not accept arguments"));
        }
        result.options.command = Command::Discover;
    } else if (command == QStringLiteral("command")) {
        if (commandArguments.size() != 2) {
            return usageError(QStringLiteral("command requires exactly DEVICE and ACTION"));
        }
        result.options.command = Command::SendCommand;
        result.options.device = commandArguments.at(0);
        result.options.action = commandArguments.at(1);
    } else {
        return usageError(QStringLiteral("unknown command '%1'").arg(command));
    }

    return result;
}

} // namespace RokuDeck::Cli

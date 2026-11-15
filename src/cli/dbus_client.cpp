// SPDX-License-Identifier: GPL-3.0-or-later
#include "dbus_client.h"

#include "command_contract.h"
#include "rokudeck1_interface.h"

#include <QDBusError>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

#include <optional>

namespace RokuDeck::Cli {
namespace {

constexpr auto serviceName = "io.github.josephur.RokuDeck1";
constexpr auto objectPath = "/io/github/josephur/RokuDeck1";
constexpr int schemaVersion = 1;

// The daemon is D-Bus activated and exits when it goes idle, so it is normally
// not on the bus when a command runs. Making the call is what starts it; a
// proxy validity check here would instead refuse every command until something
// else happened to start the daemon first. A service that genuinely cannot
// start still comes back as one of the errors isUnavailableError() names.
[[nodiscard]] IoGithubJosephurRokuDeck1Interface makeProxy() {
    return {QString::fromLatin1(serviceName), QString::fromLatin1(objectPath),
            QDBusConnection::sessionBus()};
}

[[nodiscard]] ClientResult failure(ExitCode exitCode, const QString& message) {
    return {exitCode, {}, message};
}

[[nodiscard]] bool isUnavailableError(const QString& name) {
    return name == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown") ||
           name == QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner") ||
           name == QStringLiteral("org.freedesktop.DBus.Error.Spawn.ServiceNotFound") ||
           name == QStringLiteral("org.freedesktop.DBus.Error.Disconnected");
}

[[nodiscard]] ClientResult dbusFailure(const QDBusError& error) {
    const ExitCode code = isUnavailableError(error.name()) ? ExitCode::ServiceUnavailable
                                                           : exitCodeForError(error.name());
    const QString detail = error.message().isEmpty() ? error.name() : error.message();
    return failure(code, QStringLiteral("D-Bus request failed: %1").arg(detail));
}

[[nodiscard]] ClientResult parseObject(const QString& payload) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failure(ExitCode::TransportFailure,
                       QStringLiteral("the service returned malformed JSON"));
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != schemaVersion) {
        return failure(ExitCode::TransportFailure,
                       QStringLiteral("the service returned an unsupported schema version"));
    }
    const QString errorCode = object.value(QStringLiteral("error")).toString();
    if (!errorCode.isEmpty()) {
        return failure(exitCodeForError(errorCode),
                       QStringLiteral("RokuDeck rejected the request: %1").arg(errorCode));
    }
    return {ExitCode::Success, document, {}};
}

[[nodiscard]] ClientResult listResult(const QString& payload) {
    ClientResult result = parseObject(payload);
    if (!result.succeeded()) {
        return result;
    }
    const QJsonValue devices = result.document.object().value(QStringLiteral("devices"));
    if (!devices.isArray()) {
        return failure(ExitCode::TransportFailure,
                       QStringLiteral("the service response has no device list"));
    }
    result.document = QJsonDocument(devices.toArray());
    return result;
}

[[nodiscard]] QString commandPayload(const QString& action) {
    QJsonParseError parseError;
    const QJsonDocument supplied = QJsonDocument::fromJson(action.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && supplied.isObject()) {
        return QString::fromUtf8(supplied.toJson(QJsonDocument::Compact));
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("operation"), QStringLiteral("keypress")},
                                  {QStringLiteral("key"), action}})
            .toJson(QJsonDocument::Compact));
}

template <typename PendingReply>
[[nodiscard]] ClientResult stringReply(PendingReply reply, bool list) {
    reply.waitForFinished();
    if (reply.isError()) {
        return dbusFailure(reply.error());
    }
    return list ? listResult(reply.value()) : parseObject(reply.value());
}

// The generated proxy's void methods return void, silently dropping D-Bus
// error replies. Peer removal/revocation and the listener toggle must report
// their rejections, so they go through a synchronous call whose reply carries
// the error name and message.
[[nodiscard]] ClientResult voidCall(const QString& method, const QVariantList& arguments) {
    QDBusInterface interface(QString::fromLatin1(serviceName),
                             QString::fromLatin1(objectPath),
                             QString::fromLatin1(serviceName),
                             QDBusConnection::sessionBus());
    const QDBusMessage reply = interface.callWithArgumentList(QDBus::AutoDetect, method, arguments);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QString name = reply.errorName();
        const ExitCode code =
            isUnavailableError(name) ? ExitCode::ServiceUnavailable : exitCodeForError(name);
        const QString detail =
            reply.errorMessage().isEmpty() ? name : reply.errorMessage();
        return failure(code, QStringLiteral("D-Bus request failed: %1").arg(detail));
    }
    return {ExitCode::Success, {}, {}};
}

} // namespace

ExitCode exitCodeForError(const QString& errorCode) {
    const QString code = errorCode.section(QChar(u'.'), -1);
    if (code.compare(QStringLiteral("unknownDevice"), Qt::CaseInsensitive) == 0) {
        return ExitCode::UnknownDevice;
    }
    if (code.compare(QStringLiteral("unsupportedCommand"), Qt::CaseInsensitive) == 0 ||
        code.compare(QStringLiteral("unsupportedCapability"), Qt::CaseInsensitive) == 0 ||
        code.compare(QStringLiteral("unsupportedOperation"), Qt::CaseInsensitive) == 0 ||
        code.compare(QStringLiteral("invalidCommand"), Qt::CaseInsensitive) == 0 ||
        code.compare(QStringLiteral("commandTooLarge"), Qt::CaseInsensitive) == 0) {
        return ExitCode::UnsupportedCommand;
    }
    if (code.contains(QStringLiteral("safety"), Qt::CaseInsensitive) ||
        code.compare(QStringLiteral("staleSafetyData"), Qt::CaseInsensitive) == 0 ||
        code.compare(QStringLiteral("volumeMaximumExceeded"), Qt::CaseInsensitive) == 0) {
        return ExitCode::SafetyRejected;
    }
    if (code.compare(QStringLiteral("pairingRejected"), Qt::CaseInsensitive) == 0) {
        return ExitCode::PairingRejected;
    }
    return ExitCode::TransportFailure;
}

ClientResult DbusClient::devices() const {
    auto proxy = makeProxy();
    return stringReply(proxy.ListDevices(), true);
}

ClientResult DbusClient::status(const QString& device) const {
    auto proxy = makeProxy();
    return stringReply(proxy.GetSnapshot(device), false);
}

ClientResult DbusClient::discover() const {
    auto proxy = makeProxy();
    return stringReply(proxy.DiscoverDevices(), true);
}

ClientResult DbusClient::sendCommand(const QString& device, const QString& action) const {
    auto proxy = makeProxy();
    proxy.setTimeout(CommandContract::clientCompletionTimeoutMs);

    std::optional<ClientResult> completed;
    QHash<QString, QString> earlyCompletions;
    QString requestId;
    QEventLoop eventLoop;
    const QMetaObject::Connection signalConnection =
        QObject::connect(&proxy, &IoGithubJosephurRokuDeck1Interface::CommandFinished, &eventLoop,
                         [&](const QString& finishedId, const QString& payload) {
                             if (requestId.isEmpty()) {
                                 earlyCompletions.insert(finishedId, payload);
                             } else if (finishedId == requestId) {
                                 completed = parseObject(payload);
                                 eventLoop.quit();
                             }
                         });

    QDBusPendingReply<QString> reply = proxy.SendCommand(device, commandPayload(action));
    reply.waitForFinished();
    if (reply.isError()) {
        QObject::disconnect(signalConnection);
        return dbusFailure(reply.error());
    }
    requestId = reply.value();
    if (requestId.isEmpty()) {
        QObject::disconnect(signalConnection);
        return failure(ExitCode::TransportFailure,
                       QStringLiteral("the service returned an empty command request ID"));
    }
    if (const auto early = earlyCompletions.constFind(requestId);
        early != earlyCompletions.cend()) {
        completed = parseObject(early.value());
    }

    if (!completed) {
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.setInterval(CommandContract::clientCompletionTimeoutMs);
        QObject::connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
        timeout.start();
        eventLoop.exec();
    }
    QObject::disconnect(signalConnection);
    if (!completed) {
        return failure(ExitCode::TransportFailure,
                       QStringLiteral("timed out waiting for command completion"));
    }
    return *completed;
}

ClientResult DbusClient::peersList() const {
    auto proxy = makeProxy();
    return stringReply(proxy.ListPeers(), false);
}

ClientResult DbusClient::peersCode() const {
    auto proxy = makeProxy();
    QDBusPendingReply<QString> reply = proxy.PeerPairingCode();
    reply.waitForFinished();
    if (reply.isError()) {
        return dbusFailure(reply.error());
    }
    // The code is a bare token, not an envelope; wrap it so --json has a shape.
    return {ExitCode::Success,
            QJsonDocument(QJsonObject{{QStringLiteral("code"), reply.value()}}), {}};
}

ClientResult DbusClient::peersPair(const QString& address, uint port,
                                   const QString& code) const {
    auto proxy = makeProxy();
    proxy.setTimeout(CommandContract::clientCompletionTimeoutMs);
    return stringReply(proxy.PairPeer(address, port, code), false);
}

ClientResult DbusClient::peersRemove(const QString& machineId) const {
    return voidCall(QStringLiteral("RemovePeerTarget"), {machineId});
}

ClientResult DbusClient::peersRevoke(const QString& machineId) const {
    return voidCall(QStringLiteral("RevokePairedController"), {machineId});
}

ClientResult DbusClient::peersSetListener(bool enabled) const {
    const ClientResult toggled = voidCall(QStringLiteral("SetPeerListenerEnabled"), {enabled});
    if (!toggled.succeeded()) {
        return toggled;
    }
    // The toggle itself has no useful reply body; the fresh listing is the
    // confirmation the user can actually read (bound port or "off").
    return peersList();
}

} // namespace RokuDeck::Cli

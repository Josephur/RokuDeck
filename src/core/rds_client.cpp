// SPDX-License-Identifier: GPL-3.0-or-later
#include "rds_client.h"

#include "active_app_parser.h"
#include "audio_device_parser.h"
#include "media_player_parser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>

#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace RokuDeck {
namespace {

constexpr auto supportedRdsVersion = "1.0.0";
constexpr auto activeAppEndpoint = "/query/active-app";
constexpr auto mediaPlayerEndpoint = "/query/media-player";
constexpr auto audioDeviceEndpoint = "/query/audio-device";

struct QueryState {
    DeviceSnapshot snapshot;
    SnapshotCallback callback;
    int remainingEndpoints = 3;
    bool partialFailure = false;
    std::optional<RdsError> fatalError;
};

[[nodiscard]] bool looksLikeGrammarFailure(const ProcessResult& result) {
    const QString diagnostic = QString::fromUtf8(result.standardError).toLower();
    return diagnostic.contains(QStringLiteral("unknown command")) ||
           diagnostic.contains(QStringLiteral("unknown option")) ||
           diagnostic.contains(QStringLiteral("too many arguments")) ||
           diagnostic.contains(QStringLiteral("usage: rds"));
}

[[nodiscard]] std::optional<QJsonObject> jsonObject(const QByteArray& bytes) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

[[nodiscard]] PowerState powerStateFor(const QString& value) {
    if (value.compare(QStringLiteral("PowerOn"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("On"), Qt::CaseInsensitive) == 0) {
        return PowerState::On;
    }
    if (value.compare(QStringLiteral("Ready"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("Standby"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("PowerOff"), Qt::CaseInsensitive) == 0) {
        return PowerState::Standby;
    }
    return PowerState::Unknown;
}

[[nodiscard]] bool jsonBoolean(const QJsonValue& value) {
    return value.isBool()
               ? value.toBool()
               : value.toString().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] std::optional<DeviceSnapshot>
parseDeviceInfo(const QString& address, const ProcessResult& result, RdsError& error) {
    if (result.timedOut) {
        error = {RdsError::Code::TimedOut, QStringLiteral("rds device info timed out")};
        return std::nullopt;
    }
    if (result.exitCode != 0) {
        const bool grammarFailure = looksLikeGrammarFailure(result);
        error = {grammarFailure ? RdsError::Code::IncompatibleCli : RdsError::Code::ProcessFailed,
                 grammarFailure ? QStringLiteral("rds 1.0.0 command grammar is incompatible")
                                : QStringLiteral("rds device info failed")};
        return std::nullopt;
    }
    const auto object = jsonObject(result.standardOutput);
    if (!object) {
        error = {RdsError::Code::MalformedPayload,
                 QStringLiteral("rds device info returned malformed JSON")};
        return std::nullopt;
    }

    DeviceSnapshot snapshot;
    snapshot.address = address;
    snapshot.serial = object->value(QStringLiteral("serialNumber")).toString().trimmed();
    snapshot.displayName = object->value(QStringLiteral("deviceName")).toString().trimmed();
    snapshot.power = powerStateFor(object->value(QStringLiteral("powerMode")).toString());
    if (snapshot.serial.isEmpty()) {
        error = {RdsError::Code::MissingDeviceInfo,
                 QStringLiteral("rds device info has no serial number")};
        return std::nullopt;
    }

    const std::array<std::pair<const char*, const char*>, 3> capabilities{{
        {"supportsSuspend", "supports-suspend"},
        {"supportsPrivateListening", "supports-private-listening"},
        {"supportsFindRemote", "supports-find-remote"},
    }};
    for (const auto& [jsonName, capability] : capabilities) {
        if (jsonBoolean(object->value(QString::fromLatin1(jsonName)))) {
            snapshot.capabilities.insert(QString::fromLatin1(capability));
        }
    }
    if (snapshot.power == PowerState::On) {
        snapshot.health = DeviceHealth::Online;
    } else if (snapshot.power == PowerState::Standby) {
        snapshot.health = DeviceHealth::Standby;
    }
    return snapshot;
}

[[nodiscard]] ProcessRequest deviceInfoRequest(const QString& address) {
    return {QStringLiteral("rds"),
            {QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--timeout"),
             QStringLiteral("3000"), QStringLiteral("--ip"), address, QStringLiteral("device"),
             QStringLiteral("info")},
            std::chrono::milliseconds(3000)};
}

[[nodiscard]] ProcessRequest endpointRequest(const QString& address, const QString& endpoint) {
    return {QStringLiteral("rds"),
            {QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--timeout"),
             QStringLiteral("3000"), QStringLiteral("--ip"), address, QStringLiteral("ecp"),
             QStringLiteral("query"), endpoint},
            std::chrono::milliseconds(3000)};
}

[[nodiscard]] std::optional<QByteArray> endpointPayload(const ProcessResult& result) {
    if (result.timedOut || result.exitCode != 0) {
        return std::nullopt;
    }
    const auto object = jsonObject(result.standardOutput);
    if (!object || !object->value(QStringLiteral("success")).toBool() ||
        object->value(QStringLiteral("status")).toInt(-1) != 200 ||
        !object->value(QStringLiteral("data")).isString()) {
        return std::nullopt;
    }
    const QByteArray payload = object->value(QStringLiteral("data")).toString().toUtf8();
    return payload.isEmpty() ? std::nullopt : std::optional<QByteArray>(payload);
}

void finishEndpoint(const std::shared_ptr<QueryState>& state, const QString& endpoint,
                    const ProcessResult& result) {
    const auto payload = endpointPayload(result);
    if (!payload) {
        state->partialFailure = true;
    } else if (endpoint == QString::fromLatin1(activeAppEndpoint)) {
        const auto parsed = parseActiveApp(*payload);
        if (parsed) {
            state->snapshot.activeApp = parsed.value();
            state->snapshot.screensaver = parsed.value().screensaver;
        } else {
            state->partialFailure = true;
        }
    } else if (endpoint == QString::fromLatin1(mediaPlayerEndpoint)) {
        const auto parsed = parseMediaPlayer(*payload);
        if (parsed) {
            state->snapshot.media = parsed.value();
        } else {
            state->partialFailure = true;
        }
    } else if (endpoint == QString::fromLatin1(audioDeviceEndpoint)) {
        const auto parsed = parseAudioDevice(*payload);
        if (parsed) {
            state->snapshot.volume = parsed.value().volume;
            state->snapshot.muted = parsed.value().muted;
        } else {
            state->partialFailure = true;
        }
    }

    --state->remainingEndpoints;
    if (state->remainingEndpoints != 0) {
        return;
    }
    if (state->fatalError) {
        state->callback(*state->fatalError);
        return;
    }
    state->snapshot.partialFailure = state->partialFailure;
    if (state->partialFailure) {
        state->snapshot.health = DeviceHealth::Degraded;
    }
    state->snapshot.normalize();
    state->callback(std::move(state->snapshot));
}

} // namespace

RdsClient::RdsClient(ProcessRunner* runner) : m_runner(runner) {
    Q_ASSERT(m_runner);
}

void RdsClient::queryDevice(const QString& address, SnapshotCallback callback) {
    if (m_compatibility == CompatibilityState::Disabled) {
        callback(m_disabledError);
        return;
    }
    if (m_compatibility == CompatibilityState::Compatible) {
        startQuery({address, std::move(callback)});
        return;
    }

    m_pendingQueries.append({address, std::move(callback)});
    if (m_compatibility == CompatibilityState::Unchecked) {
        checkCompatibility();
    }
}

void RdsClient::checkCompatibility() {
    m_compatibility = CompatibilityState::Checking;
    ProcessRequest request{
        QStringLiteral("rds"), {QStringLiteral("--version")}, std::chrono::milliseconds(3000)};
    QPointer<RdsClient> client(this);
    m_runner->start(request, [client](ProcessResult result) {
        if (!client) {
            return;
        }
        if (result.timedOut || result.exitCode != 0) {
            client->disablePolling(
                {RdsError::Code::DependencyUnavailable,
                 result.timedOut ? QStringLiteral("rds version check timed out; polling disabled")
                                 : QStringLiteral("rds is unavailable; polling disabled")});
            return;
        }
        const QString version = QString::fromUtf8(result.standardOutput).trimmed();
        if (version != QString::fromLatin1(supportedRdsVersion)) {
            client->disablePolling(
                {RdsError::Code::IncompatibleCli,
                 QStringLiteral("unsupported rds version '%1'; polling requires rds %2")
                     .arg(version.isEmpty() ? QStringLiteral("unknown") : version,
                          QString::fromLatin1(supportedRdsVersion))});
            return;
        }

        client->m_compatibility = CompatibilityState::Compatible;
        const QList<PendingQuery> pending = std::exchange(client->m_pendingQueries, {});
        for (PendingQuery query : pending) {
            client->startQuery(std::move(query));
        }
    });
}

void RdsClient::disablePolling(RdsError error) {
    m_compatibility = CompatibilityState::Disabled;
    m_disabledError = std::move(error);
    const QList<PendingQuery> pending = std::exchange(m_pendingQueries, {});
    for (PendingQuery query : pending) {
        query.callback(m_disabledError);
    }
}

void RdsClient::startQuery(PendingQuery query) {
    QPointer<RdsClient> client(this);
    const ProcessRequest request = deviceInfoRequest(query.address);
    m_runner->start(request, [client, query = std::move(query)](ProcessResult result) mutable {
        if (!client) {
            return;
        }
        if (client->m_compatibility == CompatibilityState::Disabled) {
            query.callback(client->m_disabledError);
            return;
        }

        RdsError error;
        auto snapshot = parseDeviceInfo(query.address, result, error);
        if (!snapshot) {
            if (error.code == RdsError::Code::IncompatibleCli) {
                client->disablePolling(error);
            }
            query.callback(std::move(error));
            return;
        }

        auto state = std::make_shared<QueryState>();
        state->snapshot = std::move(*snapshot);
        state->callback = std::move(query.callback);
        const std::array<QString, 3> endpoints{
            QString::fromLatin1(activeAppEndpoint),
            QString::fromLatin1(mediaPlayerEndpoint),
            QString::fromLatin1(audioDeviceEndpoint),
        };
        for (const QString& endpoint : endpoints) {
            client->m_runner->start(
                endpointRequest(state->snapshot.address, endpoint),
                [client, state, endpoint](ProcessResult endpointResult) {
                    if (!client) {
                        return;
                    }
                    if (looksLikeGrammarFailure(endpointResult)) {
                        const RdsError grammarError{
                            RdsError::Code::IncompatibleCli,
                            QStringLiteral(
                                "rds 1.0.0 ecp query grammar is incompatible; polling disabled")};
                        client->disablePolling(grammarError);
                        state->fatalError = grammarError;
                    }
                    finishEndpoint(state, endpoint, endpointResult);
                });
        }
    });
}

bool RdsClient::pollingAvailable() const {
    return m_compatibility != CompatibilityState::Disabled;
}

QString RdsClient::diagnostic() const {
    return m_compatibility == CompatibilityState::Disabled ? m_disabledError.message : QString{};
}

} // namespace RokuDeck

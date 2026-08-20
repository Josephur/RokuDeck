// SPDX-License-Identifier: GPL-3.0-or-later
#include "rds_client.h"

#include "active_app_parser.h"
#include "audio_device_parser.h"
#include "device_info_parser.h"
#include "media_player_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <memory>
#include <utility>

namespace RokuDeck {
namespace {

[[nodiscard]] RdsQueryResult parseEnvelope(const QString &address, const ProcessResult &processResult)
{
    if (processResult.timedOut) {
        return RdsError{RdsError::Code::TimedOut, QStringLiteral("rds query timed out")};
    }
    if (processResult.exitCode != 0) {
        return RdsError{RdsError::Code::ProcessFailed, QStringLiteral("rds query failed")};
    }

    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(processResult.standardOutput, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject()) {
        return RdsError{RdsError::Code::MalformedEnvelope, QStringLiteral("rds returned malformed JSON")};
    }
    const QJsonValue resultsValue = document.object().value(QStringLiteral("results"));
    if (!resultsValue.isArray()) {
        return RdsError{RdsError::Code::MalformedEnvelope, QStringLiteral("rds response has no results array")};
    }

    DeviceSnapshot snapshot;
    snapshot.address = address;
    bool foundDeviceInfo = false;
    bool partialFailure = false;

    const QJsonArray results = resultsValue.toArray();
    for (const QJsonValue &value : results) {
        if (!value.isObject()) {
            partialFailure = true;
            continue;
        }
        const QJsonObject result = value.toObject();
        const QString endpoint = result.value(QStringLiteral("endpoint")).toString();
        const int status = result.value(QStringLiteral("status")).toInt(-1);
        const QByteArray body = result.value(QStringLiteral("body")).toString().toUtf8();
        if (endpoint.isEmpty() || status != 200 || body.isEmpty()) {
            partialFailure = true;
            continue;
        }

        if (endpoint == QStringLiteral("/query/device-info")) {
            auto parsed = parseDeviceInfo(body);
            if (!parsed) {
                return RdsError{RdsError::Code::MalformedPayload, parsed.error().message};
            }
            DeviceSnapshot deviceInfo = std::move(parsed.value());
            snapshot.serial = std::move(deviceInfo.serial);
            snapshot.displayName = std::move(deviceInfo.displayName);
            snapshot.power = deviceInfo.power;
            snapshot.capabilities = std::move(deviceInfo.capabilities);
            snapshot.health = deviceInfo.health;
            foundDeviceInfo = true;
        } else if (endpoint == QStringLiteral("/query/active-app")) {
            const auto parsed = parseActiveApp(body);
            if (parsed) {
                snapshot.activeApp = parsed.value();
                snapshot.screensaver = parsed.value().screensaver;
            } else {
                partialFailure = true;
            }
        } else if (endpoint == QStringLiteral("/query/media-player")) {
            const auto parsed = parseMediaPlayer(body);
            if (parsed) {
                snapshot.media = parsed.value();
            } else {
                partialFailure = true;
            }
        } else if (endpoint == QStringLiteral("/query/audio-device")) {
            auto parsed = parseAudioDevice(body);
            if (parsed) {
                snapshot.volume = parsed.value().volume;
                snapshot.muted = parsed.value().muted;
            } else {
                partialFailure = true;
            }
        }
    }

    if (!foundDeviceInfo) {
        return RdsError{RdsError::Code::MissingDeviceInfo, QStringLiteral("rds response has no usable device information")};
    }
    if (partialFailure && snapshot.power == PowerState::On) {
        snapshot.health = DeviceHealth::Degraded;
    }
    snapshot.normalize();
    return snapshot;
}

} // namespace

RdsClient::RdsClient(ProcessRunner *runner)
    : m_runner(runner)
{
    Q_ASSERT(m_runner);
}

void RdsClient::queryDevice(const QString &address, SnapshotCallback callback)
{
    ProcessRequest request;
    request.program = QStringLiteral("rds");
    request.arguments = {QStringLiteral("--json"),
                         QStringLiteral("--quiet"),
                         QStringLiteral("--timeout"),
                         QStringLiteral("3000"),
                         QStringLiteral("query"),
                         address,
                         QStringLiteral("/query/device-info"),
                         QStringLiteral("/query/active-app"),
                         QStringLiteral("/query/media-player"),
                         QStringLiteral("/query/audio-device")};
    request.timeout = std::chrono::milliseconds(3000);

    auto completed = std::make_shared<bool>(false);
    m_runner->start(request, [address, callback = std::move(callback), completed](ProcessResult result) mutable {
        if (*completed) {
            return;
        }
        *completed = true;
        callback(parseEnvelope(address, result));
    });
}

} // namespace RokuDeck

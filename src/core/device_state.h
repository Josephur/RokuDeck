// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>

#include <optional>

namespace RokuDeck {

enum class PowerState { Unknown, On, Standby };
enum class TriState { Unknown, Inactive, Active };
enum class MediaPlaybackState { Unknown, Playing, Paused, Stopped, Idle };
enum class DeviceHealth { Unknown, Online, Standby, Degraded, Offline };

struct ActiveAppState {
    QString id;
    QString name;
    QString type;
    QString input;
    TriState screensaver = TriState::Inactive;
};

struct DeviceSnapshot {
    QString serial;
    QString displayName;
    QString address;
    PowerState power = PowerState::Unknown;
    TriState screensaver = TriState::Unknown;
    std::optional<ActiveAppState> activeApp;
    MediaPlaybackState media = MediaPlaybackState::Unknown;
    std::optional<int> volume;
    std::optional<bool> muted;
    QSet<QString> capabilities;
    QDateTime capturedAt;
    DeviceHealth health = DeviceHealth::Unknown;
    bool partialFailure = false;

    void normalize();
};

} // namespace RokuDeck

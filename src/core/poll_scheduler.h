// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>

namespace RokuDeck {

struct PollInterest {
    QString clientId;
    QString serial;
    QSet<QString> fields;
    std::chrono::milliseconds interval{1000};
};

class PollScheduler final {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Clock = std::function<TimePoint()>;
    using JitterSource = std::function<std::chrono::milliseconds(std::chrono::milliseconds)>;

    explicit PollScheduler(Clock clock = std::chrono::steady_clock::now,
                           JitterSource jitter = [](auto delay) { return delay; });

    void setInterest(const PollInterest &interest);
    bool removeInterest(const QString &clientId, const QString &serial);
    void recordResult(const QString &serial, bool succeeded);

    [[nodiscard]] QStringList dueDevices();
    [[nodiscard]] qsizetype deviceCount() const;
    [[nodiscard]] std::chrono::milliseconds intervalFor(const QString &serial) const;
    [[nodiscard]] std::chrono::milliseconds delayFor(const QString &serial) const;
    [[nodiscard]] QSet<QString> fieldsFor(const QString &serial) const;
    [[nodiscard]] static constexpr std::chrono::milliseconds maximumBackoff()
    {
        return std::chrono::minutes(5);
    }

private:
    struct Schedule {
        std::chrono::milliseconds interval{1000};
        std::chrono::milliseconds delay{1000};
        TimePoint nextDue{};
        QSet<QString> fields;
        unsigned int failures = 0;
    };

    void rebuildSchedule(const QString &serial);

    Clock m_clock;
    JitterSource m_jitter;
    QHash<QString, QHash<QString, PollInterest>> m_interestsByClient;
    QHash<QString, Schedule> m_schedules;
    QSet<QString> m_inFlightSerials;
};

} // namespace RokuDeck

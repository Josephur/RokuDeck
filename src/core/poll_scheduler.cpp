// SPDX-License-Identifier: GPL-3.0-or-later
#include "poll_scheduler.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace RokuDeck {

PollScheduler::PollScheduler(Clock clock, JitterSource jitter)
    : m_clock(std::move(clock))
    , m_jitter(std::move(jitter))
{
}

void PollScheduler::setInterest(const PollInterest &interest)
{
    if (interest.clientId.isEmpty() || interest.serial.isEmpty()) {
        return;
    }
    m_interestsByClient[interest.clientId].insert(interest.serial, interest);
    rebuildSchedule(interest.serial);
}

void PollScheduler::rebuildSchedule(const QString &serial)
{
    std::chrono::milliseconds shortest = maximumBackoff();
    QSet<QString> fields;
    bool found = false;
    for (auto clientIt = m_interestsByClient.cbegin(); clientIt != m_interestsByClient.cend(); ++clientIt) {
        const auto interestIt = clientIt->constFind(serial);
        if (interestIt == clientIt->cend()) {
            continue;
        }
        found = true;
        shortest = std::min(shortest, std::max(interestIt->interval, std::chrono::milliseconds(1)));
        fields.unite(interestIt->fields);
    }
    if (!found) {
        m_schedules.remove(serial);
        return;
    }

    Schedule &schedule = m_schedules[serial];
    schedule.interval = shortest;
    schedule.delay = shortest;
    schedule.nextDue = m_clock() + shortest;
    schedule.fields = std::move(fields);
    schedule.failures = 0;
}

void PollScheduler::recordResult(const QString &serial, bool succeeded)
{
    auto scheduleIt = m_schedules.find(serial);
    if (scheduleIt == m_schedules.end()) {
        return;
    }
    Schedule &schedule = scheduleIt.value();
    if (succeeded) {
        schedule.failures = 0;
        schedule.delay = schedule.interval;
    } else {
        schedule.failures = std::min(schedule.failures + 1U, 30U);
        const unsigned int shift = std::min(schedule.failures, 20U);
        const auto multiplier = static_cast<std::chrono::milliseconds::rep>(1ULL << shift);
        const auto maxCount = maximumBackoff().count();
        const auto intervalCount = schedule.interval.count();
        const auto baseCount = intervalCount > maxCount / multiplier ? maxCount : intervalCount * multiplier;
        const auto jittered = m_jitter(std::chrono::milliseconds(baseCount));
        schedule.delay = std::clamp(jittered, std::chrono::milliseconds::zero(), maximumBackoff());
    }
    schedule.nextDue = m_clock() + schedule.delay;
}

QStringList PollScheduler::dueDevices() const
{
    QStringList due;
    const TimePoint now = m_clock();
    for (auto it = m_schedules.cbegin(); it != m_schedules.cend(); ++it) {
        if (it->nextDue <= now) {
            due.append(it.key());
        }
    }
    due.sort();
    return due;
}

qsizetype PollScheduler::deviceCount() const
{
    return m_schedules.size();
}

std::chrono::milliseconds PollScheduler::intervalFor(const QString &serial) const
{
    return m_schedules.value(serial).interval;
}

std::chrono::milliseconds PollScheduler::delayFor(const QString &serial) const
{
    return m_schedules.value(serial).delay;
}

QSet<QString> PollScheduler::fieldsFor(const QString &serial) const
{
    return m_schedules.value(serial).fields;
}

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#include "poll_scheduler.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace RokuDeck {
namespace {

[[nodiscard]] PollScheduler::TimePoint saturatingAdd(PollScheduler::TimePoint start,
                                                     std::chrono::milliseconds delay)
{
    if (delay <= std::chrono::milliseconds::zero()) {
        return start;
    }

    using ClockDuration = PollScheduler::TimePoint::duration;
    using ClockRep = ClockDuration::rep;
    const std::chrono::duration<long double, ClockDuration::period> convertedDelay = delay;
    constexpr ClockRep maximumCount = std::numeric_limits<ClockRep>::max();
    if (convertedDelay.count() >= static_cast<long double>(maximumCount)) {
        return PollScheduler::TimePoint::max();
    }

    const ClockRep delayCount = std::chrono::duration_cast<ClockDuration>(delay).count();
    const ClockRep startCount = start.time_since_epoch().count();
    if (startCount > maximumCount - delayCount) {
        return PollScheduler::TimePoint::max();
    }
    return PollScheduler::TimePoint(ClockDuration(startCount + delayCount));
}

} // namespace

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

bool PollScheduler::removeInterest(const QString &clientId, const QString &serial)
{
    auto clientIt = m_interestsByClient.find(clientId);
    if (clientIt == m_interestsByClient.end() || clientIt->remove(serial) == 0) {
        return false;
    }
    if (clientIt->isEmpty()) {
        m_interestsByClient.erase(clientIt);
    }
    rebuildSchedule(serial);
    return true;
}

void PollScheduler::rebuildSchedule(const QString &serial)
{
    std::chrono::milliseconds shortest = std::chrono::milliseconds::max();
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

    auto scheduleIt = m_schedules.find(serial);
    if (scheduleIt == m_schedules.end()) {
        Schedule schedule;
        schedule.interval = shortest;
        schedule.delay = shortest;
        schedule.nextDue = saturatingAdd(m_clock(), shortest);
        schedule.fields = std::move(fields);
        m_schedules.insert(serial, std::move(schedule));
        return;
    }

    Schedule &schedule = scheduleIt.value();
    const bool intervalChanged = schedule.interval != shortest;
    schedule.interval = shortest;
    schedule.fields = std::move(fields);
    if (intervalChanged && schedule.failures == 0) {
        schedule.delay = shortest;
        const TimePoint earlierDeadline = saturatingAdd(m_clock(), shortest);
        schedule.nextDue = std::min(schedule.nextDue, earlierDeadline);
    }
}

bool PollScheduler::recordResult(const PollClaim &claim, bool succeeded)
{
    const auto activeClaimIt = m_activeClaims.constFind(claim.serial);
    if (claim.token == 0 || activeClaimIt == m_activeClaims.cend() || activeClaimIt.value() != claim.token) {
        return false;
    }
    m_activeClaims.remove(claim.serial);

    auto scheduleIt = m_schedules.find(claim.serial);
    if (scheduleIt == m_schedules.end()) {
        return true;
    }
    Schedule &schedule = scheduleIt.value();
    if (succeeded) {
        schedule.failures = 0;
        schedule.delay = schedule.interval;
    } else {
        schedule.failures = std::min(schedule.failures + 1U, 30U);
        const unsigned int shift = std::min(schedule.failures, 20U);
        const auto multiplier = static_cast<std::chrono::milliseconds::rep>(1ULL << shift);
        const auto backoffCeiling = std::max(maximumBackoff(), schedule.interval);
        const auto maxCount = backoffCeiling.count();
        const auto intervalCount = schedule.interval.count();
        const auto baseCount = intervalCount > maxCount / multiplier ? maxCount : intervalCount * multiplier;
        const auto jittered = m_jitter(std::chrono::milliseconds(baseCount));
        schedule.delay = std::clamp(jittered, std::chrono::milliseconds::zero(), backoffCeiling);
    }
    schedule.nextDue = saturatingAdd(m_clock(), schedule.delay);
    return true;
}

QList<PollClaim> PollScheduler::claimDuePolls()
{
    QList<PollClaim> due;
    const TimePoint now = m_clock();
    for (auto it = m_schedules.begin(); it != m_schedules.end(); ++it) {
        if (!m_activeClaims.contains(it.key()) && it->nextDue <= now) {
            if (m_nextClaimToken == 0) {
                break;
            }
            const quint64 token = m_nextClaimToken++;
            due.append({it.key(), token});
            m_activeClaims.insert(it.key(), token);
        }
    }
    std::ranges::sort(due, {}, &PollClaim::serial);
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

std::optional<PollScheduler::TimePoint> PollScheduler::nextDueFor(const QString &serial) const
{
    const auto scheduleIt = m_schedules.constFind(serial);
    if (scheduleIt == m_schedules.cend()) {
        return std::nullopt;
    }
    return scheduleIt->nextDue;
}

QSet<QString> PollScheduler::fieldsFor(const QString &serial) const
{
    return m_schedules.value(serial).fields;
}

} // namespace RokuDeck

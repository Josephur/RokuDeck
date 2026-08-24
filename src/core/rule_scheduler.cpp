// SPDX-License-Identifier: GPL-3.0-or-later
#include "rule_scheduler.h"

#include <algorithm>

namespace RokuDeck {
namespace {

constexpr qint64 oneDayMs = 24ll * 60 * 60 * 1000;

[[nodiscard]] bool sameTrigger(const RuleScheduler::Entry& entry,
                               const TriggerSpec& trigger) {
    if (entry.type != trigger.type) {
        return false;
    }
    if (entry.type == QStringLiteral("interval")) {
        return entry.minutes == trigger.minutes;
    }
    const bool sameKind = entry.timeKind == trigger.timeKind;
    const bool sameTime = entry.fireTime == QTime::fromString(trigger.time,
                                                              QStringLiteral("HH:mm"));
    if (entry.timeKind != QStringLiteral("once")) {
        return sameKind && sameTime;
    }
    return sameKind && sameTime &&
           entry.fireDate == QDate::fromString(trigger.date, QStringLiteral("yyyy-MM-dd"));
}

} // namespace

void RuleScheduler::updateRules(const QList<RuleDocument>& rules, QDateTime now) {
    rebuild(rules, now, false);
}

void RuleScheduler::updateRulesAtStartup(const QList<RuleDocument>& rules, QDateTime now) {
    rebuild(rules, now, true);
}

void RuleScheduler::rebuild(const QList<RuleDocument>& rules, QDateTime now,
                            bool catchUpMissedOnce) {
    QList<Entry> rebuilt;
    for (const RuleDocument& rule : rules) {
        const bool scheduled = rule.enabled &&
            (rule.trigger.type == QStringLiteral("time") ||
             rule.trigger.type == QStringLiteral("interval"));
        if (!scheduled) {
            continue;
        }
        const auto existing = std::find_if(m_entries.cbegin(), m_entries.cend(),
            [&rule](const Entry& entry) {
                return entry.id == rule.id && sameTrigger(entry, rule.trigger);
            });
        if (existing != m_entries.cend()) {
            rebuilt.append(*existing);  // unchanged: keep the anchor
            continue;
        }
        Entry entry;
        seed(entry, rule, now, catchUpMissedOnce);
        if (entry.nextFire.isValid()) {
            rebuilt.append(std::move(entry));
        }
    }
    m_entries = std::move(rebuilt);
}

void RuleScheduler::seed(Entry& entry, const RuleDocument& rule, QDateTime now,
                         bool catchUpMissedOnce) {
    entry.id = rule.id;
    entry.type = rule.trigger.type;
    if (entry.type == QStringLiteral("interval")) {
        entry.minutes = rule.trigger.minutes;
        entry.nextFire = now.addSecs(entry.minutes * 60);
        return;
    }
    entry.timeKind = rule.trigger.timeKind;
    entry.fireTime = QTime::fromString(rule.trigger.time, QStringLiteral("HH:mm"));
    if (entry.timeKind == QStringLiteral("once")) {
        entry.fireDate = QDate::fromString(rule.trigger.date, QStringLiteral("yyyy-MM-dd"));
        const QDateTime instant(entry.fireDate, entry.fireTime);
        const QDateTime firedAt =
            QDateTime::fromString(rule.lastRun.firedAtIso, Qt::ISODateWithMs);
        if (firedAt.isValid() && firedAt >= instant) {
            return;  // already consumed
        }
        if (instant > now) {
            entry.nextFire = instant;
        } else if (catchUpMissedOnce && instant.msecsTo(now) < oneDayMs) {
            entry.nextFire = instant;  // missed while down: catch up once
        } else {
            return;  // stale, or added mid-session after its time
        }
        return;
    }
    // daily / weekdays: first occurrence strictly after now.
    entry.nextFire = QDateTime(now.date(), entry.fireTime);
    if (entry.nextFire <= now) {
        entry.nextFire = QDateTime(now.date().addDays(1), entry.fireTime);
    }
    if (entry.timeKind == QStringLiteral("weekdays")) {
        while (entry.nextFire.date().dayOfWeek() > 5 || entry.nextFire <= now) {
            entry.nextFire = QDateTime(entry.nextFire.date().addDays(1), entry.fireTime);
        }
    }
}

void RuleScheduler::advance(Entry& entry, QDateTime now) {
    if (entry.type == QStringLiteral("interval")) {
        entry.nextFire = now.addSecs(entry.minutes * 60);
        return;
    }
    // time triggers: walk to the first occurrence strictly after now.
    do {
        entry.nextFire = QDateTime(entry.nextFire.date().addDays(1), entry.fireTime);
    } while (entry.nextFire <= now ||
             (entry.timeKind == QStringLiteral("weekdays") &&
              entry.nextFire.date().dayOfWeek() > 5));
}

QList<QString> RuleScheduler::dueRuleIds(QDateTime now) {
    QList<QString> due;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        Entry& entry = *it;
        if (!entry.nextFire.isValid()) {
            it = m_entries.erase(it);
            continue;
        }
        if (entry.nextFire <= now) {
            due.append(entry.id);
            if (entry.timeKind == QStringLiteral("once")) {
                it = m_entries.erase(it);  // consumed
                continue;
            }
            advance(entry, now);  // one firing per entry per evaluation
        }
        ++it;
    }
    return due;
}

QDateTime RuleScheduler::nextDeadline() const {
    QDateTime earliest;
    for (const Entry& entry : m_entries) {
        if (entry.nextFire.isValid() &&
            (!earliest.isValid() || entry.nextFire < earliest)) {
            earliest = entry.nextFire;
        }
    }
    return earliest;
}

} // namespace RokuDeck

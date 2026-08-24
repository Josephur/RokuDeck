// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rule_model.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTime>

namespace RokuDeck {

// Headless wall-clock scheduler for time/interval rule triggers. Decides
// which rules are due at a given instant; DaemonService polls it.
class RuleScheduler {
  public:
    // Public so rule_scheduler.cpp's free sameTrigger() helper can name
    // RuleScheduler::Entry.
    struct Entry {
        QString id;
        QString type;      // time | interval
        QString timeKind;  // daily | weekdays | once
        QTime fireTime;    // local wall time
        QDate fireDate;    // once
        int minutes = 0;   // interval
        QDateTime nextFire;
    };

    // Announce-time reseed: `once` whose fire time already passed is skipped.
    void updateRules(const QList<RuleDocument>& rules, QDateTime now);
    // Startup reseed: `once` missed by less than 24h fires once at startup;
    // older ones are stale and skipped.
    void updateRulesAtStartup(const QList<RuleDocument>& rules, QDateTime now);
    // Enabled rules whose fire time is at or before `now` and not yet
    // reported, in rule-list order. One firing per entry per call; anchors
    // advance so a repeated call with the same `now` returns nothing.
    [[nodiscard]] QList<QString> dueRuleIds(QDateTime now);
    // Earliest still-pending fire time; invalid when nothing is pending.
    [[nodiscard]] QDateTime nextDeadline() const;

  private:
    void rebuild(const QList<RuleDocument>& rules, QDateTime now, bool catchUpMissedOnce);
    void seed(Entry& entry, const RuleDocument& rule, QDateTime now, bool catchUpMissedOnce);
    void advance(Entry& entry, QDateTime now);

    QList<Entry> m_entries;  // rule-list order
};

} // namespace RokuDeck

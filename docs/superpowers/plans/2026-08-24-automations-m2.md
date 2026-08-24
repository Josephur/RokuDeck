# Automations M2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The daemon fires enabled `time` (daily/weekdays/once) and `interval` triggers automatically through the exact M1 execution pipeline — with the spec §4.2 missed-fire policy — and stays alive while schedules are pending.

**Architecture:** A headless `RuleScheduler` in `src/core` holds per-rule anchors and answers three questions over an injected wall clock: which rules are due, when is the next deadline, and how to reseed when the rule list changes. `DaemonService` owns it plus a single rearmed `QTimer` (60 s cap = sleep/wake and DST recheck), refactors `RunRule`'s body into a shared `executeRuleRun`, and counts a pending schedule as accepted work so the daemon never idles out under an armed timer.

**Tech Stack:** C++20 (no exceptions, `QT_NO_CAST_FROM_ASCII`), Qt 6 Core/DBus/Test, KConfig, QtTest.

**Spec:** `docs/superpowers/specs/2026-08-23-automations-design.md` (§3 wall-clock note, §4.2 timer wheel + missed-fire policy, §8 testing)
**Prior work:** `docs/superpowers/plans/2026-08-23-automations-m1.md` (completed; ledger `.superpowers/sdd/2026-08-23-automations-m1/progress.md`)

## Global Constraints

- All string literals: `QStringLiteral(...)` or `_s` (tests: `using namespace Qt::Literals::StringLiterals;`). ASCII casts are compile errors.
- Build is `-Werror`; no exceptions in daemon/core targets.
- Every task ends green under `ctest` from `build/dev` before committing (suite grows 15/15 → 16/16 with the new `rule-scheduler-test` target).
- Commit messages follow repo convention: `type(scope): lowercase imperative summary`.

## M2 Rulings (user-confirmed + plan decisions)

1. **Interval restart:** re-anchor at daemon start; no catch-up, no burst. After each firing the cadence re-anchors to the fire instant. (User-confirmed)
2. **Idle:** a pending schedule counts as accepted work — `m_scheduleTimer.isActive()` joins `hasAcceptedWork()`, so enabled schedules keep the daemon alive. (User-confirmed)
3. **`once` fired marker:** the persisted `lastRun` record. Manual `RunRule` on a once-rule consumes it — documented edge, no schema change. (User-confirmed)
4. **Mode gate + condition evaluation:** deferred to M3/M4; M2 dispatches through the M1 path unchanged. (User-confirmed)
5. **Missed `daily`/`weekdays`:** never backfilled (alarm-clock semantics per spec §3 note); only `once` has catch-up.
6. **Mid-session overdue `once`:** a once-rule *added* after its fire time never fires (catch-up applies only at startup, for rules loaded from disk). Conservative; avoids surprising immediate firings.
7. **Clock injection:** `DaemonService` gains a `WallClock` ctor param (defaults to `QDateTime::currentDateTime()`); `lastRun.firedAtIso` stays real UTC (`currentDateTimeUtc()`) — zero M1 drift.
8. **Timer shape:** one rearmed single-shot `QTimer` (60 s cap) instead of spec's "one QTimer per pending schedule" — same semantics, simpler; the 60 s cap replaces the "re-check on every snapshot tick" wake protection.
9. **No D-Bus contract changes** in M2; the scheduler is purely internal.
10. **Interval anti-burst:** at most one interval firing per `dueRuleIds` evaluation, even if the daemon slept many intervals.

## File Structure

- Create: `src/core/rule_scheduler.h` / `src/core/rule_scheduler.cpp` — pure headless scheduling logic.
- Modify: `src/core/CMakeLists.txt` — append `rule_scheduler.cpp` to the core target (pattern from M1 T1).
- Modify: `src/daemon/daemon_service.h` / `.cpp` — clock injection, timer, wiring, `executeRuleRun` refactor, idle protection.
- Create: `tests/unit/rule_scheduler_test.cpp`; Modify: `tests/CMakeLists.txt` — register `rule-scheduler-test` after `rule-model-test`.
- Modify: `tests/integration/daemon_dbus_test.cpp` — harness fake clock + 5 new tests.
- Modify: `CHANGELOG.md`; Create: `.superpowers/sdd/2026-08-24-automations-m2/progress.md` (ledger updated per task).

---

### Task 1: RuleScheduler — time triggers + once missed-fire policy

**Files:**
- Create: `src/core/rule_scheduler.h`, `src/core/rule_scheduler.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: Create `tests/unit/rule_scheduler_test.cpp`; Modify `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `RuleDocument`/`TriggerSpec` from `src/core/rule_model.h`.
- Produces (Tasks 2–4):
  ```cpp
  namespace RokuDeck {
  class RuleScheduler {
    public:
      // Announce-time reseed: `once` whose fire time already passed is skipped.
      void updateRules(const QList<RuleDocument>& rules, QDateTime now);
      // Startup reseed: `once` missed by less than 24h fires once at startup;
      // older ones are stale and skipped. (Task 4 wires this into the ctor.)
      void updateRulesAtStartup(const QList<RuleDocument>& rules, QDateTime now);
      // Enabled rules whose fire time is at or before `now` and not yet
      // reported, in rule-list order. One firing per entry per call; anchors
      // advance so a repeated call with the same `now` returns nothing.
      [[nodiscard]] QList<QString> dueRuleIds(QDateTime now);
      // Earliest still-pending fire time; invalid when nothing is pending.
      [[nodiscard]] QDateTime nextDeadline() const;
    private:
      struct Entry {
          QString id;
          QString type;      // time | interval
          QString timeKind;  // daily | weekdays | once
          QTime fireTime;    // local wall time
          QDate fireDate;    // once
          int minutes = 0;   // interval
          QDateTime nextFire;
      };
      void rebuild(const QList<RuleDocument>& rules, QDateTime now, bool catchUpMissedOnce);
      void seed(Entry& entry, const RuleDocument& rule, QDateTime now, bool catchUpMissedOnce);
      void advance(Entry& entry, QDateTime now);
      QList<Entry> m_entries;  // rule-list order
  };
  }
  ```

- [ ] **Step 1: Register the test target**

In `tests/CMakeLists.txt`, immediately after the `rule-model-test` block (lines 26–33):

```cmake
add_executable(rule-scheduler-test unit/rule_scheduler_test.cpp)
target_include_directories(rule-scheduler-test
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(rule-scheduler-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME rule-scheduler COMMAND rule-scheduler-test)
```

- [ ] **Step 2: Write the failing tests**

```cpp
// tests/unit/rule_scheduler_test.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/rule_model.h"
#include "core/rule_scheduler.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace RokuDeck;
using namespace Qt::Literals::StringLiterals;

namespace {

[[nodiscard]] RuleDocument rule(const QString& id, const QString& name,
                                const QJsonObject& trigger, bool enabled = true) {
    const auto parsed = parseRule(QJsonObject{{"schemaVersion"_L1, 1},
                                              {"id"_L1, id},
                                              {"name"_L1, name},
                                              {"enabled"_L1, enabled},
                                              {"trigger"_L1, trigger},
                                              {"actions"_L1, QJsonArray{
                                                  QJsonObject{{"type"_L1, "keypress"_L1},
                                                              {"key"_L1, "Home"_L1}}}}});
    Q_ASSERT(parsed.has_value());
    return parsed.value();
}

[[nodiscard]] QDateTime at(const QDate& date, const QString& hhmm) {
    return QDateTime(date, QTime::fromString(hhmm, QStringLiteral("HH:mm")));
}

[[nodiscard]] QJsonObject dailyTrigger() {
    return QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "daily"_L1},
                       {"time"_L1, "23:00"_L1}};
}

} // namespace

class RuleSchedulerTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void dailyRuleFiresAtItsWallClockTimeAndRepeats();
    void weekdayRuleSkipsWeekendDays();
    void missedOnceRuleFiresAtStartupWhileFresh();
    void staleOnceRuleIsSkipped();
    void consumedOnceRuleNeverRefires();
    void overdueOnceRuleAddedMidSessionNeverFires();
    void disabledAndUnscheduledRulesAreIgnored();
};

void RuleSchedulerTest::dailyRuleFiresAtItsWallClockTimeAndRepeats() {
    RuleScheduler scheduler;
    const RuleDocument daily = rule(QStringLiteral("daily"_L1),
                                    QStringLiteral("Nightly"_L1), dailyTrigger());
    QDateTime now = at(QDate(2026, 1, 5), QStringLiteral("22:59"));
    scheduler.updateRules({daily}, now);
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());  // one minute early
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 5), QStringLiteral("23:00")));

    now = at(QDate(2026, 1, 5), QStringLiteral("23:00"));
    QCOMPARE(scheduler.dueRuleIds(now), QStringList{QStringLiteral("daily"_L1)});
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());  // idempotent per occurrence
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 6), QStringLiteral("23:00")));

    // A long gap (daemon asleep for days) never backfills: one firing, then
    // the next occurrence strictly after the evaluation instant.
    now = at(QDate(2026, 1, 9), QStringLiteral("23:00"));
    QCOMPARE(scheduler.dueRuleIds(now), QStringList{QStringLiteral("daily"_L1)});
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());
}

void RuleSchedulerTest::weekdayRuleSkipsWeekendDays() {
    RuleScheduler scheduler;
    const RuleDocument weekdays = rule(QStringLiteral("week"_L1),
        QStringLiteral("Weeknight"_L1),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "weekdays"_L1},
                    {"time"_L1, "23:00"_L1}});
    // Friday 2026-01-09: fires that evening, next deadline Monday.
    scheduler.updateRules({weekdays}, at(QDate(2026, 1, 9), QStringLiteral("22:00")));
    QCOMPARE(scheduler.dueRuleIds(at(QDate(2026, 1, 9), QStringLiteral("23:00"))),
             QStringList{QStringLiteral("week"_L1)});
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 12), QStringLiteral("23:00")));

    // Seeded on a Saturday: the first occurrence lands Monday, not the weekend.
    RuleScheduler weekendSeed;
    weekendSeed.updateRules({weekdays}, at(QDate(2026, 1, 10), QStringLiteral("08:00")));
    QCOMPARE(weekendSeed.nextDeadline(), at(QDate(2026, 1, 12), QStringLiteral("23:00")));
}

void RuleSchedulerTest::missedOnceRuleFiresAtStartupWhileFresh() {
    RuleScheduler scheduler;
    const RuleDocument once = rule(QStringLiteral("once"_L1), QStringLiteral("Morning"_L1),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-05"_L1}});
    // Daemon boots 90 minutes late: fires once at startup, then is consumed.
    const QDateTime startup = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRulesAtStartup({once}, startup);
    QCOMPARE(scheduler.dueRuleIds(startup), QStringList{QStringLiteral("once"_L1)});
    QVERIFY(scheduler.dueRuleIds(startup).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::staleOnceRuleIsSkipped() {
    RuleScheduler scheduler;
    const RuleDocument stale = rule(QStringLiteral("stale"_L1), QStringLiteral("Old"_L1),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-03"_L1}});
    const QDateTime startup = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRulesAtStartup({stale}, startup);
    QVERIFY(scheduler.dueRuleIds(startup).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::consumedOnceRuleNeverRefires() {
    RuleScheduler scheduler;
    RuleDocument consumed = rule(QStringLiteral("done"_L1), QStringLiteral("Done"_L1),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-05"_L1}});
    consumed.lastRun.firedAtIso = QStringLiteral("2026-01-05T08:00:00.000Z"_L1);
    consumed.lastRun.outcome = QStringLiteral("ok"_L1);
    const QDateTime startup = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRulesAtStartup({consumed}, startup);
    QVERIFY(scheduler.dueRuleIds(startup).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::overdueOnceRuleAddedMidSessionNeverFires() {
    RuleScheduler scheduler;
    const RuleDocument once = rule(QStringLiteral("late"_L1), QStringLiteral("Late"_L1),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-05"_L1}});
    const QDateTime now = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRules({once}, now);  // announce-time reseed: no catch-up
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::disabledAndUnscheduledRulesAreIgnored() {
    RuleScheduler scheduler;
    const RuleDocument disabled = rule(QStringLiteral("off"_L1),
        QStringLiteral("Disabled"_L1), dailyTrigger(), false);
    const RuleDocument manual = rule(QStringLiteral("man"_L1),
        QStringLiteral("Manual"_L1), QJsonObject{{"type"_L1, "manual"_L1}});
    scheduler.updateRules({disabled, manual}, at(QDate(2026, 1, 5), QStringLiteral("22:00")));
    QVERIFY(!scheduler.nextDeadline().isValid());
}

QTEST_MAIN(RuleSchedulerTest)
#include "rule_scheduler_test.moc"
```

(Test calendar: 2026-01-05 is a Monday, 2026-01-09 Friday, 2026-01-10 Saturday.)

- [ ] **Step 3: Run to verify RED**

Run: `cmake --build build/dev --target rule-scheduler-test 2>&1 | tail -5`
Expected: FAIL — `core/rule_scheduler.h` does not exist.

- [ ] **Step 4: Implement the header and logic**

Create `src/core/rule_scheduler.h` exactly as the Interfaces block above (plus `#pragma once`, SPDX header, `#include "rule_model.h"`, `<QDate> <QDateTime> <QList> <QString> <QTime>`).

Implement `src/core/rule_scheduler.cpp` (add `rule_scheduler.cpp` to `src/core/CMakeLists.txt`):

```cpp
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
```

Notes: `QDate::dayOfWeek()` is 1 (Mon) … 7 (Sun), so `> 5` is Saturday/Sunday. `QDateTime(QDate, QTime)` constructs local wall time — the alarm-clock semantics of spec §3 by construction, including Qt's DST-gap normalization. The `Entry` struct must be public inside the class (or the free `sameTrigger` helper becomes a private static member) so it can name `RuleScheduler::Entry`.

- [ ] **Step 5: Build and run to green**

Run: `cmake --build build/dev && cd build/dev && ctest -R rule-scheduler --output-on-failure`
Expected: PASS (7 cases).

- [ ] **Step 6: Commit**

```bash
git add src/core/rule_scheduler.h src/core/rule_scheduler.cpp src/core/CMakeLists.txt \
        tests/unit/rule_scheduler_test.cpp tests/CMakeLists.txt
git commit -m "feat(core): wall-clock time trigger scheduler with missed-once catch-up"
```

---

### Task 2: RuleScheduler — interval cadence, re-anchoring, anchor preservation

**Files:**
- Test: Modify `tests/unit/rule_scheduler_test.cpp`
- Modify: `src/core/rule_scheduler.cpp` (interval branches if not already present)

**Interfaces:**
- Consumes/Produces: Task 1's `RuleScheduler` unchanged.

- [ ] **Step 1: Add the interval tests**

Add to the test class:

```cpp
  private Q_SLOTS:
    // ... existing ...
    void intervalRuleFiresOnCadenceAndReanchorsToTheFireInstant();
    void intervalRuleNeverBurstsAfterLongGaps();
    void intervalRuleReanchorsAcrossRestart();
    void unchangedTriggersKeepAnchorsAcrossUpdates();

void RuleSchedulerTest::intervalRuleFiresOnCadenceAndReanchorsToTheFireInstant() {
    RuleScheduler scheduler;
    const RuleDocument cadence = rule(QStringLiteral("cadence"_L1),
        QStringLiteral("Nudge"_L1), QJsonObject{{"type"_L1, "interval"_L1},
                                                {"minutes"_L1, 30}});
    const QDateTime boot = at(QDate(2026, 1, 5), QStringLiteral("08:00"));
    scheduler.updateRules({cadence}, boot);
    QVERIFY(scheduler.dueRuleIds(boot).isEmpty());   // not due at boot
    QCOMPARE(scheduler.nextDeadline(), boot.addSecs(30 * 60));

    const QDateTime firstFire = boot.addSecs(30 * 60);
    QCOMPARE(scheduler.dueRuleIds(firstFire), QStringList{QStringLiteral("cadence"_L1)});
    QVERIFY(scheduler.dueRuleIds(firstFire).isEmpty());  // idempotent
    QCOMPARE(scheduler.nextDeadline(), firstFire.addSecs(30 * 60));
}

void RuleSchedulerTest::intervalRuleNeverBurstsAfterLongGaps() {
    RuleScheduler scheduler;
    const RuleDocument cadence = rule(QStringLiteral("cadence"_L1),
        QStringLiteral("Nudge"_L1), QJsonObject{{"type"_L1, "interval"_L1},
                                                {"minutes"_L1, 30}});
    const QDateTime boot = at(QDate(2026, 1, 5), QStringLiteral("08:00"));
    scheduler.updateRules({cadence}, boot);

    // The daemon sleeps 3 intervals: exactly one firing, then the cadence
    // restarts from the evaluation instant (anti-burst).
    const QDateTime late = boot.addSecs(3 * 30 * 60);
    QCOMPARE(scheduler.dueRuleIds(late), QStringList{QStringLiteral("cadence"_L1)});
    QVERIFY(scheduler.dueRuleIds(late).isEmpty());
    QCOMPARE(scheduler.nextDeadline(), late.addSecs(30 * 60));
}

void RuleSchedulerTest::intervalRuleReanchorsAcrossRestart() {
    const RuleDocument cadence = rule(QStringLiteral("cadence"_L1),
        QStringLiteral("Nudge"_L1), QJsonObject{{"type"_L1, "interval"_L1},
                                                {"minutes"_L1, 30}});
    RuleScheduler first;
    first.updateRules({cadence}, at(QDate(2026, 1, 5), QStringLiteral("08:00")));

    // Restart: a fresh scheduler instance sees the same rule list. No
    // catch-up, the cadence starts over from boot.
    RuleScheduler second;
    const QDateTime restart = at(QDate(2026, 1, 5), QStringLiteral("09:10"));
    second.updateRulesAtStartup({cadence}, restart);
    QVERIFY(second.dueRuleIds(restart).isEmpty());
    QCOMPARE(second.nextDeadline(), restart.addSecs(30 * 60));
}

void RuleSchedulerTest::unchangedTriggersKeepAnchorsAcrossUpdates() {
    RuleScheduler scheduler;
    const RuleDocument cadence = rule(QStringLiteral("cadence"_L1),
        QStringLiteral("Nudge"_L1), QJsonObject{{"type"_L1, "interval"_L1},
                                                {"minutes"_L1, 30}});
    QDateTime now = at(QDate(2026, 1, 5), QStringLiteral("08:00"));
    scheduler.updateRules({cadence}, now);
    QCOMPARE(scheduler.nextDeadline(), now.addSecs(30 * 60));

    // Announce-time reseeds (e.g. after a lastRun record is written) must not
    // re-anchor an unchanged trigger.
    now = at(QDate(2026, 1, 5), QStringLiteral("08:10"));
    scheduler.updateRules({cadence}, now);
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 5), QStringLiteral("08:30")));

    // Changing the trigger reseeds from the new evaluation instant.
    RuleDocument faster = cadence;
    faster.trigger.minutes = 5;
    scheduler.updateRules({faster}, now);
    QCOMPARE(scheduler.nextDeadline(), now.addSecs(5 * 60));
}
```

- [ ] **Step 2: Run to verify RED**

Run: `cd build/dev && ctest -R rule-scheduler --output-on-failure`
Expected: FAIL — interval entries never fire (`seed`/`advance`/`dueRuleIds` lack the interval branch).

- [ ] **Step 3: Implement**

In `src/core/rule_scheduler.cpp`, add the interval branches to `seed`, `advance`, and `dueRuleIds` exactly as shown in Task 1's Step 4 code (that code is complete; this task's diff is small because the branches were drafted together — if the implementer already included them, Step 2's RED is the compile-free variant and this task is a pure test-hardening commit, noted in the ledger).

- [ ] **Step 4: Run to green**

Run: `cd build/dev && ctest -R rule-scheduler --output-on-failure`
Expected: PASS (11 cases).

- [ ] **Step 5: Commit**

```bash
git add tests/unit/rule_scheduler_test.cpp src/core/rule_scheduler.cpp
git commit -m "feat(core): interval trigger cadence with restart re-anchoring"
```

---

### Task 3: DaemonService — clock injection, schedule timer, shared execution path

**Files:**
- Modify: `src/daemon/daemon_service.h`, `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Interfaces:**
- Consumes: `RuleScheduler` (Tasks 1–2), existing `RunRule` body, `findRule`, `announceRules`, `updateIdleTimer`.
- Produces (Task 4 relies on):
  ```cpp
  public:
      using WallClock = std::function<QDateTime()>;
      // ctor gains: WallClock wallClock = {}  (before QObject* parent)
      void evaluateDueRules();               // timer target + deterministic test seam
  private:
      void rearmScheduleTimer();
      [[nodiscard]] QString executeRuleRun(const QString& ruleId);
      QTimer m_scheduleTimer;
      RuleScheduler m_ruleScheduler;
      WallClock m_wallClock;
      bool m_dispatchActive = false;
  ```

- [ ] **Step 1: Failing integration tests + harness clock**

Harness changes:
- Member: `QDateTime m_now;` (next to `m_automationEnabled`).
- `init()`: set `m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 0));` **before** `recreateDaemon()`, then call `recreateDaemon(30s, {}, [this] { return m_now; });`.
- `recreateDaemon` signature gains a third defaulted param and passes it through:

```cpp
void DaemonDbusTest::recreateDaemon(std::chrono::milliseconds idleGrace = 30s,
                                    DaemonService::DeadlineRemaining deadlineRemaining = {},
                                    DaemonService::WallClock wallClock = {}) {
    ...
    m_daemon = std::make_unique<DaemonService>(
        QDBusConnection::sessionBus(), [this] { return m_automationEnabled; }, std::move(runner),
        std::move(configStore), idleGrace, std::move(deadlineRemaining), std::move(wallClock));
    ...
}
```

New slots in the `private Q_SLOTS:` block:
```cpp
    void scheduledDailyRuleFiresThroughTheCommandPipeline();
    void disablingARuleUnschedulesItsNextFire();
```

Test bodies (mirror the M1 patterns at lines 1718–1785 of `tests/integration/daemon_dbus_test.cpp`: `upsertDevice` defaults `allowAutomations=true`; the fake runner records the request without needing completion):

```cpp
void DaemonDbusTest::scheduledDailyRuleFiresThroughTheCommandPipeline() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.80")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nightly home","trigger":{"type":"time","kind":"daily","time":"09:00"})json"
        R"json(,"actions":[{"type":"keypress","key":"Home"}]})json"));
    QSignalSpy changed(m_daemon.get(), &DaemonService::RulesChanged);
    const int announcementsBefore = changed.size();

    // A minute early: no firing.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 59));
    m_daemon->evaluateDueRules();
    QVERIFY(m_runner->requests.isEmpty());

    // At the wall-clock time the rule runs through the real pipeline.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 0));
    m_daemon->evaluateDueRules();
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));
    QCOMPARE(changed.size(), announcementsBefore + 1);
    const QJsonObject stored = rulesListing()
                                   .value(QStringLiteral("rules"))
                                   .toArray()
                                   .at(0)
                                   .toObject();
    QCOMPARE(stored.value(QStringLiteral("id")).toString(), ruleId);
    QCOMPARE(stored.value(QStringLiteral("lastRun"))
                 .toObject()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));

    // The same evaluation instant must not fire the occurrence twice.
    m_daemon->evaluateDueRules();
    QCOMPARE(m_runner->requests.size(), 1);
}

void DaemonDbusTest::disablingARuleUnschedulesItsNextFire() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.81")});
    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nightly home","trigger":{"type":"time","kind":"daily","time":"09:00"})json"
        R"json(,"actions":[{"type":"keypress","key":"Home"}]})json"));
    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));

    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 0));
    m_daemon->evaluateDueRules();
    QVERIFY(m_runner->requests.isEmpty());
}
```

- [ ] **Step 2: Verify RED**

Run: `cmake --build build/dev --target daemon-dbus-test 2>&1 | tail -5`
Expected: FAIL — `evaluateDueRules` and `DaemonService::WallClock` don't exist.

- [ ] **Step 3: Implement**

`daemon_service.h`:
- Add `#include "core/rule_scheduler.h"` and `#include <QDateTime>`.
- Add the public `WallClock` alias + `evaluateDueRules()` (public, after `pollingDiagnostic()`).
- Add private `rearmScheduleTimer()` / `executeRuleRun()` declarations (near `executeActions`).
- Add members: `QTimer m_scheduleTimer;` (next to `m_pollTimer`), `RuleScheduler m_ruleScheduler;` (next to `m_rules`), `WallClock m_wallClock;` (immediately after `QDBusServiceWatcher m_ownerWatcher;` so the init-list order stays declaration order), `bool m_dispatchActive = false;` (next to `m_shuttingDown`).
- Ctor: append `WallClock wallClock = {},` before `QObject* parent = nullptr`.

`daemon_service.cpp`:
- Init list: after `m_ownerWatcher(this)` add:
  ```cpp
  m_wallClock(wallClock ? std::move(wallClock)
                        : [] { return QDateTime::currentDateTime(); }),
  ```
- After `m_rules = m_configStore->loadRules();` add:
  ```cpp
  m_ruleScheduler.updateRules(m_rules, m_wallClock());
  m_scheduleTimer.setSingleShot(true);
  connect(&m_scheduleTimer, &QTimer::timeout, this, &DaemonService::evaluateDueRules);
  rearmScheduleTimer();
  ```
- `announceRules()` becomes:
  ```cpp
  void DaemonService::announceRules() {
      m_configStore->saveRules(m_rules);
      m_ruleScheduler.updateRules(m_rules, m_wallClock());
      rearmScheduleTimer();
      Q_EMIT RulesChanged(rulesJson());
  }
  ```
- New methods:
  ```cpp
  void DaemonService::rearmScheduleTimer() {
      const QDateTime deadline = m_ruleScheduler.nextDeadline();
      if (!deadline.isValid()) {
          m_scheduleTimer.stop();
          updateIdleTimer();
          return;
      }
      const qint64 until = std::max<qint64>(0, m_wallClock().msecsTo(deadline));
      // Cap the arm at 60s: a re-evaluation every minute re-anchors schedules
      // in local time and absorbs system sleep/wake without a second timer.
      m_scheduleTimer.start(static_cast<int>(std::min<qint64>(until, 60000)));
      updateIdleTimer();
  }

  void DaemonService::evaluateDueRules() {
      if (m_dispatchActive) {
          return;  // a run is mid-delay; the tail call re-evaluates when done
      }
      const QStringList due = m_ruleScheduler.dueRuleIds(m_wallClock());
      for (const QString& ruleId : due) {
          m_dispatchActive = true;
          static_cast<void>(executeRuleRun(ruleId));
          m_dispatchActive = false;
      }
      rearmScheduleTimer();
  }
  ```
- Refactor `RunRule`: its body after the `unknownRule` reply becomes `return executeRuleRun(ruleId);`; the extracted body is:
  ```cpp
  QString DaemonService::executeRuleRun(const QString& ruleId) {
      const auto existing = findRule(ruleId);
      if (existing == m_rules.end()) {
          return {};
      }
      RuleDocument rule = *existing;
      // [current RunRule body verbatim from target resolution through
      //  "announceRules();" — see daemon_service.cpp lines 1006-1042]
      evaluateDueRules();  // schedules that fell due during a delay step
      return outcome;
  }
  ```

- [ ] **Step 4: Build and run to green**

Run: `cmake --build build/dev && cd build/dev && ctest -R daemon-dbus --output-on-failure`
Expected: PASS, including both new cases. (The `m_dispatchActive` guard makes the `delay` action's nested `QEventLoop` re-entry safe: timer timeouts during a run are ignored and re-evaluated from the tail call.)

- [ ] **Step 5: Full suite + commit**

Run: `cd build/dev && ctest` — expect 16/16.
```bash
git add src/daemon/daemon_service.h src/daemon/daemon_service.cpp tests/integration/daemon_dbus_test.cpp
git commit -m "feat(daemon): fire scheduled rules through the execution pipeline"
```

---

### Task 4: Startup catch-up, idle protection, restart semantics

**Files:**
- Modify: `src/daemon/daemon_service.{h,cpp}`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Interfaces:**
- Consumes: Task 3's `evaluateDueRules`, `m_scheduleTimer`, `rearmScheduleTimer`; `RuleScheduler::updateRulesAtStartup` (Task 1); `idleTimerActive()`, `automationEnablementChanged()` (existing).
- Produces: nothing new — this task closes the M2 behavior set.

- [ ] **Step 1: Failing integration tests**

```cpp
  private Q_SLOTS:
    // ... existing ...
    void onceRuleMissedByLessThanADayFiresAtStartup();
    void intervalRuleReanchorsAcrossDaemonRestarts();
    void pendingScheduleKeepsTheDaemonAwake();

void DaemonDbusTest::onceRuleMissedByLessThanADayFiresAtStartup() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.82")});
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Once fresh","trigger":{"type":"time","kind":"once","time":"08:00")json"
        R"json(,"date":"2026-01-05"},"actions":[{"type":"keypress","key":"Home"}]})json"));
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Once stale","trigger":{"type":"time","kind":"once","time":"08:00")json"
        R"json(,"date":"2026-01-03"},"actions":[{"type":"keypress","key":"Back"}]})json"));

    // Boot 90 minutes after the fresh rule's fire time.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(9, 30));
    recreateDaemon(30s, {}, [this] { return m_now; });

    // Fresh: caught up exactly once at startup. Stale: skipped forever.
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));

    // Restart again: the persisted lastRun marks the rule consumed.
    recreateDaemon(30s, {}, [this] { return m_now; });
    QVERIFY(m_runner->requests.isEmpty());
}

void DaemonDbusTest::intervalRuleReanchorsAcrossDaemonRestarts() {
    m_daemon->upsertDevice({QStringLiteral("SERIAL-A"), QStringLiteral("Fixture Room"),
                            QStringLiteral("198.51.100.83")});
    addRuleReturningId(QStringLiteral(
        R"json({"name":"Nudge","trigger":{"type":"interval","minutes":5})json"
        R"json(,"actions":[{"type":"keypress","key":"Home"}]})json"));

    // Restart: the cadence starts over from boot — no burst.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 0));
    recreateDaemon(30s, {}, [this] { return m_now; });
    QVERIFY(m_runner->requests.isEmpty());

    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 5));
    m_daemon->evaluateDueRules();
    QCOMPARE(m_runner->requests.size(), 1);
    QCOMPARE(m_runner->lastRequest.arguments.last(2),
             QStringList({QStringLiteral("keypress"), QStringLiteral("Home")}));

    // Re-anchored to the fire instant: 10 minutes after boot is not due.
    m_now = QDateTime(QDate(2026, 1, 5), QTime(8, 10));
    m_daemon->evaluateDueRules();
    QCOMPARE(m_runner->requests.size(), 1);
}

void DaemonDbusTest::pendingScheduleKeepsTheDaemonAwake() {
    m_automationEnabled = false;
    m_daemon->automationEnablementChanged();
    QVERIFY(m_daemon->idleTimerActive());  // nothing to keep the daemon alive

    const QString ruleId = addRuleReturningId(QStringLiteral(
        R"json({"name":"Nightly off","trigger":{"type":"time","kind":"daily","time":"23:00"})json"
        R"json(,"actions":[{"type":"keypress","key":"Power"}]})json"));
    QVERIFY(!m_daemon->idleTimerActive());  // the pending schedule blocks idle exit

    const QDBusMessage disabled = waitForReply(m_proxy->SetRuleEnabled(ruleId, false));
    QVERIFY2(disabled.type() != QDBusMessage::ErrorMessage,
             qPrintable(disabled.errorMessage()));
    QVERIFY(m_daemon->idleTimerActive());   // released
}
```

- [ ] **Step 2: Verify RED**

Run: `cd build/dev && ctest -R daemon-dbus --output-on-failure`
Expected: FAIL — `onceRuleMissedByLessThanADayFiresAtStartup` (0 requests, expected 1: the ctor doesn't evaluate due rules yet) and `pendingScheduleKeepsTheDaemonAwake` (idle timer still running after adding the rule: `hasAcceptedWork` doesn't count the schedule timer). (`intervalRuleReanchorsAcrossDaemonRestarts` may already pass from Task 3's wiring; it is a guard against regressions.)

- [ ] **Step 3: Implement**

`daemon_service.cpp`:
- In the ctor, replace the `updateRules` call with the startup reseed and add the startup evaluation, i.e. the block added in Task 3 becomes:
  ```cpp
  m_ruleScheduler.updateRulesAtStartup(m_rules, m_wallClock());
  m_scheduleTimer.setSingleShot(true);
  connect(&m_scheduleTimer, &QTimer::timeout, this, &DaemonService::evaluateDueRules);
  rearmScheduleTimer();
  evaluateDueRules();  // spec §4.2: fresh missed `once` rules fire once at boot
  ```
- `hasAcceptedWork()` gains the schedule clause:
  ```cpp
  bool DaemonService::hasAcceptedWork() const {
      return m_discoveryActive || !m_deviceTestReplies.isEmpty() ||
             !m_activeCommandSerials.isEmpty() || !m_commandQueues.isEmpty() ||
             m_scheduleTimer.isActive();
  }
  ```
  (`handleIdleTimeout` already re-checks `hasAcceptedWork()`, so a schedule arming after the idle timer started cancels the exit; `rearmScheduleTimer` already calls `updateIdleTimer()` on both paths.)

- [ ] **Step 4: Run to green**

Run: `cd build/dev && ctest -R daemon-dbus --output-on-failure`
Expected: PASS (all 5 new M2 cases + existing cases).

- [ ] **Step 5: Full suite + commit**

Run: `cd build/dev && ctest` — expect 16/16.
```bash
git add src/daemon/daemon_service.cpp tests/integration/daemon_dbus_test.cpp
git commit -m "feat(daemon): startup catch-up and idle protection for scheduled rules"
```

---

### Task 5: CHANGELOG and release hygiene

- [ ] **Step 1:** Add under `## Unreleased` in `CHANGELOG.md`:
  - "The daemon now fires `time` (daily/weekdays/once) and `interval` automation triggers on its own — no widget or CLI session needs to be running, and pending schedules keep the daemon alive."
  - "`once` rules missed while the daemon was down fire once at startup if less than 24 hours late; older ones are skipped as stale."
- [ ] **Step 2:** Update the M2 ledger `.superpowers/sdd/2026-08-24-automations-m2/progress.md` (per-task rulings, final COMPLETE entry listing the five commits).
- [ ] **Step 3:** Full suite: `cd build/dev && ctest` — expect 16/16.
- [ ] **Step 4:** Deploy smoke: `bash scripts/install-dev.sh`, then add a rule via `busctl --user call` with a `daily` trigger one minute ahead; confirm the keypress fires on the TV without any client connected and `ListRules` shows `lastRun.outcome == "ok"`; then `DeleteRule`. If no physical Roku is reachable in this environment, record that the automated suite covers the pipeline and skip the device-visible step.
- [ ] **Step 5:** Commit `docs(changelog): scheduled automation firing`.

---

## Self-review notes

- Spec coverage — §4.2 timer wheel + missed-fire policy: T1 (daily/weekdays/once semantics, once <24h catch-up, stale skip), T2 (interval cadence), T4 (startup catch-up wiring, idle protection). §4 ordering ("in storage order within a tick"): `dueRuleIds` iterates `m_entries` in rule-list order. §3 wall-clock note: `QDateTime(QDate, QTime)` local construction throughout; DST shifts are by-design (documented in T1 notes). §8 fake clock + fake runner: unit suite uses injected `now`; integration uses `m_now` + `FakeProcessRunner`. §9 "scheduled power-off macros" user value: T3/T4 fire real keypresses end-to-end.
- Deviations from spec §4, surfaced honestly: one rearmed QTimer with a 60 s cap instead of "one QTimer per pending schedule" + "re-check on every snapshot tick" (ruling 8); mode gate and condition evaluation deferred per user ruling; `once` catch-up at startup only (mid-session overdue adds are skipped, ruling 6); `lastRun` stays real UTC while scheduling uses the injected clock (ruling 7).
- Type consistency: `RuleScheduler` signatures identical across T1 header, T3/T4 call sites (`updateRules`, `updateRulesAtStartup`, `dueRuleIds`, `nextDeadline()`); `executeRuleRun`/`evaluateDueRules`/`rearmScheduleTimer` names match everywhere; `WallClock` param is last so all existing `DaemonService` construction sites stay valid; member declaration order matches the init-list order (`m_wallClock` after `m_ownerWatcher`).
- Existing tests unaffected: non-schedule tests use the default real clock; the fake `m_now` only affects scheduler decisions, never `lastRun` timestamps or D-Bus behavior.

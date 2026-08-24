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
    void intervalRuleFiresOnCadenceAndReanchorsToTheFireInstant();
    void intervalRuleNeverBurstsAfterLongGaps();
    void intervalRuleReanchorsAcrossRestart();
    void unchangedTriggersKeepAnchorsAcrossUpdates();
};

void RuleSchedulerTest::dailyRuleFiresAtItsWallClockTimeAndRepeats() {
    RuleScheduler scheduler;
    const RuleDocument daily = rule(QStringLiteral("daily"),
                                    QStringLiteral("Nightly"), dailyTrigger());
    QDateTime now = at(QDate(2026, 1, 5), QStringLiteral("22:59"));
    scheduler.updateRules({daily}, now);
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());  // one minute early
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 5), QStringLiteral("23:00")));

    now = at(QDate(2026, 1, 5), QStringLiteral("23:00"));
    QCOMPARE(scheduler.dueRuleIds(now), QStringList{"daily"_L1});
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());  // idempotent per occurrence
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 6), QStringLiteral("23:00")));

    // A long gap (daemon asleep for days) never backfills: one firing, then
    // the next occurrence strictly after the evaluation instant.
    now = at(QDate(2026, 1, 9), QStringLiteral("23:00"));
    QCOMPARE(scheduler.dueRuleIds(now), QStringList{"daily"_L1});
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());
}

void RuleSchedulerTest::weekdayRuleSkipsWeekendDays() {
    RuleScheduler scheduler;
    const RuleDocument weekdays = rule(QStringLiteral("week"),
        QStringLiteral("Weeknight"),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "weekdays"_L1},
                    {"time"_L1, "23:00"_L1}});
    // Friday 2026-01-09: fires that evening, next deadline Monday.
    scheduler.updateRules({weekdays}, at(QDate(2026, 1, 9), QStringLiteral("22:00")));
    QCOMPARE(scheduler.dueRuleIds(at(QDate(2026, 1, 9), QStringLiteral("23:00"))),
             QStringList{"week"_L1});
    QCOMPARE(scheduler.nextDeadline(), at(QDate(2026, 1, 12), QStringLiteral("23:00")));

    // Seeded on a Saturday: the first occurrence lands Monday, not the weekend.
    RuleScheduler weekendSeed;
    weekendSeed.updateRules({weekdays}, at(QDate(2026, 1, 10), QStringLiteral("08:00")));
    QCOMPARE(weekendSeed.nextDeadline(), at(QDate(2026, 1, 12), QStringLiteral("23:00")));
}

void RuleSchedulerTest::missedOnceRuleFiresAtStartupWhileFresh() {
    RuleScheduler scheduler;
    const RuleDocument once = rule(QStringLiteral("once"), QStringLiteral("Morning"),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-05"_L1}});
    // Daemon boots 90 minutes late: fires once at startup, then is consumed.
    const QDateTime startup = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRulesAtStartup({once}, startup);
    QCOMPARE(scheduler.dueRuleIds(startup), QStringList{"once"_L1});
    QVERIFY(scheduler.dueRuleIds(startup).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::staleOnceRuleIsSkipped() {
    RuleScheduler scheduler;
    const RuleDocument stale = rule(QStringLiteral("stale"), QStringLiteral("Old"),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-03"_L1}});
    const QDateTime startup = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRulesAtStartup({stale}, startup);
    QVERIFY(scheduler.dueRuleIds(startup).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::consumedOnceRuleNeverRefires() {
    RuleScheduler scheduler;
    RuleDocument consumed = rule(QStringLiteral("done"), QStringLiteral("Done"),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-05"_L1}});
    // Production stores lastFiredAt as UTC; derive this fixture's timestamp
    // from the scheduled wall-clock instant so the test is timezone-faithful.
    consumed.lastRun.firedAtIso =
        at(QDate(2026, 1, 5), QStringLiteral("08:00")).toUTC().toString(Qt::ISODateWithMs);
    consumed.lastRun.outcome = QStringLiteral("ok");
    const QDateTime startup = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRulesAtStartup({consumed}, startup);
    QVERIFY(scheduler.dueRuleIds(startup).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::overdueOnceRuleAddedMidSessionNeverFires() {
    RuleScheduler scheduler;
    const RuleDocument once = rule(QStringLiteral("late"), QStringLiteral("Late"),
        QJsonObject{{"type"_L1, "time"_L1}, {"kind"_L1, "once"_L1},
                    {"time"_L1, "08:00"_L1}, {"date"_L1, "2026-01-05"_L1}});
    const QDateTime now = at(QDate(2026, 1, 5), QStringLiteral("09:30"));
    scheduler.updateRules({once}, now);  // announce-time reseed: no catch-up
    QVERIFY(scheduler.dueRuleIds(now).isEmpty());
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::disabledAndUnscheduledRulesAreIgnored() {
    RuleScheduler scheduler;
    const RuleDocument disabled = rule(QStringLiteral("off"),
        QStringLiteral("Disabled"), dailyTrigger(), false);
    const RuleDocument manual = rule(QStringLiteral("man"),
        QStringLiteral("Manual"), QJsonObject{{"type"_L1, "manual"_L1}});
    scheduler.updateRules({disabled, manual}, at(QDate(2026, 1, 5), QStringLiteral("22:00")));
    QVERIFY(!scheduler.nextDeadline().isValid());
}

void RuleSchedulerTest::intervalRuleFiresOnCadenceAndReanchorsToTheFireInstant() {
    RuleScheduler scheduler;
    const RuleDocument cadence = rule(QStringLiteral("cadence"),
        QStringLiteral("Nudge"), QJsonObject{{"type"_L1, "interval"_L1},
                                             {"minutes"_L1, 30}});
    const QDateTime boot = at(QDate(2026, 1, 5), QStringLiteral("08:00"));
    scheduler.updateRules({cadence}, boot);
    QVERIFY(scheduler.dueRuleIds(boot).isEmpty());   // not due at boot
    QCOMPARE(scheduler.nextDeadline(), boot.addSecs(30 * 60));

    const QDateTime firstFire = boot.addSecs(30 * 60);
    QCOMPARE(scheduler.dueRuleIds(firstFire), QStringList{"cadence"_L1});
    QVERIFY(scheduler.dueRuleIds(firstFire).isEmpty());  // idempotent
    QCOMPARE(scheduler.nextDeadline(), firstFire.addSecs(30 * 60));
}

void RuleSchedulerTest::intervalRuleNeverBurstsAfterLongGaps() {
    RuleScheduler scheduler;
    const RuleDocument cadence = rule(QStringLiteral("cadence"),
        QStringLiteral("Nudge"), QJsonObject{{"type"_L1, "interval"_L1},
                                             {"minutes"_L1, 30}});
    const QDateTime boot = at(QDate(2026, 1, 5), QStringLiteral("08:00"));
    scheduler.updateRules({cadence}, boot);

    // The daemon sleeps 3 intervals: exactly one firing, then the cadence
    // restarts from the evaluation instant (anti-burst).
    const QDateTime late = boot.addSecs(3 * 30 * 60);
    QCOMPARE(scheduler.dueRuleIds(late), QStringList{"cadence"_L1});
    QVERIFY(scheduler.dueRuleIds(late).isEmpty());
    QCOMPARE(scheduler.nextDeadline(), late.addSecs(30 * 60));
}

void RuleSchedulerTest::intervalRuleReanchorsAcrossRestart() {
    const RuleDocument cadence = rule(QStringLiteral("cadence"),
        QStringLiteral("Nudge"), QJsonObject{{"type"_L1, "interval"_L1},
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
    const RuleDocument cadence = rule(QStringLiteral("cadence"),
        QStringLiteral("Nudge"), QJsonObject{{"type"_L1, "interval"_L1},
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

QTEST_MAIN(RuleSchedulerTest)
#include "rule_scheduler_test.moc"

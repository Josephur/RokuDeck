// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/rule_summary.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace RokuDeck;
using namespace Qt::Literals::StringLiterals;

namespace {

QJsonObject trigger(const QString& type) {
    return QJsonObject{{"type"_L1, type}};
}

// A complete, parseable rule document carrying only the pieces the summary
// renderer looks at; optional blocks ride in via `extra`.
QJsonObject fixtureRule(const QJsonObject& ruleTrigger, const QJsonArray& actions,
                        const QJsonObject& extra = {}) {
    QJsonObject object{{"schemaVersion"_L1, 1},
                       {"name"_L1, "Fixture"_L1},
                       {"enabled"_L1, true},
                       {"mode"_L1, "queued"_L1},
                       {"devices"_L1, QJsonArray{}},
                       {"trigger"_L1, ruleTrigger},
                       {"actions"_L1, actions}};
    for (const QString& key : {QStringLiteral("id"), QStringLiteral("conditions"),
                               QStringLiteral("lastRun")}) {
        if (extra.contains(key)) {
            object.insert(key, extra.value(key));
        }
    }
    return object;
}

} // namespace

class RuleSummaryTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void rendersKnownTriggerAndActionShapes_data();
    void rendersKnownTriggerAndActionShapes();
    void appendsExtraConditionsNotice();
    void degradesUnknownTypesToCustomRule();
    void toleratesMalformedDocuments();
};

void RuleSummaryTest::rendersKnownTriggerAndActionShapes_data() {
    QTest::addColumn<QJsonObject>("rule");
    QTest::addColumn<QString>("summary");

    QTest::newRow("daily-keypress")
        << fixtureRule(QJsonObject{{"type"_L1, "time"_L1},
                                   {"kind"_L1, "daily"_L1},
                                   {"time"_L1, "23:00"_L1}},
                       QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "Home"_L1}}})
        << QStringLiteral("Daily at 23:00 \u00b7 Press Home");
    QTest::newRow("weekdays-volume-delay")
        << fixtureRule(QJsonObject{{"type"_L1, "time"_L1},
                                   {"kind"_L1, "weekdays"_L1},
                                   {"time"_L1, "08:30"_L1}},
                       QJsonArray{QJsonObject{{"type"_L1, "setVolume"_L1}, {"volume"_L1, 15}},
                                  QJsonObject{{"type"_L1, "delay"_L1}, {"seconds"_L1, 30}}})
        << QStringLiteral("Weekdays at 08:30 \u00b7 Set volume to 15 \u00b7 Wait 30 s");
    QTest::newRow("once-launch")
        << fixtureRule(QJsonObject{{"type"_L1, "time"_L1},
                                   {"kind"_L1, "once"_L1},
                                   {"time"_L1, "07:00"_L1},
                                   {"date"_L1, "2026-09-01"_L1}},
                       QJsonArray{QJsonObject{{"type"_L1, "launchApp"_L1},
                                              {"appId"_L1, "tv.netflix"_L1}}})
        << QStringLiteral("Once on 2026-09-01 at 07:00 \u00b7 Launch tv.netflix");
    QTest::newRow("interval-mute")
        << fixtureRule(QJsonObject{{"type"_L1, "interval"_L1}, {"minutes"_L1, 30}},
                       QJsonArray{QJsonObject{{"type"_L1, "mute"_L1}, {"muted"_L1, true}}})
        << QStringLiteral("Every 30 minutes \u00b7 Mute");
    QTest::newRow("interval-unmute")
        << fixtureRule(QJsonObject{{"type"_L1, "interval"_L1}, {"minutes"_L1, 5}},
                       QJsonArray{QJsonObject{{"type"_L1, "mute"_L1}, {"muted"_L1, false}}})
        << QStringLiteral("Every 5 minutes \u00b7 Unmute");
    QTest::newRow("manual-multi")
        << fixtureRule(trigger("manual"_L1),
                       QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "Select"_L1}},
                                  QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "Back"_L1}}})
        << QStringLiteral("Manually run \u00b7 Press Select \u00b7 Press Back");
    QTest::newRow("statechange-gt")
        << fixtureRule(QJsonObject{{"type"_L1, "stateChange"_L1},
                                   {"field"_L1, "volume"_L1},
                                   {"op"_L1, "gt"_L1},
                                   {"value"_L1, 20}},
                       QJsonArray{QJsonObject{{"type"_L1, "setVolume"_L1}, {"volume"_L1, 10}}})
        << QStringLiteral("When volume above 20 \u00b7 Set volume to 10");
    QTest::newRow("statechange-between")
        << fixtureRule(QJsonObject{{"type"_L1, "stateChange"_L1},
                                   {"field"_L1, "volume"_L1},
                                   {"op"_L1, "between"_L1},
                                   {"value"_L1, QJsonArray{10, 20}}},
                       QJsonArray{QJsonObject{{"type"_L1, "mute"_L1}, {"muted"_L1, false}}})
        << QStringLiteral("When volume between 10 and 20 \u00b7 Unmute");
    QTest::newRow("statechange-is-string")
        << fixtureRule(QJsonObject{{"type"_L1, "stateChange"_L1},
                                   {"field"_L1, "power"_L1},
                                   {"op"_L1, "is"_L1},
                                   {"value"_L1, "standby"_L1}},
                       QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "PowerOn"_L1}}})
        << QStringLiteral("When power is standby \u00b7 Press PowerOn");
    QTest::newRow("statechange-playing")
        << fixtureRule(QJsonObject{{"type"_L1, "stateChange"_L1},
                                   {"field"_L1, "playing"_L1},
                                   {"op"_L1, "is"_L1},
                                   {"value"_L1, true}},
                       QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "Home"_L1}}})
        << QStringLiteral("When playing is true \u00b7 Press Home");
    QTest::newRow("runrule-macro")
        << fixtureRule(trigger("manual"_L1), QJsonArray{QJsonObject{{"type"_L1, "runRule"_L1},
                                                                   {"id"_L1, "rABC123"_L1}}})
        << QStringLiteral("Manually run \u00b7 Run rule rABC123");
    QTest::newRow("daily-window")
        << fixtureRule(QJsonObject{{"type"_L1, "time"_L1},
                                   {"kind"_L1, "daily"_L1},
                                   {"time"_L1, "22:00"_L1},
                                   {"activeWindow"_L1,
                                    QJsonObject{{"start"_L1, "22:00"_L1},
                                                {"end"_L1, "08:00"_L1}}}},
                       QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "Home"_L1}}})
        << QStringLiteral(
            "Daily at 22:00 \u00b7 active 22:00\u201308:00 \u00b7 Press Home");

    const QJsonObject conditionalAction{
        {"type"_L1, "ifThenElse"_L1},
        {"cond"_L1,
         QJsonObject{{"leaf"_L1, QJsonObject{{"field"_L1, "power"_L1},
                                             {"op"_L1, "is"_L1},
                                             {"value"_L1, "on"_L1}}}}},
        {"then"_L1, QJsonArray{}}};
    QTest::newRow("ifthenelse-step")
        << fixtureRule(trigger("manual"_L1), QJsonArray{conditionalAction})
        << QStringLiteral("Manually run \u00b7 Conditional step");
}

void RuleSummaryTest::rendersKnownTriggerAndActionShapes() {
    QFETCH(QJsonObject, rule);
    QFETCH(QString, summary);
    QCOMPARE(ruleSummary(rule), summary);
}

void RuleSummaryTest::appendsExtraConditionsNotice() {
    const QJsonObject rule =
        fixtureRule(QJsonObject{{"type"_L1, "time"_L1},
                                {"kind"_L1, "daily"_L1},
                                {"time"_L1, "22:00"_L1}},
                    QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1}, {"key"_L1, "Home"_L1}}},
                    QJsonObject{{"conditions"_L1,
                                 QJsonObject{{"leaf"_L1, QJsonObject{{"field"_L1, "power"_L1},
                                                                     {"op"_L1, "is"_L1},
                                                                     {"value"_L1, "on"_L1}}}}}});
    QCOMPARE(ruleSummary(rule),
             QStringLiteral("Daily at 22:00 \u00b7 Press Home \u00b7 extra conditions"));
}

void RuleSummaryTest::degradesUnknownTypesToCustomRule() {
    const QJsonObject unknownTrigger = fixtureRule(
        QJsonObject{{"type"_L1, "telepathy"_L1}},
        QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1}, {"key"_L1, "Home"_L1}}});
    QCOMPARE(ruleSummary(unknownTrigger), QStringLiteral("Custom rule"));

    const QJsonObject unknownAction =
        fixtureRule(trigger("manual"_L1), QJsonArray{QJsonObject{{"type"_L1, "teleport"_L1}}});
    QCOMPARE(ruleSummary(unknownAction), QStringLiteral("Custom rule"));

    // A window block the renderer cannot read degrades the whole rule.
    const QJsonObject unreadableWindow = fixtureRule(
        QJsonObject{{"type"_L1, "manual"_L1},
                    {"activeWindow"_L1, QJsonObject{{"start"_L1, ""_L1}}}},
        QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1}, {"key"_L1, "Home"_L1}}});
    QCOMPARE(ruleSummary(unreadableWindow), QStringLiteral("Custom rule"));
}

void RuleSummaryTest::toleratesMalformedDocuments() {
    QCOMPARE(ruleSummary(QJsonObject{}), QStringLiteral("Custom rule"));
    QCOMPARE(ruleSummary(QJsonObject{{"schemaVersion"_L1, 1}}), QStringLiteral("Custom rule"));
}

QTEST_MAIN(RuleSummaryTest)
#include "rule_summary_test.moc"

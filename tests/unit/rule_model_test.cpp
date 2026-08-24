// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/rule_model.h"
#include "core/parse_result.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <functional>
#include <QJsonObject>
#include <QtTest>

using namespace RokuDeck;
using namespace Qt::Literals::StringLiterals;

class RuleModelTest final : public QObject {
    Q_OBJECT

private:
    [[nodiscard]] static QJsonObject leaf(const QString& field, const QString& op,
        const QJsonValue& value, const QString& device = QStringLiteral("*"))
    {
        return QJsonObject{{QStringLiteral("device"), device},
            {QStringLiteral("field"), field},
            {QStringLiteral("op"), op},
            {QStringLiteral("value"), value}};
    }

    [[nodiscard]] static QJsonObject keypressAction(const QString& key)
    {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("keypress")},
            {QStringLiteral("key"), key}};
    }

    [[nodiscard]] static QJsonObject completeRuleJson()
    {
        const QJsonObject mutedLeaf =
            leaf(QStringLiteral("muted"), QStringLiteral("is"), true);
        const QJsonObject notMuted{{QStringLiteral("not"),
            QJsonObject{{QStringLiteral("leaf"), mutedLeaf}}}};
        const QJsonObject powerLeaf =
            leaf(QStringLiteral("power"), QStringLiteral("is"), QStringLiteral("on"));
        const QJsonObject powerLeafWrapped{{QStringLiteral("leaf"),
            leaf(QStringLiteral("power"), QStringLiteral("is"),
                QStringLiteral("on"))}};
        const QJsonArray allChildren{powerLeafWrapped, notMuted};
        const QJsonObject conditions{{QStringLiteral("all"), allChildren}};

        const QJsonObject appLeaf = leaf(QStringLiteral("app"),
            QStringLiteral("isNot"), QStringLiteral("tv.netflix"));
        const QJsonObject appLeafWrapped{{QStringLiteral("leaf"), appLeaf}};
        const QJsonArray anyChildren{appLeafWrapped};
        const QJsonObject cond{{QStringLiteral("any"), anyChildren}};
        const QJsonArray thenActions{keypressAction(QStringLiteral("Home"))};
        const QJsonArray elseActions{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("runRule")},
            {QStringLiteral("id"), QStringLiteral("rXYZ")}}};
        const QJsonObject ifThenElse{
            {QStringLiteral("type"), QStringLiteral("ifThenElse")},
            {QStringLiteral("cond"), cond},
            {QStringLiteral("then"), thenActions},
            {QStringLiteral("else"), elseActions}};

        const QJsonArray actions{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("setVolume")},
                {QStringLiteral("volume"), 15}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("delay")},
                {QStringLiteral("seconds"), 30}},
            ifThenElse};

        const QJsonObject trigger{
            {QStringLiteral("type"), QStringLiteral("time")},
            {QStringLiteral("kind"), QStringLiteral("weekdays")},
            {QStringLiteral("time"), QStringLiteral("23:00")}};

        return QJsonObject{{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("id"), QStringLiteral("rABC")},
            {QStringLiteral("name"), QStringLiteral("Night volume")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("mode"), QStringLiteral("queued")},
            {QStringLiteral("devices"), QJsonArray{QStringLiteral("X001")}},
            {QStringLiteral("trigger"), trigger},
            {QStringLiteral("conditions"), conditions},
            {QStringLiteral("actions"), actions}};
    }

private Q_SLOTS:
    void parsesAndSerializesACompleteRule();
    void parsesPlayingFieldsAndRejectsNonBooleanValues();
    void rejectsMalformedRules_data();
    void rejectsMalformedRules();
    void rejectsOverlyDeepConditions();
};

void RuleModelTest::parsesAndSerializesACompleteRule()
{
    const QJsonObject source = completeRuleJson();
    const auto parsed = parseRule(source);
    if (!parsed.has_value()) {
        QFAIL(qPrintable(parsed.error().message));
    }
    QCOMPARE(parsed.value().name, "Night volume"_L1);
    QCOMPARE(parsed.value().mode, "queued"_L1);
    QCOMPARE(parsed.value().enabled, true);
    QCOMPARE(parsed.value().devices, QStringList{"X001"_L1});
    QCOMPARE(parsed.value().trigger.type, "time"_L1);
    QCOMPARE(parsed.value().trigger.timeKind, "weekdays"_L1);
    QCOMPARE(parsed.value().trigger.time, "23:00"_L1);
    QVERIFY(parsed.value().conditions.has_value());
    QCOMPARE(parsed.value().conditions->kind, ConditionNode::Kind::All);
    QCOMPARE(parsed.value().conditions->children.size(), 2);
    QCOMPARE(parsed.value().actions.size(), 3);
    QCOMPARE(parsed.value().actions.at(0).volume, 15);
    QCOMPARE(parsed.value().actions.at(1).delaySeconds, 30);
    QCOMPARE(parsed.value().actions.at(2).thenActions.size(), 1);
    QCOMPARE(parsed.value().actions.at(2).elseActions.size(), 1);

    // Round trip: serializing the parsed document must reproduce the input.
    const QJsonObject serialized = ruleJson(parsed.value());
    const auto reparsed = parseRule(serialized);
    if (!reparsed.has_value()) {
        QFAIL(qPrintable(reparsed.error().message));
    }
    QCOMPARE(QJsonDocument(ruleJson(reparsed.value())),
        QJsonDocument(serialized));
}

void RuleModelTest::parsesPlayingFieldsAndRejectsNonBooleanValues()
{
    const QJsonObject playingTrigger{
        {QStringLiteral("type"), QStringLiteral("stateChange")},
        {QStringLiteral("field"), QStringLiteral("playing")},
        {QStringLiteral("op"), QStringLiteral("is")},
        {QStringLiteral("value"), true}};
    const QJsonObject playingLeaf = leaf(QStringLiteral("playing"),
        QStringLiteral("isNot"), false, QStringLiteral("LOCAL-PC"));
    const QJsonObject document{
        {"schemaVersion"_L1, 1},
        {"name"_L1, "media"_L1},
        {"devices"_L1, QJsonArray{QStringLiteral("LOCAL-PC")}},
        {"trigger"_L1, playingTrigger},
        {"conditions"_L1,
            QJsonObject{{QStringLiteral("leaf"), playingLeaf}}},
        {"actions"_L1, QJsonArray{keypressAction(QStringLiteral("Home"))}}};

    const auto parsed = parseRule(document);
    if (!parsed.has_value()) {
        QFAIL(qPrintable(parsed.error().message));
    }
    QCOMPARE(parsed.value().trigger.stateChange.field, "playing"_L1);
    QVERIFY(parsed.value().trigger.stateChange.value.isBool());
    QCOMPARE(parsed.value().trigger.stateChange.value.toBool(), true);
    QVERIFY(parsed.value().conditions.has_value());
    QCOMPARE(parsed.value().conditions->leaf.device, "LOCAL-PC"_L1);
    QCOMPARE(parsed.value().conditions->leaf.value.toBool(), false);

    // Round trip: the boolean value survives serialization untouched.
    const auto reparsed = parseRule(ruleJson(parsed.value()));
    if (!reparsed.has_value()) {
        QFAIL(qPrintable(reparsed.error().message));
    }
    QCOMPARE(reparsed.value().trigger.stateChange.value.toBool(), true);

    // Strings and numbers never satisfy the boolean field.
    QJsonObject stringified = document;
    QJsonObject trigger = playingTrigger;
    trigger.insert(QStringLiteral("value"), QStringLiteral("true"));
    stringified.insert(QStringLiteral("trigger"), trigger);
    const auto stringParsed = parseRule(stringified);
    QVERIFY(!stringParsed.has_value());
    QVERIFY2(stringParsed.error().message.contains(QStringLiteral("playing")),
             qPrintable(stringParsed.error().message));

    QJsonObject numeric = document;
    trigger.insert(QStringLiteral("value"), 1);
    numeric.insert(QStringLiteral("trigger"), trigger);
    const auto numericParsed = parseRule(numeric);
    QVERIFY(!numericParsed.has_value());
    QVERIFY2(numericParsed.error().message.contains(QStringLiteral("playing")),
             qPrintable(numericParsed.error().message));
}

void RuleModelTest::rejectsMalformedRules_data()
{
    QTest::addColumn<QJsonObject>("document");
    QTest::addColumn<QString>("expectSubstring");

    auto base = [] {
        return QJsonObject{{"schemaVersion"_L1, 1},
            {"name"_L1, "x"_L1},
            {"trigger"_L1, QJsonObject{{"type"_L1, "manual"_L1}}},
            {"actions"_L1, QJsonArray{keypressAction(QStringLiteral("Home"))}}};
    };
    auto mutated = [&](std::function<void(QJsonObject&)> mutate) {
        QJsonObject object = base();
        mutate(object);
        return object;
    };

    QTest::newRow("empty-name")
        << mutated([](QJsonObject& o) { o.insert("name"_L1, ""_L1); })
        << "rule name";
    QTest::newRow("unknown-trigger")
        << mutated([](QJsonObject& o) {
               o.insert("trigger"_L1,
                   QJsonObject{{"type"_L1, "telepathy"_L1}});
           })
        << "unknown trigger type";
    QTest::newRow("bad-time-format")
        << mutated([](QJsonObject& o) {
               o.insert("trigger"_L1,
                   QJsonObject{{"type"_L1, "time"_L1},
                       {"kind"_L1, "daily"_L1},
                       {"time"_L1, "25:99"_L1}});
           })
        << "HH:MM";
    QTest::newRow("interval-too-small")
        << mutated([](QJsonObject& o) {
               o.insert("trigger"_L1,
                   QJsonObject{{"type"_L1, "interval"_L1}, {"minutes"_L1, 1}});
           })
        << "5..1440";
    QTest::newRow("unknown-field")
        << mutated([](QJsonObject& o) {
               o.insert("trigger"_L1,
                   QJsonObject{{"type"_L1, "stateChange"_L1},
                       {"field"_L1, "smell"_L1},
                       {"op"_L1, "is"_L1},
                       {"value"_L1, "on"_L1}});
           })
        << "unknown field";
    QTest::newRow("between-not-pair")
        << mutated([](QJsonObject& o) {
               o.insert("trigger"_L1,
                   QJsonObject{{"type"_L1, "stateChange"_L1},
                       {"field"_L1, "volume"_L1},
                       {"op"_L1, "between"_L1},
                       {"value"_L1, QJsonArray{20}}});
           })
        << "[low,high]";
    QTest::newRow("unknown-action")
        << mutated([](QJsonObject& o) {
               o.insert("actions"_L1,
                   QJsonArray{QJsonObject{{"type"_L1, "teleport"_L1}}});
           })
        << "unknown action type";
    QTest::newRow("volume-out-of-range")
        << mutated([](QJsonObject& o) {
               o.insert("actions"_L1,
                   QJsonArray{QJsonObject{{"type"_L1, "setVolume"_L1},
                       {"volume"_L1, 140}}});
           })
        << "0..100";
    QTest::newRow("delay-out-of-range")
        << mutated([](QJsonObject& o) {
               o.insert("actions"_L1,
                   QJsonArray{QJsonObject{{"type"_L1, "delay"_L1},
                       {"seconds"_L1, 601}}});
           })
        << "1..600";
    QTest::newRow("extra-keys")
        << mutated([](QJsonObject& o) { o.insert("wat"_L1, 1); })
        << "keys mismatch";
    QTest::newRow("no-actions")
        << mutated([](QJsonObject& o) { o.insert("actions"_L1, QJsonArray{}); })
        << "1..32 actions";
    QTest::newRow("bad-mode")
        << mutated([](QJsonObject& o) { o.insert("mode"_L1, "chaos"_L1); })
        << "unknown mode";
}

void RuleModelTest::rejectsMalformedRules()
{
    QFETCH(QJsonObject, document);
    QFETCH(QString, expectSubstring);
    const auto parsed = parseRule(document);
    QVERIFY(!parsed.has_value());
    QVERIFY2(parsed.error().message.contains(expectSubstring),
             qPrintable(parsed.error().message));
}

void RuleModelTest::rejectsOverlyDeepConditions()
{
    // 17 nested not-wrappers around a valid leaf must trip the depth cap.
    QJsonObject node{{QStringLiteral("leaf"),
        leaf(QStringLiteral("power"), QStringLiteral("is"),
            QStringLiteral("on"))}};
    for (int depth = 0; depth < 17; ++depth) {
        node = QJsonObject{{QStringLiteral("not"), node}};
    }
    const QJsonObject document{
        {"name"_L1, "deep"_L1},
        {"trigger"_L1, QJsonObject{{"type"_L1, "manual"_L1}}},
        {"conditions"_L1, node},
        {"actions"_L1, QJsonArray{keypressAction(QStringLiteral("Home"))}}};
    const auto parsed = parseRule(document);
    QVERIFY(!parsed.has_value());
    QVERIFY(parsed.error().message.contains("exceeds 16"_L1));
}

QTEST_MAIN(RuleModelTest)
#include "rule_model_test.moc"

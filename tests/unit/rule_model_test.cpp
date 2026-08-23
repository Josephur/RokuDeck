// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/rule_model.h"
#include "core/parse_result.h"

#include <QJsonArray>
#include <QJsonDocument>
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

QTEST_MAIN(RuleModelTest)
#include "rule_model_test.moc"

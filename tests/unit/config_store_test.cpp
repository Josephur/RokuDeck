// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_store.h"
#include "core/rule_model.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace RokuDeck;
using namespace Qt::Literals::StringLiterals;

namespace {

[[nodiscard]] QJsonObject manualRuleJson(const QString& name)
{
    return QJsonObject{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("name"), name},
        {QStringLiteral("trigger"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("manual")}}},
        {QStringLiteral("actions"),
            QJsonArray{QJsonObject{{QStringLiteral("type"),
                QStringLiteral("keypress")},
                {QStringLiteral("key"), QStringLiteral("Back")}}}}};
}

} // namespace

class ConfigStoreTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void rulesRoundTripPreservesOrderAndFlags();
    void corruptRuleEntriesAreSkipped();
};

void ConfigStoreTest::rulesRoundTripPreservesOrderAndFlags()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    ConfigStore store(config);

    const QString firstJson = QString::fromUtf8(
        QJsonDocument(manualRuleJson(QStringLiteral("Second saved"))).toJson());
    const QString secondJson = QString::fromUtf8(
        QJsonDocument(manualRuleJson(QStringLiteral("First saved"))).toJson());

    RuleDocument first;
    QVERIFY(parseRule(QJsonDocument::fromJson(firstJson.toUtf8()).object())
                .has_value());
    first = parseRule(
        QJsonDocument::fromJson(firstJson.toUtf8()).object()).value();
    first.id = QStringLiteral("rB");
    first.enabled = true;

    RuleDocument second;
    second = parseRule(
        QJsonDocument::fromJson(secondJson.toUtf8()).object()).value();
    second.id = QStringLiteral("rA");
    second.enabled = false;

    store.saveRules({first, second});

    ConfigStore reload(config);
    const QList<RuleDocument> loaded = reload.loadRules();
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).id, QStringLiteral("rB"));
    QCOMPARE(loaded.at(0).name, QStringLiteral("Second saved"));
    QCOMPARE(loaded.at(0).enabled, true);
    QCOMPARE(loaded.at(1).id, QStringLiteral("rA"));
    QCOMPARE(loaded.at(1).enabled, false);
    QCOMPARE(loaded.at(1).actions.size(), 1);
    QCOMPARE(loaded.at(1).actions.first().key, QStringLiteral("Back"));
}

void ConfigStoreTest::corruptRuleEntriesAreSkipped()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    {
        KConfigGroup rulesGroup(config, QStringLiteral("Rules"));
        KConfigGroup good(&rulesGroup, QStringLiteral("0"));
        good.writeEntry("Name", QStringLiteral("Good"));
        good.writeEntry("Enabled", true);
        good.writeEntry("Definition",
            QJsonDocument(manualRuleJson(QStringLiteral("Good"))).toJson());
        KConfigGroup bad(&rulesGroup, QStringLiteral("1"));
        bad.writeEntry("Name", QStringLiteral("Broken"));
        bad.writeEntry("Enabled", true);
        bad.writeEntry("Definition", QStringLiteral("{not json"));
        config->sync();
    }

    ConfigStore store(config);
    const QList<RuleDocument> loaded = store.loadRules();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().name, QStringLiteral("Good"));
}

QTEST_MAIN(ConfigStoreTest)
#include "config_store_test.moc"

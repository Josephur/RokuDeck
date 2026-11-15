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
    void corruptRuleSurvivesALaterSave();
    void devicesRoundTripPreservesVolumeCeiling();
    void absentVolumeCeilingDefaultsToUnrestricted();
    void peerListenerSettingsRoundTripAndDefaults();
    void pairedControllersRoundTripAndSkipNamelessEntries();
    void peerTargetsRoundTripAndSkipIncompleteEntries();
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

void ConfigStoreTest::corruptRuleSurvivesALaterSave()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    const QString corruptDefinition = QStringLiteral("{not json");
    {
        KConfigGroup rulesGroup(config, QStringLiteral("Rules"));
        KConfigGroup good(&rulesGroup, QStringLiteral("0"));
        good.writeEntry("Name", QStringLiteral("Good"));
        good.writeEntry("Enabled", true);
        good.writeEntry("Definition",
            QJsonDocument(manualRuleJson(QStringLiteral("Good"))).toJson());
        KConfigGroup bad(&rulesGroup, QStringLiteral("1"));
        bad.writeEntry("Name", QStringLiteral("Broken"));
        bad.writeEntry("Enabled", false);
        bad.writeEntry("Definition", corruptDefinition);
        bad.writeEntry("SortIndex", 1);
        config->sync();
    }

    ConfigStore store(config);
    QList<RuleDocument> loaded = store.loadRules();
    QCOMPARE(loaded.size(), 1);
    // Any later edit rewrites [Rules]; the unparsable sibling must survive it
    // instead of being silently deleted from disk.
    loaded.first().enabled = false;
    store.saveRules(loaded);

    QCOMPARE(store.loadRules().size(), 1);
    const KConfigGroup rulesGroup(config, QStringLiteral("Rules"));
    bool preserved = false;
    for (const QString& groupName : rulesGroup.groupList()) {
        const KConfigGroup group(&rulesGroup, groupName);
        if (group.readEntry("Definition", QString()) == corruptDefinition) {
            preserved = true;
            QCOMPARE(group.readEntry("Name", QString()), QStringLiteral("Broken"));
            QCOMPARE(group.readEntry("Enabled", true), false);
        }
    }
    QVERIFY2(preserved, "an unparsable rule must survive a later save verbatim");
}

void ConfigStoreTest::devicesRoundTripPreservesVolumeCeiling()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    ConfigStore store(config);

    DeviceRecord device;
    device.serial = QStringLiteral("SERIAL-A");
    device.displayName = QStringLiteral("Living Room");
    device.address = QStringLiteral("198.51.100.10");
    device.volumeCeiling = 42;
    store.saveDevices({device});

    ConfigStore reload(config);
    const QList<DeviceRecord> loaded = reload.loadDevices();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().volumeCeiling, 42);
}

void ConfigStoreTest::absentVolumeCeilingDefaultsToUnrestricted()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    // A file written before ceilings existed carries no VolumeCeiling key.
    {
        KConfigGroup devicesGroup(config, QStringLiteral("Devices"));
        KConfigGroup deviceGroup(&devicesGroup, QStringLiteral("0"));
        deviceGroup.writeEntry("Serial", QStringLiteral("SERIAL-A"));
        deviceGroup.writeEntry("DisplayName", QStringLiteral("Living Room"));
        deviceGroup.writeEntry("Address", QStringLiteral("198.51.100.10"));
        config->sync();
    }

    ConfigStore store(config);
    const QList<DeviceRecord> loaded = store.loadDevices();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().volumeCeiling, 100);
}

void ConfigStoreTest::peerListenerSettingsRoundTripAndDefaults()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);

    // Defaults: explicitly disabled, the design's default port.
    ConfigStore fresh(config);
    const PeerListenerSettings defaults = fresh.loadPeerListener();
    QCOMPARE(defaults.enabled, false);
    QCOMPARE(defaults.port, static_cast<quint16>(49160));

    PeerListenerSettings settings;
    settings.enabled = true;
    settings.port = 0;  // OS-assigned, a legitimate stored choice
    fresh.savePeerListener(settings);

    ConfigStore reload(config);
    const PeerListenerSettings loaded = reload.loadPeerListener();
    QCOMPARE(loaded.enabled, true);
    QCOMPARE(loaded.port, static_cast<quint16>(0));
}

void ConfigStoreTest::pairedControllersRoundTripAndSkipNamelessEntries()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    ConfigStore store(config);

    PairedController controller;
    controller.machineId = QStringLiteral("aaaabbbbccccdddd1111222233334444");
    controller.token = QStringLiteral("token-secret");
    controller.addedAt = QDateTime(QDate(2026, 11, 14), QTime(10, 30));
    store.savePairedControllers({controller});

    // A hand-edited file cannot inject a nameless or tokenless controller.
    {
        KConfigGroup peersGroup(config, QStringLiteral("Peers"));
        KConfigGroup entry(&peersGroup, QStringLiteral("Controller1"));
        entry.writeEntry("MachineId", QString());
        entry.writeEntry("Token", QStringLiteral("orphan"));
        KConfigGroup tokenless(&peersGroup, QStringLiteral("Controller2"));
        tokenless.writeEntry("MachineId", QStringLiteral("no-token-machine"));
        config->sync();
    }

    ConfigStore reload(config);
    const QList<PairedController> loaded = reload.loadPairedControllers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().machineId, controller.machineId);
    QCOMPARE(loaded.first().token, controller.token);
    QCOMPARE(loaded.first().addedAt, controller.addedAt);
}

void ConfigStoreTest::peerTargetsRoundTripAndSkipIncompleteEntries()
{
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/store.rc"), KConfig::SimpleConfig);
    ConfigStore store(config);

    PeerTarget target;
    target.machineId = QStringLiteral("eeeeffff000011112222333344445555");
    target.displayName = QStringLiteral("machine-two");
    target.address = QStringLiteral("127.0.0.1");
    target.port = 49160;
    target.token = QStringLiteral("peer-token");
    store.savePeerTargets({target});

    // Missing port or address or token: skipped, like nameless devices.
    {
        KConfigGroup targetsGroup(config, QStringLiteral("PeerTargets"));
        KConfigGroup entry(&targetsGroup, QStringLiteral("Target1"));
        entry.writeEntry("MachineId", QStringLiteral("broken-machine"));
        entry.writeEntry("Address", QStringLiteral("127.0.0.1"));
        entry.writeEntry("Token", QStringLiteral("t"));
        config->sync();
    }

    ConfigStore reload(config);
    const QList<PeerTarget> loaded = reload.loadPeerTargets();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().machineId, target.machineId);
    QCOMPARE(loaded.first().displayName, target.displayName);
    QCOMPARE(loaded.first().address, target.address);
    QCOMPARE(loaded.first().port, static_cast<quint16>(49160));
    QCOMPARE(loaded.first().token, target.token);
}

QTEST_MAIN(ConfigStoreTest)
#include "config_store_test.moc"

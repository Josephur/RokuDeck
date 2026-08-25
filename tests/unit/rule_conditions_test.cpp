// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/rule_conditions.h"

#include "core/device_state.h"
#include "core/rule_model.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace RokuDeck;
using namespace Qt::Literals::StringLiterals;

namespace {

// Fully-populated baseline: every optional present, no Unknown enums. Each
// test mutates only the axis it cares about.
[[nodiscard]] DeviceSnapshot knownSnapshot(const QString& serial) {
    DeviceSnapshot snap;
    snap.serial = serial;
    snap.power = PowerState::On;
    snap.screensaver = TriState::Inactive;
    ActiveAppState netflix;
    netflix.id = QStringLiteral("tv.netflix");
    netflix.name = QStringLiteral("Netflix");
    snap.activeApp = netflix;
    snap.volume = 15;
    snap.muted = false;
    snap.health = DeviceHealth::Online;
    return snap;
}

[[nodiscard]] DeviceSnapshot withPower(PowerState power) {
    auto snap = knownSnapshot(QStringLiteral("serial-tv"));
    snap.power = power;
    return snap;
}

[[nodiscard]] DeviceSnapshot withVolume(int volume) {
    auto snap = knownSnapshot(QStringLiteral("serial-tv"));
    snap.volume = volume;
    return snap;
}

[[nodiscard]] FieldPredicate predicate(const QString& device, const QString& field,
                                       const QString& op, QJsonValue value) {
    FieldPredicate parsed;
    parsed.device = device;
    parsed.field = field;
    parsed.op = op;
    parsed.value = std::move(value);
    return parsed;
}

[[nodiscard]] ConditionNode leafNode(FieldPredicate leaf) {
    ConditionNode node;
    node.kind = ConditionNode::Kind::Leaf;
    node.leaf = std::move(leaf);
    return node;
}

[[nodiscard]] ConditionNode allNode(QList<ConditionNode> children) {
    ConditionNode node;
    node.kind = ConditionNode::Kind::All;
    node.children = std::move(children);
    return node;
}

[[nodiscard]] ConditionNode anyNode(QList<ConditionNode> children) {
    ConditionNode node;
    node.kind = ConditionNode::Kind::Any;
    node.children = std::move(children);
    return node;
}

[[nodiscard]] ConditionNode notNode(ConditionNode child) {
    ConditionNode node;
    node.kind = ConditionNode::Kind::Not;
    node.notChild.append(std::move(child));
    return node;
}

[[nodiscard]] RuleDocument rule(const QString& id, const QJsonObject& trigger,
                                bool enabled = true) {
    const QString name = u"Fixture "_s + id;
    const auto parsed =
        parseRule(QJsonObject{{"schemaVersion"_L1, 1},
                              {"id"_L1, id},
                              {"name"_L1, name},
                              {"enabled"_L1, enabled},
                              {"trigger"_L1, trigger},
                              {"actions"_L1, QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                                                   {"key"_L1, "Home"_L1}}}}});
    Q_ASSERT(parsed.has_value());
    return parsed.value();
}

[[nodiscard]] RuleDocument powerOnStateChangeRule(const QString& id, bool enabled = true) {
    return rule(id,
                QJsonObject{{"type"_L1, "stateChange"_L1},
                            {"field"_L1, "power"_L1},
                            {"op"_L1, "is"_L1},
                            {"value"_L1, "on"_L1}},
                enabled);
}

} // namespace

class RuleConditionsTest final : public QObject {
    Q_OBJECT

  private Q_SLOTS:
    void powerPredicateComparesRenderedEnumValues();
    void absentOrUnknownFieldNeverMatchesEvenForIsNot();
    void volumeOpsAreStrictAndBetweenIsClosed();
    void numericOpsNeverMatchMutedBooleans();
    void playingPredicateRendersBooleansAndNumericOpsNeverMatch();
    void mediaPredicateRendersStatesAndNeverMatchesUnknown();
    void dueStateChangeFiresOnPlayingEdge();
    void appPredicateMatchesTheActiveAppId();
    void healthPredicateRendersHealthNames();
    void screensaverPredicateRendersTriState();
    void deviceScopingDecidesWhichSnapshotIsEvaluated();
    void conditionTreesWalkAllAnyAndNot();
    void dueStateChangeFiresOnlyOnRisingEdge();
    void dueStateChangeSkipsDisabledAndNonStateChangeRules();
    void dueStateChangePreservesRuleListOrder();
    void dueStateChangeRespectsTheTriggerDeviceScope();
};

void RuleConditionsTest::powerPredicateComparesRenderedEnumValues() {
    const DeviceSnapshot on = withPower(PowerState::On);
    const QHash<QString, DeviceSnapshot> onMap{{on.serial, on}};
    const QStringList onTargets{on.serial};
    QVERIFY(predicateMatches(predicate(on.serial, u"power"_s, u"is"_s, u"on"_s), onMap, onTargets));
    QVERIFY(
        !predicateMatches(predicate(on.serial, u"power"_s, u"is"_s, u"standby"_s), onMap, onTargets));
    QVERIFY(predicateMatches(
        predicate(on.serial, u"power"_s, u"isNot"_s, u"standby"_s), onMap, onTargets));

    const DeviceSnapshot standby = withPower(PowerState::Standby);
    const QHash<QString, DeviceSnapshot> standbyMap{{standby.serial, standby}};
    QVERIFY(predicateMatches(predicate(standby.serial, u"power"_s, u"is"_s, u"standby"_s),
                             standbyMap, QStringList{standby.serial}));
    QVERIFY(predicateMatches(predicate(standby.serial, u"power"_s, u"isNot"_s, u"on"_s),
                             standbyMap, QStringList{standby.serial}));
    QVERIFY(!predicateMatches(predicate(standby.serial, u"power"_s, u"is"_s, u"on"_s),
                              standbyMap, QStringList{standby.serial}));
}

void RuleConditionsTest::absentOrUnknownFieldNeverMatchesEvenForIsNot() {
    const DeviceSnapshot blank;  // all enums Unknown, optionals unset
    const QHash<QString, DeviceSnapshot> blankMap{{blank.serial, blank}};
    const QStringList blankTargets{blank.serial};

    QVERIFY(!predicateMatches(
        predicate(blank.serial, u"power"_s, u"is"_s, u"on"_s), blankMap, blankTargets));
    QVERIFY(!predicateMatches(
        predicate(blank.serial, u"power"_s, u"isNot"_s, u"on"_s), blankMap, blankTargets));
    QVERIFY(!predicateMatches(predicate(blank.serial, u"screensaver"_s, u"isNot"_s, u"active"_s),
                              blankMap, blankTargets));
    QVERIFY(!predicateMatches(predicate(blank.serial, u"health"_s, u"isNot"_s, u"offline"_s),
                              blankMap, blankTargets));
    QVERIFY(!predicateMatches(
        predicate(blank.serial, u"muted"_s, u"isNot"_s, true), blankMap, blankTargets));
    QVERIFY(!predicateMatches(
        predicate(blank.serial, u"volume"_s, u"isNot"_s, 50), blankMap, blankTargets));
    QVERIFY(!predicateMatches(predicate(blank.serial, u"app"_s, u"isNot"_s, u"tv.netflix"_s),
                              blankMap, blankTargets));

    // Known power does not rescue the unset optionals on the same snapshot.
    DeviceSnapshot partial = withPower(PowerState::On);
    partial.volume.reset();
    partial.activeApp.reset();
    const QHash<QString, DeviceSnapshot> partialMap{{partial.serial, partial}};
    QVERIFY(predicateMatches(predicate(partial.serial, u"power"_s, u"is"_s, u"on"_s), partialMap,
                             QStringList{partial.serial}));
    QVERIFY(!predicateMatches(predicate(partial.serial, u"volume"_s, u"isNot"_s, 99), partialMap,
                              QStringList{partial.serial}));
    QVERIFY(!predicateMatches(predicate(partial.serial, u"app"_s, u"isNot"_s, u"tv.netflix"_s),
                              partialMap, QStringList{partial.serial}));
}

void RuleConditionsTest::volumeOpsAreStrictAndBetweenIsClosed() {
    const auto volumeMatches = [](const DeviceSnapshot& snap, const QString& op,
                                  QJsonValue value) {
        const QHash<QString, DeviceSnapshot> snapshots{{snap.serial, snap}};
        return predicateMatches(predicate(snap.serial, u"volume"_s, op, std::move(value)),
                                snapshots, QStringList{snap.serial});
    };

    const DeviceSnapshot mid = withVolume(15);
    QVERIFY(volumeMatches(mid, u"is"_s, 15));
    QVERIFY(volumeMatches(mid, u"is"_s, u"15"_s));  // canonical decimal both sides
    QVERIFY(!volumeMatches(mid, u"is"_s, 16));
    QVERIFY(volumeMatches(mid, u"gt"_s, 14));
    QVERIFY(!volumeMatches(mid, u"gt"_s, 15));  // strict
    QVERIFY(volumeMatches(mid, u"lt"_s, 16));
    QVERIFY(!volumeMatches(mid, u"lt"_s, 15));  // strict
    QVERIFY(volumeMatches(mid, u"between"_s, QJsonArray{10, 20}));
    QVERIFY(!volumeMatches(mid, u"between"_s, QJsonArray{16, 20}));
    QVERIFY(!volumeMatches(mid, u"between"_s, QJsonArray{10, 14}));

    const DeviceSnapshot low = withVolume(10);
    const DeviceSnapshot high = withVolume(20);
    QVERIFY(volumeMatches(low, u"between"_s, QJsonArray{10, 20}));   // closed lower edge
    QVERIFY(volumeMatches(high, u"between"_s, QJsonArray{10, 20}));  // closed upper edge

    // Non-numeric operands never match numeric ops.
    QVERIFY(!volumeMatches(mid, u"gt"_s, u"loud"_s));
    QVERIFY(!volumeMatches(mid, u"between"_s, QJsonArray{u"a"_s, u"b"_s}));
    QVERIFY(!volumeMatches(mid, u"gt"_s, QJsonArray{1, 2}));
}

void RuleConditionsTest::numericOpsNeverMatchMutedBooleans() {
    DeviceSnapshot mutedTv = knownSnapshot(QStringLiteral("serial-tv"));
    mutedTv.muted = true;

    const auto mutedMatches = [&mutedTv](const QString& op, QJsonValue value) {
        const QHash<QString, DeviceSnapshot> snapshots{{mutedTv.serial, mutedTv}};
        return predicateMatches(predicate(mutedTv.serial, u"muted"_s, op, std::move(value)),
                                snapshots, QStringList{mutedTv.serial});
    };
    QVERIFY(mutedMatches(u"is"_s, true));
    QVERIFY(mutedMatches(u"is"_s, u"true"_s));
    QVERIFY(!mutedMatches(u"is"_s, false));
    QVERIFY(mutedMatches(u"isNot"_s, false));
    QVERIFY(!mutedMatches(u"isNot"_s, true));
    QVERIFY(!mutedMatches(u"gt"_s, 0));
    QVERIFY(!mutedMatches(u"lt"_s, 1));
    QVERIFY(!mutedMatches(u"between"_s, QJsonArray{0, 1}));

    DeviceSnapshot quietTv = knownSnapshot(QStringLiteral("serial-tv"));
    quietTv.muted = false;
    const QHash<QString, DeviceSnapshot> unmutedMap{{quietTv.serial, quietTv}};
    QVERIFY(predicateMatches(predicate(quietTv.serial, u"muted"_s, u"is"_s, false), unmutedMap,
                             QStringList{quietTv.serial}));
    QVERIFY(predicateMatches(predicate(quietTv.serial, u"muted"_s, u"is"_s, u"false"_s),
                             unmutedMap, QStringList{quietTv.serial}));
    QVERIFY(!predicateMatches(predicate(quietTv.serial, u"muted"_s, u"isNot"_s, false), unmutedMap,
                              QStringList{quietTv.serial}));
}

void RuleConditionsTest::playingPredicateRendersBooleansAndNumericOpsNeverMatch() {
    DeviceSnapshot playing = knownSnapshot(QStringLiteral("serial-tv"));
    playing.playing = true;

    const auto playingMatches = [&playing](const QString& op, QJsonValue value) {
        const QHash<QString, DeviceSnapshot> snapshots{{playing.serial, playing}};
        return predicateMatches(predicate(playing.serial, u"playing"_s, op, std::move(value)),
                                snapshots, QStringList{playing.serial});
    };
    QVERIFY(playingMatches(u"is"_s, true));
    QVERIFY(!playingMatches(u"is"_s, false));
    QVERIFY(playingMatches(u"isNot"_s, false));
    QVERIFY(!playingMatches(u"isNot"_s, true));
    // Numeric ops never match a boolean field, mirroring muted.
    QVERIFY(!playingMatches(u"gt"_s, 0));
    QVERIFY(!playingMatches(u"lt"_s, 1));
    QVERIFY(!playingMatches(u"between"_s, QJsonArray{0, 1}));

    DeviceSnapshot paused = knownSnapshot(QStringLiteral("serial-tv"));
    paused.playing = false;
    const QHash<QString, DeviceSnapshot> pausedMap{{paused.serial, paused}};
    QVERIFY(predicateMatches(predicate(paused.serial, u"playing"_s, u"is"_s, false), pausedMap,
                             QStringList{paused.serial}));
    QVERIFY(!predicateMatches(predicate(paused.serial, u"playing"_s, u"isNot"_s, false), pausedMap,
                              QStringList{paused.serial}));

    // Absent playing (a Roku snapshot) never matches - not even isNot.
    const DeviceSnapshot roku = knownSnapshot(QStringLiteral("serial-tv"));
    const QHash<QString, DeviceSnapshot> rokuMap{{roku.serial, roku}};
    QVERIFY(!predicateMatches(predicate(roku.serial, u"playing"_s, u"is"_s, true), rokuMap,
                              QStringList{roku.serial}));
    QVERIFY(!predicateMatches(predicate(roku.serial, u"playing"_s, u"isNot"_s, true), rokuMap,
                              QStringList{roku.serial}));
}

void RuleConditionsTest::mediaPredicateRendersStatesAndNeverMatchesUnknown() {
    DeviceSnapshot playing = knownSnapshot(QStringLiteral("serial-tv"));
    playing.media = MediaPlaybackState::Playing;

    const auto mediaMatches = [&playing](const QString& op, QJsonValue value) {
        const QHash<QString, DeviceSnapshot> snapshots{{playing.serial, playing}};
        return predicateMatches(predicate(playing.serial, u"media"_s, op, std::move(value)),
                                snapshots, QStringList{playing.serial});
    };
    QVERIFY(mediaMatches(u"is"_s, u"playing"_s));
    QVERIFY(!mediaMatches(u"is"_s, u"paused"_s));
    QVERIFY(mediaMatches(u"isNot"_s, u"paused"_s));
    QVERIFY(!mediaMatches(u"isNot"_s, u"playing"_s));
    // Numeric ops never match a string enum field, mirroring power.
    QVERIFY(!mediaMatches(u"gt"_s, 0));
    QVERIFY(!mediaMatches(u"between"_s, QJsonArray{0, 1}));

    // Unknown media state never matches - not even isNot.
    DeviceSnapshot idle = knownSnapshot(QStringLiteral("serial-tv"));
    idle.media = MediaPlaybackState::Idle;
    const QHash<QString, DeviceSnapshot> idleMap{{idle.serial, idle}};
    QVERIFY(predicateMatches(predicate(idle.serial, u"media"_s, u"is"_s, u"idle"_s), idleMap,
                             QStringList{idle.serial}));
    const DeviceSnapshot unknown = knownSnapshot(QStringLiteral("serial-tv"));
    const QHash<QString, DeviceSnapshot> unknownMap{{unknown.serial, unknown}};
    QVERIFY(!predicateMatches(predicate(unknown.serial, u"media"_s, u"is"_s, u"idle"_s),
                              unknownMap, QStringList{unknown.serial}));
    QVERIFY(!predicateMatches(predicate(unknown.serial, u"media"_s, u"isNot"_s, u"idle"_s),
                              unknownMap, QStringList{unknown.serial}));
}

void RuleConditionsTest::dueStateChangeFiresOnPlayingEdge() {
    const QList<RuleDocument> rules{
        rule(u"media"_s,
             QJsonObject{{"type"_L1, "stateChange"_L1},
                         {"field"_L1, "playing"_L1},
                         {"op"_L1, "is"_L1},
                         {"value"_L1, true}})};
    DeviceSnapshot stopped = knownSnapshot(QStringLiteral("serial-tv"));
    stopped.playing = false;
    DeviceSnapshot started = knownSnapshot(QStringLiteral("serial-tv"));
    started.playing = true;

    QCOMPARE(dueStateChangeRuleIds(rules, started.serial, stopped, started),
             QStringList{u"media"_s});
    QVERIFY(dueStateChangeRuleIds(rules, started.serial, started, stopped).isEmpty());
    QVERIFY(dueStateChangeRuleIds(rules, started.serial, started, started).isEmpty());
    // A Roku device that never reports playing cannot fire the rule.
    const DeviceSnapshot blank = knownSnapshot(QStringLiteral("serial-roku"));
    QVERIFY(dueStateChangeRuleIds(rules, blank.serial, blank, blank).isEmpty());
}

void RuleConditionsTest::appPredicateMatchesTheActiveAppId() {
    const DeviceSnapshot snap = knownSnapshot(QStringLiteral("serial-tv"));  // tv.netflix active
    const QHash<QString, DeviceSnapshot> snapshots{{snap.serial, snap}};
    QVERIFY(predicateMatches(predicate(snap.serial, u"app"_s, u"is"_s, u"tv.netflix"_s), snapshots,
                             QStringList{snap.serial}));
    QVERIFY(!predicateMatches(predicate(snap.serial, u"app"_s, u"is"_s, u"tv.youtube"_s), snapshots,
                              QStringList{snap.serial}));

    DeviceSnapshot homeScreen = knownSnapshot(QStringLiteral("serial-tv"));
    homeScreen.activeApp.reset();
    const QHash<QString, DeviceSnapshot> homeMap{{homeScreen.serial, homeScreen}};
    QVERIFY(!predicateMatches(predicate(homeScreen.serial, u"app"_s, u"is"_s, u"tv.netflix"_s),
                              homeMap, QStringList{homeScreen.serial}));
    QVERIFY(!predicateMatches(predicate(homeScreen.serial, u"app"_s, u"isNot"_s, u"tv.netflix"_s),
                              homeMap, QStringList{homeScreen.serial}));
}

void RuleConditionsTest::healthPredicateRendersHealthNames() {
    const DeviceSnapshot online = knownSnapshot(QStringLiteral("serial-tv"));
    const QHash<QString, DeviceSnapshot> onlineMap{{online.serial, online}};
    QVERIFY(predicateMatches(predicate(online.serial, u"health"_s, u"is"_s, u"online"_s), onlineMap,
                             QStringList{online.serial}));
    QVERIFY(predicateMatches(predicate(online.serial, u"health"_s, u"isNot"_s, u"degraded"_s),
                             onlineMap, QStringList{online.serial}));

    DeviceSnapshot struggling = knownSnapshot(QStringLiteral("serial-tv"));
    struggling.health = DeviceHealth::Degraded;
    const QHash<QString, DeviceSnapshot> degradedMap{{struggling.serial, struggling}};
    QVERIFY(predicateMatches(predicate(struggling.serial, u"health"_s, u"is"_s, u"degraded"_s),
                             degradedMap, QStringList{struggling.serial}));
    QVERIFY(!predicateMatches(predicate(struggling.serial, u"health"_s, u"is"_s, u"online"_s),
                              degradedMap, QStringList{struggling.serial}));

    // Unknown health never matches - not even against the literal "unknown".
    const DeviceSnapshot blank;
    const QHash<QString, DeviceSnapshot> blankMap{{blank.serial, blank}};
    QVERIFY(!predicateMatches(predicate(blank.serial, u"health"_s, u"is"_s, u"unknown"_s), blankMap,
                              QStringList{blank.serial}));
    QVERIFY(!predicateMatches(predicate(blank.serial, u"health"_s, u"isNot"_s, u"unknown"_s),
                              blankMap, QStringList{blank.serial}));
}

void RuleConditionsTest::screensaverPredicateRendersTriState() {
    DeviceSnapshot showing = knownSnapshot(QStringLiteral("serial-tv"));
    showing.screensaver = TriState::Active;
    const QHash<QString, DeviceSnapshot> activeMap{{showing.serial, showing}};
    QVERIFY(predicateMatches(predicate(showing.serial, u"screensaver"_s, u"is"_s, u"active"_s),
                             activeMap, QStringList{showing.serial}));
    QVERIFY(!predicateMatches(predicate(showing.serial, u"screensaver"_s, u"is"_s, u"inactive"_s),
                              activeMap, QStringList{showing.serial}));

    DeviceSnapshot idle = knownSnapshot(QStringLiteral("serial-tv"));
    idle.screensaver = TriState::Inactive;
    const QHash<QString, DeviceSnapshot> inactiveMap{{idle.serial, idle}};
    QVERIFY(predicateMatches(predicate(idle.serial, u"screensaver"_s, u"isNot"_s, u"active"_s),
                             inactiveMap, QStringList{idle.serial}));

    const DeviceSnapshot blank;
    QVERIFY(!predicateMatches(predicate(blank.serial, u"screensaver"_s, u"isNot"_s, u"active"_s),
                              QHash<QString, DeviceSnapshot>{{blank.serial, blank}},
                              QStringList{blank.serial}));
}

void RuleConditionsTest::deviceScopingDecidesWhichSnapshotIsEvaluated() {
    const DeviceSnapshot living = knownSnapshot(QStringLiteral("serial-living"));
    DeviceSnapshot bedroom = knownSnapshot(QStringLiteral("serial-bedroom"));
    bedroom.power = PowerState::Standby;
    const QHash<QString, DeviceSnapshot> snapshots{{living.serial, living},
                                                   {bedroom.serial, bedroom}};
    const auto powerIsOn = [](const QString& device) {
        return predicate(device, u"power"_s, u"is"_s, u"on"_s);
    };

    // Named serial: that snapshot must exist in the map and match.
    QVERIFY(predicateMatches(powerIsOn(living.serial), snapshots, {living.serial}));
    QVERIFY(!predicateMatches(powerIsOn(bedroom.serial), snapshots,
                              {living.serial, bedroom.serial}));
    QVERIFY(!predicateMatches(powerIsOn(QStringLiteral("serial-garage")), snapshots,
                              {living.serial}));

    // Wildcard: every target must match; empty targets never match.
    QVERIFY(predicateMatches(powerIsOn(QStringLiteral("*")), snapshots, {living.serial}));
    QVERIFY(!predicateMatches(powerIsOn(QStringLiteral("*")), snapshots,
                              {living.serial, bedroom.serial}));
    QVERIFY(!predicateMatches(powerIsOn(QStringLiteral("*")), snapshots, {}));

    // An empty device string means the same wildcard.
    QVERIFY(predicateMatches(predicate({}, u"power"_s, u"is"_s, u"on"_s), snapshots,
                             QStringList{living.serial}));
    QVERIFY(!predicateMatches(predicate({}, u"power"_s, u"is"_s, u"on"_s), snapshots, {}));
}

void RuleConditionsTest::conditionTreesWalkAllAnyAndNot() {
    const DeviceSnapshot snap = knownSnapshot(QStringLiteral("serial-tv"));
    const QHash<QString, DeviceSnapshot> snapshots{{snap.serial, snap}};
    const QStringList targets{snap.serial};

    const auto tvOn = leafNode(predicate(u"*"_s, u"power"_s, u"is"_s, u"on"_s));      // true here
    const auto muted = leafNode(predicate(u"*"_s, u"muted"_s, u"is"_s, true));         // false here
    const auto loud = leafNode(predicate(u"*"_s, u"volume"_s, u"gt"_s, 100));          // false here

    QVERIFY(conditionsMet(allNode({tvOn}), snapshots, targets));
    QVERIFY(!conditionsMet(allNode({tvOn, muted}), snapshots, targets));
    QVERIFY(conditionsMet(anyNode({tvOn, muted}), snapshots, targets));
    QVERIFY(!conditionsMet(anyNode({loud, muted}), snapshots, targets));
    QVERIFY(conditionsMet(notNode(muted), snapshots, targets));
    QVERIFY(!conditionsMet(notNode(tvOn), snapshots, targets));
    QVERIFY(conditionsMet(allNode({tvOn, anyNode({loud, notNode(muted)})}), snapshots, targets));

    // Leaves keep their own device scope inside a tree.
    DeviceSnapshot other = knownSnapshot(QStringLiteral("serial-other"));
    other.power = PowerState::Standby;
    const QHash<QString, DeviceSnapshot> twoDevices{{snap.serial, snap}, {other.serial, other}};
    const auto thisOn = leafNode(predicate(snap.serial, u"power"_s, u"is"_s, u"on"_s));
    const auto otherStandby =
        leafNode(predicate(other.serial, u"power"_s, u"is"_s, u"standby"_s));
    QVERIFY(conditionsMet(allNode({thisOn, otherStandby}), twoDevices,
                          {snap.serial, other.serial}));
    // A wildcard leaf still demands every listed target to match.
    QVERIFY(!conditionsMet(allNode({tvOn, otherStandby}), twoDevices,
                           {snap.serial, other.serial}));
}

void RuleConditionsTest::dueStateChangeFiresOnlyOnRisingEdge() {
    const QList<RuleDocument> rules{powerOnStateChangeRule(QStringLiteral("wake"))};
    const DeviceSnapshot off = withPower(PowerState::Standby);
    const DeviceSnapshot on = withPower(PowerState::On);

    QCOMPARE(dueStateChangeRuleIds(rules, on.serial, off, on), QStringList{u"wake"_s});
    QVERIFY(dueStateChangeRuleIds(rules, on.serial, on, off).isEmpty());
    QVERIFY(dueStateChangeRuleIds(rules, on.serial, on, on).isEmpty());
    QVERIFY(dueStateChangeRuleIds(rules, on.serial, off, off).isEmpty());
}

void RuleConditionsTest::dueStateChangeSkipsDisabledAndNonStateChangeRules() {
    const QList<RuleDocument> rules{
        powerOnStateChangeRule(QStringLiteral("asleep"), false),
        rule(QStringLiteral("man"), QJsonObject{{"type"_L1, "manual"_L1}}),
        rule(QStringLiteral("clock"), QJsonObject{{"type"_L1, "time"_L1},
                                                 {"kind"_L1, "daily"_L1},
                                                 {"time"_L1, "23:00"_L1}}),
        rule(QStringLiteral("cadence"),
             QJsonObject{{"type"_L1, "interval"_L1}, {"minutes"_L1, 30}}),
        powerOnStateChangeRule(QStringLiteral("awake"))};
    const DeviceSnapshot off = withPower(PowerState::Standby);
    const DeviceSnapshot on = withPower(PowerState::On);

    QCOMPARE(dueStateChangeRuleIds(rules, on.serial, off, on), QStringList{u"awake"_s});
}

void RuleConditionsTest::dueStateChangePreservesRuleListOrder() {
    const QList<RuleDocument> rules{
        powerOnStateChangeRule(QStringLiteral("zulu")),
        powerOnStateChangeRule(QStringLiteral("yankee")),
        powerOnStateChangeRule(QStringLiteral("xray"))};
    const DeviceSnapshot off = withPower(PowerState::Standby);
    const DeviceSnapshot on = withPower(PowerState::On);

    const QStringList expectedDue{u"zulu"_s, u"yankee"_s, u"xray"_s};
    QCOMPARE(dueStateChangeRuleIds(rules, on.serial, off, on), expectedDue);
}

void RuleConditionsTest::dueStateChangeRespectsTheTriggerDeviceScope() {
    // A device-scoped rule watches that serial's edges only; the wildcard
    // twin keeps observing everything.
    const QList<RuleDocument> rules{
        rule(u"tv-only"_s,
             QJsonObject{{"type"_L1, "stateChange"_L1},
                         {"device"_L1, "SERIAL-B"_L1},
                         {"field"_L1, "power"_L1},
                         {"op"_L1, "is"_L1},
                         {"value"_L1, "on"_L1}}),
        powerOnStateChangeRule(QStringLiteral("any-device"))};
    DeviceSnapshot offA = knownSnapshot(QStringLiteral("SERIAL-A"));
    offA.power = PowerState::Standby;
    const DeviceSnapshot onA = knownSnapshot(QStringLiteral("SERIAL-A"));
    DeviceSnapshot offB = knownSnapshot(QStringLiteral("SERIAL-B"));
    offB.power = PowerState::Standby;
    const DeviceSnapshot onB = knownSnapshot(QStringLiteral("SERIAL-B"));

    QCOMPARE(dueStateChangeRuleIds(rules, offB.serial, offB, onB),
             (QStringList{u"tv-only"_s, u"any-device"_s}));
    QCOMPARE(dueStateChangeRuleIds(rules, offA.serial, offA, onA),
             QStringList{u"any-device"_s});
}

QTEST_MAIN(RuleConditionsTest)
#include "rule_conditions_test.moc"

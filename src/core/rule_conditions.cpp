// SPDX-License-Identifier: GPL-3.0-or-later
#include "rule_conditions.h"

#include <QJsonArray>

#include <algorithm>
#include <optional>

namespace RokuDeck {
using namespace Qt::Literals::StringLiterals;
namespace {

[[nodiscard]] std::optional<double> toNumber(const QJsonValue& value)
{
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok) {
            return parsed;
        }
    }
    return std::nullopt;
}

// Canonical comparison string for a predicate operand: booleans render as
// "true"/"false" and numbers as decimal, mirroring the field renderings.
[[nodiscard]] QString operandString(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? u"true"_s : u"false"_s;
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    return {};
}

// Renders the snapshot's field into its canonical string form; nullopt when
// the value is absent or Unknown (such fields never match any op).
[[nodiscard]] std::optional<QString> renderedField(const DeviceSnapshot& snapshot,
                                                   const QString& field)
{
    if (field == u"power"_s) {
        switch (snapshot.power) {
        case PowerState::On:
            return u"on"_s;
        case PowerState::Standby:
            return u"standby"_s;
        case PowerState::Unknown:
            break;
        }
        return std::nullopt;
    }
    if (field == u"screensaver"_s) {
        switch (snapshot.screensaver) {
        case TriState::Active:
            return u"active"_s;
        case TriState::Inactive:
            return u"inactive"_s;
        case TriState::Unknown:
            break;
        }
        return std::nullopt;
    }
    if (field == u"health"_s) {
        switch (snapshot.health) {
        case DeviceHealth::Online:
            return u"online"_s;
        case DeviceHealth::Standby:
            return u"standby"_s;
        case DeviceHealth::Degraded:
            return u"degraded"_s;
        case DeviceHealth::Offline:
            return u"offline"_s;
        case DeviceHealth::Unknown:
            break;
        }
        return std::nullopt;
    }
    if (field == u"muted"_s) {
        if (!snapshot.muted.has_value()) {
            return std::nullopt;
        }
        return *snapshot.muted ? u"true"_s : u"false"_s;
    }
    if (field == u"playing"_s) {
        if (!snapshot.playing.has_value()) {
            return std::nullopt;
        }
        return *snapshot.playing ? u"true"_s : u"false"_s;
    }
    if (field == u"media"_s) {
        switch (snapshot.media) {
        case MediaPlaybackState::Playing:
            return u"playing"_s;
        case MediaPlaybackState::Paused:
            return u"paused"_s;
        case MediaPlaybackState::Stopped:
            return u"stopped"_s;
        case MediaPlaybackState::Idle:
            return u"idle"_s;
        case MediaPlaybackState::Unknown:
            break;
        }
        return std::nullopt;
    }
    if (field == u"volume"_s) {
        if (!snapshot.volume.has_value()) {
            return std::nullopt;
        }
        return QString::number(*snapshot.volume);
    }
    if (field == u"app"_s) {
        if (!snapshot.activeApp.has_value()) {
            return std::nullopt;
        }
        return snapshot.activeApp->id;
    }
    return std::nullopt;
}

[[nodiscard]] bool leafMatches(const FieldPredicate& predicate, const DeviceSnapshot& snapshot)
{
    const std::optional<QString> actual = renderedField(snapshot, predicate.field);
    if (!actual.has_value()) {
        return false;
    }

    if (predicate.op == u"is"_s) {
        return *actual == operandString(predicate.value);
    }
    if (predicate.op == u"isNot"_s) {
        return *actual != operandString(predicate.value);
    }

    // gt/lt/between are strictly numeric.
    bool actualOk = false;
    const double actualNumber = actual->toDouble(&actualOk);
    if (!actualOk) {
        return false;
    }
    if (predicate.op == u"gt"_s || predicate.op == u"lt"_s) {
        const std::optional<double> bound = toNumber(predicate.value);
        if (!bound.has_value()) {
            return false;
        }
        return predicate.op == u"gt"_s ? actualNumber > *bound : actualNumber < *bound;
    }
    if (predicate.op == u"between"_s) {
        const QJsonArray bounds = predicate.value.toArray();
        if (bounds.size() != 2) {
            return false;
        }
        const std::optional<double> low = toNumber(bounds.at(0));
        const std::optional<double> high = toNumber(bounds.at(1));
        if (!low.has_value() || !high.has_value()) {
            return false;
        }
        return actualNumber >= *low && actualNumber <= *high;  // CLOSED interval
    }
    return false;
}

} // namespace

bool predicateMatches(const FieldPredicate& predicate,
                      const QHash<QString, DeviceSnapshot>& snapshots,
                      const QStringList& targets)
{
    if (predicate.device.isEmpty() || predicate.device == u"*"_s) {
        if (targets.isEmpty()) {
            return false;
        }
        return std::all_of(targets.cbegin(), targets.cend(),
                           [&predicate, &snapshots](const QString& serial) {
                               const auto it = snapshots.constFind(serial);
                               return it != snapshots.constEnd()
                                   && leafMatches(predicate, it.value());
                           });
    }
    const auto it = snapshots.constFind(predicate.device);
    return it != snapshots.constEnd() && leafMatches(predicate, it.value());
}

bool conditionsMet(const ConditionNode& conditions,
                   const QHash<QString, DeviceSnapshot>& snapshots,
                   const QStringList& targets)
{
    switch (conditions.kind) {
    case ConditionNode::Kind::Leaf:
        return predicateMatches(conditions.leaf, snapshots, targets);
    case ConditionNode::Kind::All:
        return std::all_of(conditions.children.cbegin(), conditions.children.cend(),
                           [&snapshots, &targets](const ConditionNode& child) {
                               return conditionsMet(child, snapshots, targets);
                           });
    case ConditionNode::Kind::Any:
        return std::any_of(conditions.children.cbegin(), conditions.children.cend(),
                           [&snapshots, &targets](const ConditionNode& child) {
                               return conditionsMet(child, snapshots, targets);
                           });
    case ConditionNode::Kind::Not:
        return !conditions.notChild.isEmpty()
            && !conditionsMet(conditions.notChild.front(), snapshots, targets);
    }
    return false;
}

QStringList dueStateChangeRuleIds(const QList<RuleDocument>& rules,
                                  const QString& serial,
                                  const DeviceSnapshot& previous,
                                  const DeviceSnapshot& current)
{
    QStringList due;
    const QStringList targets{serial};
    for (const RuleDocument& rule : rules) {
        if (!rule.enabled || rule.trigger.type != u"stateChange"_s) {
            continue;
        }
        // An explicitly scoped trigger observes that serial's edges only; the
        // wildcard (or empty, its legacy spelling) keeps watching every edge.
        const QString& scope = rule.trigger.stateChange.device;
        if (!scope.isEmpty() && scope != u"*"_s && scope != serial) {
            continue;
        }
        const QHash<QString, DeviceSnapshot> previousSnapshots{{serial, previous}};
        const QHash<QString, DeviceSnapshot> currentSnapshots{{serial, current}};
        const bool wasMatched =
            predicateMatches(rule.trigger.stateChange, previousSnapshots, targets);
        const bool isMatched =
            predicateMatches(rule.trigger.stateChange, currentSnapshots, targets);
        if (!wasMatched && isMatched) {
            due.append(rule.id);
        }
    }
    return due;
}

QStringList reassertionDueRuleIds(const QList<RuleDocument>& rules,
                                  const QString& serial,
                                  const DeviceSnapshot& current,
                                  const QDateTime& now)
{
    QStringList due;
    const QStringList targets{serial};
    for (const RuleDocument& rule : rules) {
        if (!rule.enabled || rule.trigger.type != u"stateChange"_s) {
            continue;
        }
        if (rule.trigger.reassertSeconds <= 0) {
            continue;
        }
        // The same scoping contract as the edge path: an explicitly scoped
        // trigger observes that serial's edges only; a wildcard keeps
        // watching every edge it was allowed to watch.
        const QString& scope = rule.trigger.stateChange.device;
        if (!scope.isEmpty() && scope != u"*"_s && scope != serial) {
            continue;
        }
        // "Still holds": the predicate matches the current reading.
        if (!predicateMatches(rule.trigger.stateChange,
                              QHash<QString, DeviceSnapshot>{{serial, current}},
                              targets)) {
            continue;
        }
        // The edge owns the first fire; re-assertion continues a run history
        // that already exists, so a daemon restart never surprise-fires a
        // rule whose edge was never seen.
        const QDateTime lastFired =
            QDateTime::fromString(rule.lastRun.firedAtIso, Qt::ISODateWithMs);
        if (!lastFired.isValid()
            || lastFired.addSecs(rule.trigger.reassertSeconds) > now) {
            continue;
        }
        due.append(rule.id);
    }
    return due;
}

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "parse_result.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTime>

#include <optional>

namespace RokuDeck {

// A field predicate is the leaf of every condition tree and of state-change
// triggers: "device X's volume is between 10 and 20" etc. An empty device
// means the wildcard "*" - the rule's own targets.
struct FieldPredicate {
    QString device = QStringLiteral("*");
    QString field;
    QString op;
    QJsonValue value;
};

// Recursive condition tree: exactly one of all/any/not/leaf per node.
// Children live in lists because a node embeds its own type recursively.
struct ConditionNode {
    enum class Kind { All, Any, Not, Leaf };
    Kind kind = Kind::Leaf;
    QList<ConditionNode> children;  // All / Any
    QList<ConditionNode> notChild;  // Not: exactly one entry
    FieldPredicate leaf;            // Leaf
};

// One step in a rule's action list. ifThenElse nests whole action lists,
// which is what makes rules compositional rather than flat.
struct ActionNode {
    QString type;  // keypress|launchApp|setVolume|mute|delay|runRule|ifThenElse
    QString key;
    QString appId;
    QString ruleId;
    QString script;  // runScript: name inside the daemon's scripts directory
    int volume = -1;
    int mutedFlag = -1;  // -1 unset, else 0/1
    int delaySeconds = 0;
    ConditionNode condition;  // ifThenElse
    QList<ActionNode> thenActions;
    QList<ActionNode> elseActions;
};

// A time-of-day window restricting WHEN a trigger may fire (spec §2). The
// bounds stay strings in HH:MM form so documents round-trip byte-identically;
// start == end is rejected at parse time and end < start wraps midnight.
struct TimeWindow {
    QString start;
    QString end;
};

struct TriggerSpec {
    QString type;  // time|interval|stateChange|manual
    QString timeKind;  // daily|weekdays|once
    QString time;  // HH:MM
    QString date;  // YYYY-MM-DD, once only
    int minutes = 0;  // interval
    FieldPredicate stateChange;
    std::optional<TimeWindow> activeWindow;  // any type
};

struct RuleLastRun {
    QString firedAtIso;
    QString outcome;
};

struct RuleDocument {
    QString id;
    QString name;
    QString mode = QStringLiteral("queued");
    bool enabled = true;
    QStringList devices;  // empty list == "*"
    TriggerSpec trigger;
    std::optional<ConditionNode> conditions;
    QList<ActionNode> actions;
    RuleLastRun lastRun;
};

[[nodiscard]] ParseResult<RuleDocument> parseRule(const QJsonObject& object);
[[nodiscard]] QJsonObject ruleJson(const RuleDocument& rule);

} // namespace RokuDeck

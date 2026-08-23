# Automations M1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Daemon-owned automation rules: typed JSON model, config persistence, full CRUD over D-Bus with `RulesChanged`, and manual (`RunRule`) execution of keypress/launch macros with consent gating and last-run records.

**Architecture:** A shared `RuleDocument` model lives in `src/core` next to the snapshot parsers. The daemon keeps rules in memory (mirrored into `[Rules]` KConfig groups via `ConfigStore`), exposes CRUD through new `io.github.josephur.RokuDeck1` methods, and executes enabled manual rules through the existing `SendCommand` pipeline so every safety gate applies unchanged.

**Tech Stack:** C++20 (no exceptions, `QT_NO_CAST_FROM_ASCII`), Qt 6 Core/DBus/Test, KConfig, QtTest.

**Spec:** `docs/superpowers/specs/2026-08-23-automations-design.md` (§3 data model, §5 storage, §6 D-Bus, §8 testing)

## Global Constraints

- All string literals: `QStringLiteral(...)` or `_s` (tests: `using namespace Qt::Literals::StringLiterals;`). ASCII casts are compile errors.
- Build is `-Werror`; no exceptions in daemon/core targets — propagate failures via `ParseResult`/error strings.
- JSON envelopes carry `"schemaVersion": 1`.
- Validation is strict: unknown keys/types rejected with specific error strings (discipline of `validateCommand`).
- Recursion depth cap for conditions/actions: **16**.
- Every task ends green under `ctest` from `build/dev` before committing.
- Commit messages follow repo convention: `type(scope): lowercase imperative summary`.

---

### Task 1: Rule model — types, parse, serialize

**Files:**
- Create: `src/core/rule_model.h`
- Create: `src/core/rule_model.cpp`
- Modify: `src/core/CMakeLists.txt` (append `rule_model.cpp` to the existing core target's source list)
- Test: Create `tests/unit/rule_model_test.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test target after `device-parsers-test`, same shape)

**Interfaces:**
- Consumes: `ParseResult<T>` from `src/core/parse_result.h`.
- Produces (used by Tasks 3–6):
  ```cpp
  namespace RokuDeck {
  struct FieldPredicate { QString device{"*"}; QString field; QString op; QJsonValue value; };
  struct ConditionNode {
      enum class Kind { All, Any, Not, Leaf } kind = Kind::Leaf;
      QList<ConditionNode> children;        // All / Any
      std::optional<ConditionNode> child;   // Not
      FieldPredicate leaf;                  // Leaf
  };
  struct ActionNode {
      QString type;            // keypress|launchApp|setVolume|mute|delay|runRule|ifThenElse
      QString key, appId, ruleId;
      int volume = -1;         // setVolume
      int mutedFlag = -1;      // mute: -1 unset else 0/1
      int delaySeconds = 0;
      ConditionNode condition; // ifThenElse
      QList<ActionNode> thenActions, elseActions;
  };
  struct TriggerSpec {
      QString type;            // time|interval|stateChange|manual
      QString timeKind;        // daily|weekdays|once
      QString time;            // "HH:MM" (time)
      QString date;            // "YYYY-MM-DD" (once)
      int minutes = 0;         // interval
      FieldPredicate stateChange;
  };
  struct RuleLastRun { QString firedAtIso; QString outcome; };
  struct RuleDocument {
      QString id, name, mode = QStringLiteral("queued");
      bool enabled = true;
      QStringList devices;     // empty list == "*"
      TriggerSpec trigger;
      std::optional<ConditionNode> conditions;
      QList<ActionNode> actions;
      RuleLastRun lastRun;
  };
  [[nodiscard]] ParseResult<RuleDocument> parseRule(const QJsonObject& object);
  [[nodiscard]] QJsonObject ruleJson(const RuleDocument& rule);
  }
  ```

- [ ] **Step 1: Write the failing round-trip test**

```cpp
// tests/unit/rule_model_test.cpp
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

  private Q_SLOTS:
    void parsesAndSerializesACompleteRule();

  private:
    static QJsonObject completeRuleJson();
};

QJsonObject RuleModelTest::completeRuleJson() {
    return QJsonObject{
        {"schemaVersion"_L1, 1},
        {"id"_L1, "rABC"_L1},
        {"name"_L1, "Night volume"_L1},
        {"enabled"_L1, true},
        {"mode"_L1, "queued"_L1},
        {"devices"_L1, QJsonArray{"X001"_L1}},
        {"trigger"_L1, QJsonObject{{"type"_L1, "time"_L1},
                                   {"kind"_L1, "weekdays"_L1},
                                   {"time"_L1, "23:00"_L1}}},
        {"conditions"_L1,
         QJsonObject{{"all"_L1, QJsonArray{
             QJsonObject{{"leaf"_L1, QJsonObject{{"device"_L1, "*"_L1},
                                                 {"field"_L1, "power"_L1},
                                                 {"op"_L1, "is"_L1},
                                                 {"value"_L1, "on"_L1}}}},
             QJsonObject{{"not"_L1, QJsonObject{{"leaf"_L1,
                 QJsonObject{{"field"_L1, "muted"_L1}, {"op"_L1, "is"},
                             {"value"_L1, true}}}}}}}}}},
        {"actions"_L1, QJsonArray{
            QJsonObject{{"type"_L1, "setVolume"_L1}, {"volume"_L1, 15}},
            QJsonObject{{"type"_L1, "delay"_L1}, {"seconds"_L1, 30}},
            QJsonObject{{"type"_L1, "ifThenElse"_L1},
                        {"cond"_L1, QJsonObject{{"any"_L1, QJsonArray{
                            QJsonObject{{"leaf"_L1, QJsonObject{{"field"_L1, "app"_L1},
                                                                {"op"_L1, "isNot"_L1},
                                                                {"value"_L1, "tv.netflix"}}}}}}}},
                        {"then"_L1, QJsonArray{QJsonObject{
                            {"type"_L1, "keypress"_L1}, {"key"_L1, "Home"}}}},
                        {"else"_L1, QJsonArray{QJsonObject{
                            {"type"_L1, "runRule"_L1}, {"id"_L1, "rXYZ"}}}}}}}};
}

void RuleModelTest::parsesAndSerializesACompleteRule() {
    const QJsonObject source = completeRuleJson();
    const auto parsed = parseRule(source);
    QVERIFY2(parsed.has_value(), "complete fixture must parse");
    QCOMPARE(parsed->name, "Night volume"_L1);
    QCOMPARE(parsed->mode, "queued"_L1);
    QCOMPARE(parsed->devices, QStringList{"X001"_L1});
    QCOMPARE(parsed->trigger.type, "time"_L1);
    QCOMPARE(parsed->trigger.timeKind, "weekdays"_L1);
    QCOMPARE(parsed->trigger.time, "23:00"_L1);
    QVERIFY(parsed->conditions.has_value());
    QCOMPARE(parsed->conditions->kind, ConditionNode::Kind::All);
    QCOMPARE(parsed->actions.size(), 3);
    QCOMPARE(parsed->actions.at(0).volume, 15);
    QCOMPARE(parsed->actions.at(2).elseActions.size(), 1);

    // Round trip: serializing the parsed document must reproduce the input.
    const QJsonObject serialized = ruleJson(*parsed);
    const auto reparsed = parseRule(serialized);
    QVERIFY(reparsed.has_value());
    QCOMPARE(QJsonDocument(ruleJson(*reparsed)),
             QJsonDocument(serialized));
}

QTEST_MAIN(RuleModelTest)
#include "rule_model_test.moc"
```

- [ ] **Step 2: Register the test target**

In `tests/CMakeLists.txt`, immediately after the `device-parsers-test` block, add:

```cmake
add_executable(rule-model-test unit/rule_model_test.cpp)
# Match the include/define block shape of device-parsers-test above it.
target_link_libraries(rule-model-test PRIVATE RokuDeck::Core Qt6::Core Qt6::Test)
add_test(NAME rule-model COMMAND rule-model-test)
```

- [ ] **Step 3: Run to verify failure**

Run: `cmake --build build/dev --target rule-model-test 2>&1 | tail -5`
Expected: FAIL — `core/rule_model.h` does not exist.

- [ ] **Step 4: Implement the header and parser**

Create `src/core/rule_model.h` exactly as the Interfaces block above (plus `#pragma once`, SPDX header, includes for `QJsonValue/QList/QString/QStringList/optional`).

Implement `src/core/rule_model.cpp`. Key bodies (strict-key discipline mirrors `validateCommand`):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rule_model.h"

#include <QJsonArray>

namespace RokuDeck {
namespace {

constexpr int maximumDepth = 16;

bool exactKeys(const QJsonObject& object, const QStringList& expected) {
    const QList<QString> actual = object.keys();
    QSet<QString> actualSet(actual.cbegin(), actual.cend());
    QSet<QString> expectedSet(expected.cbegin(), expected.cend());
    return actualSet == expectedSet;
}

ParseResult<FieldPredicate> parsePredicate(const QJsonObject& object, int depth) {
    static const QStringList allowedFields{QStringLiteral("power"),
        QStringLiteral("screensaver"), QStringLiteral("app"),
        QStringLiteral("muted"), QStringLiteral("volume"), QStringLiteral("health")};
    static const QStringList allowedOps{QStringLiteral("is"), QStringLiteral("isNot"),
        QStringLiteral("gt"), QStringLiteral("lt"), QStringLiteral("between")};
    if (!exactKeys(object, {QStringLiteral("device"), QStringLiteral("field"),
                            QStringLiteral("op"), QStringLiteral("value")})) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("predicate keys mismatch")}};
    }
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("condition depth exceeds 16")}};
    }
    FieldPredicate predicate;
    predicate.device = object.value(QStringLiteral("device")).toString(QStringLiteral("*"));
    predicate.field = object.value(QStringLiteral("field")).toString();
    predicate.op = object.value(QStringLiteral("op")).toString();
    if (!allowedFields.contains(predicate.field)) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("unknown field %1").arg(predicate.field)}};
    }
    if (!allowedOps.contains(predicate.op)) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("unknown op %1").arg(predicate.op)}};
    }
    if (predicate.op == QStringLiteral("between")) {
        const QJsonValue value = object.value(QStringLiteral("value"));
        if (!value.isArray() || value.toArray().size() != 2) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("between needs [low,high]")}};
        }
    }
    predicate.value = object.value(QStringLiteral("value"));
    return {std::move(predicate)};
}

ParseResult<ConditionNode> parseCondition(const QJsonObject& object, int depth);

} // namespace
} // namespace RokuDeck
```

`parseCondition` (defined outside the anonymous namespace opening above — keep all helpers in the same anonymous namespace) dispatches on exactly one of `all`/`any`/`not`/`leaf` being present:

```cpp
// inside the anonymous namespace
ParseResult<ConditionNode> parseCondition(const QJsonObject& object, int depth) {
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("condition depth exceeds 16")}};
    }
    int present = 0;
    for (const char* key : {"all", "any", "not", "leaf"}) {
        if (object.contains(QLatin1String(key))) {
            ++present;
        }
    }
    if (present != 1) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("condition needs exactly one of all/any/not/leaf")}};
    }
    if (object.contains(QStringLiteral("leaf"))) {
        ConditionNode node;
        node.kind = ConditionNode::Kind::Leaf;
        const auto leaf = parsePredicate(object.value(QStringLiteral("leaf")).toObject(), depth);
        if (!leaf) {
            return leaf;
        }
        node.leaf = *leaf;
        return {std::move(node)};
    }
    const bool isNot = object.contains(QStringLiteral("not"));
    ConditionNode node;
    node.kind = isNot ? ConditionNode::Kind::Not
                      : (object.contains(QStringLiteral("all")) ? ConditionNode::Kind::All
                                                               : ConditionNode::Kind::Any);
    const QString arrayKey = isNot ? QStringLiteral("not")
                                   : (node.kind == ConditionNode::Kind::All
                                          ? QStringLiteral("all")
                                          : QStringLiteral("any"));
    const auto child = parseCondition(object.value(arrayKey).toObject(), depth + 1);
    if (!child) {
        return child;
    }
    if (isNot) {
        node.child = *child;
    } else {
        const QJsonArray entries = object.value(arrayKey).toArray();
        // Re-parse children from the array form.
        node.children.clear();
        for (const auto& entry : entries) {
            const auto parsed = parseCondition(entry.toObject(), depth + 1);
            if (!parsed) {
                return parsed;
            }
            node.children.append(*parsed);
        }
        return {std::move(node)};
    }
    return {std::move(node)};
}
```

Note: `all`/`any` hold arrays, `not` holds an object — the branch above re-parses array entries; make sure the single-object `not` path stores into `child`.

Trigger parsing (same namespace):

```cpp
ParseResult<TriggerSpec> parseTrigger(const QJsonObject& object) {
    static const QStringList allowedTypes{QStringLiteral("time"),
        QStringLiteral("interval"), QStringLiteral("stateChange"),
        QStringLiteral("manual")};
    const QString type = object.value(QStringLiteral("type")).toString();
    if (!allowedTypes.contains(type)) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("unknown trigger type %1").arg(type)}};
    }
    TriggerSpec trigger;
    trigger.type = type;
    if (type == QStringLiteral("manual")) {
        if (!exactKeys(object, {QStringLiteral("type")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("manual trigger takes no options")}};
        }
        return {std::move(trigger)};
    }
    if (type == QStringLiteral("time")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("kind"),
                                QStringLiteral("time"), QStringLiteral("date")})
            && !exactKeys(object, {QStringLiteral("type"), QStringLiteral("kind"),
                                   QStringLiteral("time")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("time trigger keys mismatch")}};
        }
        trigger.timeKind = object.value(QStringLiteral("kind")).toString();
        trigger.time = object.value(QStringLiteral("time")).toString();
        trigger.date = object.value(QStringLiteral("date")).toString();
        static const QStringList kinds{QStringLiteral("daily"),
            QStringLiteral("weekdays"), QStringLiteral("once")};
        if (!kinds.contains(trigger.timeKind)) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("unknown time kind %1").arg(trigger.timeKind)}};
        }
        const QTime parsedTime = QTime::fromString(trigger.time, QStringLiteral("HH:mm"));
        if (!parsedTime.isValid()) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("time must be HH:MM")}};
        }
        if (trigger.timeKind == QStringLiteral("once")) {
            if (QDate::fromString(trigger.date, QStringLiteral("yyyy-MM-dd")).isNull()) {
                return {ParseError{ParseError::Code::InvalidValue,
                                   QStringLiteral("once needs date YYYY-MM-DD")}};
            }
        } else if (!trigger.date.isEmpty()) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("date only valid for once")}};
        }
        return {std::move(trigger)};
    }
    if (type == QStringLiteral("interval")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("minutes")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("interval keys mismatch")}};
        }
        trigger.minutes = object.value(QStringLiteral("minutes")).toInt(0);
        if (trigger.minutes < 5 || trigger.minutes > 24 * 60) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("interval minutes must be 5..1440")}};
        }
        return {std::move(trigger)};
    }
    // stateChange
    if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("field"),
                            QStringLiteral("op"), QStringLiteral("value")})) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("stateChange keys mismatch")}};
    }
    const auto predicate =
        parsePredicate(QJsonObject{{"device", QStringLiteral("*")},
                                   {"field", object.value(QStringLiteral("field"))},
                                   {"op", object.value(QStringLiteral("op"))},
                                   {"value", object.value(QStringLiteral("value"))}},
                       0);
    if (!predicate) {
        return predicate;
    }
    trigger.stateChange = *predicate;
    return {std::move(trigger)};
}
```

Action parsing:

```cpp
ParseResult<ActionNode> parseAction(const QJsonObject& object, int depth) {
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("action depth exceeds 16")}};
    }
    static const QStringList allowedTypes{QStringLiteral("keypress"),
        QStringLiteral("launchApp"), QStringLiteral("setVolume"),
        QStringLiteral("mute"), QStringLiteral("delay"), QStringLiteral("runRule"),
        QStringLiteral("ifThenElse")};
    ActionNode action;
    action.type = object.value(QStringLiteral("type")).toString();
    if (!allowedTypes.contains(action.type)) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("unknown action type %1").arg(action.type)}};
    }
    if (action.type == QStringLiteral("keypress")) {
        action.key = object.value(QStringLiteral("key")).toString();
        if (action.key.isEmpty() || action.key.size() > 32) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("keypress key invalid")}};
        }
        return {std::move(action)};
    }
    if (action.type == QStringLiteral("launchApp")) {
        action.appId = object.value(QStringLiteral("appId")).toString();
        static const QRegularExpression safeAppId(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
        if (!safeAppId.match(action.appId).hasMatch()) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("appId invalid")}};
        }
        return {std::move(action)};
    }
    if (action.type == QStringLiteral("setVolume")) {
        action.volume = object.value(QStringLiteral("volume")).toInt(-1);
        if (action.volume < 0 || action.volume > 100) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("volume must be 0..100")}};
        }
        return {std::move(action)};
    }
    if (action.type == QStringLiteral("mute")) {
        const QJsonValue value = object.value(QStringLiteral("muted"));
        if ((value.toBool(true) && !value.isBool())
            || (value.isBool() && false)) { // handled below
        }
        if (value.isBool()) {
            action.mutedFlag = value.toBool() ? 1 : 0;
            return {std::move(action)};
        }
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("mute needs boolean muted")}};
    }
    if (action.type == QStringLiteral("delay")) {
        action.delaySeconds = object.value(QStringLiteral("seconds")).toInt(-1);
        if (action.delaySeconds < 1 || action.delaySeconds > 600) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("delay seconds must be 1..600")}};
        }
        return {std::move(action)};
    }
    if (action.type == QStringLiteral("runRule")) {
        action.ruleId = object.value(QStringLiteral("id")).toString();
        if (action.ruleId.isEmpty() || action.ruleId.size() > 64) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("runRule id invalid")}};
        }
        return {std::move(action)};
    }
    // ifThenElse
    if (!object.contains(QStringLiteral("cond"))
        || !object.contains(QStringLiteral("then"))) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("ifThenElse needs cond and then")}};
    }
    const auto condition =
        parseCondition(object.value(QStringLiteral("cond")).toObject(), depth + 1);
    if (!condition) {
        return condition;
    }
    action.condition = *condition;
    const QJsonArray thenArray = object.value(QStringLiteral("then")).toArray();
    for (const auto& entry : thenArray) {
        const auto parsed = parseAction(entry.toObject(), depth + 1);
        if (!parsed) {
            return parsed;
        }
        action.thenActions.append(*parsed);
    }
    const QJsonArray elseArray = object.value(QStringLiteral("else")).toArray();
    for (const auto& entry : elseArray) {
        const auto parsed = parseAction(entry.toObject(), depth + 1);
        if (!parsed) {
            return parsed;
        }
        action.elseActions.append(*parsed);
    }
    return {std::move(action)};
}
```

Public API at the bottom of the file (outside anonymous namespace):

```cpp
ParseResult<RuleDocument> parseRule(const QJsonObject& object) {
    static const QStringList modes{QStringLiteral("single"), QStringLiteral("restart"),
                                   QStringLiteral("queued")};
    if (!exactKeys(object, {QStringLiteral("schemaVersion"), QStringLiteral("id"),
                            QStringLiteral("name"), QStringLiteral("enabled"),
                            QStringLiteral("mode"), QStringLiteral("devices"),
                            QStringLiteral("trigger"), QStringLiteral("actions"),
                            QStringLiteral("conditions"), QStringLiteral("lastRun")})
        && !exactKeys(object, {QStringLiteral("schemaVersion"), QStringLiteral("id"),
                               QStringLiteral("name"), QStringLiteral("enabled"),
                               QStringLiteral("mode"), QStringLiteral("devices"),
                               QStringLiteral("trigger"), QStringLiteral("actions")})
        && !exactKeys(object, {QStringLiteral("schemaVersion"), QStringLiteral("name"),
                               QStringLiteral("trigger"), QStringLiteral("actions")})
        && !exactKeys(object, {QStringLiteral("schemaVersion"), QStringLiteral("name"),
                               QStringLiteral("enabled"), QStringLiteral("mode"),
                               QStringLiteral("devices"), QStringLiteral("trigger"),
                               QStringLiteral("actions")})) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("rule keys mismatch")}};
    }
    RuleDocument rule;
    rule.name = object.value(QStringLiteral("name")).toString();
    if (rule.name.isEmpty() || rule.name.size() > 80) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("rule name must be 1..80 chars")}};
    }
    rule.id = object.value(QStringLiteral("id")).toString();
    if (!rule.id.isEmpty() && (rule.id.size() > 64 || !rule.id.contains(QRegularExpression(
                                          QStringLiteral("^[A-Za-z0-9_-]+$"))))) {
        return {ParseError{ParseError::Code::InvalidValue, QStringLiteral("rule id invalid")}};
    }
    rule.mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("queued"));
    if (!modes.contains(rule.mode)) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("unknown mode %1").arg(rule.mode)}};
    }
    const QJsonValue enabled = object.value(QStringLiteral("enabled"));
    rule.enabled = enabled.isUndefined() ? true : enabled.toBool();
    const QJsonArray devices = object.value(QStringLiteral("devices")).toArray();
    for (const auto& entry : devices) {
        if (!entry.isString() || entry.toString().isEmpty()) {
            return {ParseError{ParseError::Code::InvalidValue,
                               QStringLiteral("devices must be non-empty strings")}};
        }
        rule.devices.append(entry.toString());
    }
    const auto trigger = parseTrigger(object.value(QStringLiteral("trigger")).toObject());
    if (!trigger) {
        return trigger;
    }
    rule.trigger = *trigger;
    if (object.contains(QStringLiteral("conditions"))
        && !object.value(QStringLiteral("conditions")).isNull()
        && !object.value(QStringLiteral("conditions")).isUndefined()
        && object.value(QStringLiteral("conditions")).toObject().size() > 0) {
        const auto conditions =
            parseCondition(object.value(QStringLiteral("conditions")).toObject(), 0);
        if (!conditions) {
            return conditions;
        }
        rule.conditions = *conditions;
    }
    const QJsonArray actions = object.value(QStringLiteral("actions")).toArray();
    if (actions.isEmpty() || actions.size() > 32) {
        return {ParseError{ParseError::Code::InvalidValue,
                           QStringLiteral("rule needs 1..32 actions")}};
    }
    for (const auto& entry : actions) {
        const auto parsed = parseAction(entry.toObject(), 0);
        if (!parsed) {
            return parsed;
        }
        rule.actions.append(*parsed);
    }
    if (object.contains(QStringLiteral("lastRun"))) {
        const QJsonObject lastRun = object.value(QStringLiteral("lastRun")).toObject();
        rule.lastRun.firedAtIso = lastRun.value(QStringLiteral("lastFiredAt")).toString();
        rule.lastRun.outcome = lastRun.value(QStringLiteral("outcome")).toString();
    }
    return {std::move(rule)};
}

QJsonObject ruleJson(const RuleDocument& rule) {
    // Serialize structs back to JSON symmetric with parseRule; conditions and
    // lastRun omitted when empty, devices omitted when empty (== "*").
}
```

`ruleJson` must serialize every field the parser accepts, recursively, so the round-trip
test passes bit-for-bit on `QJsonDocument` comparison (array order preserved).

Add `#include <QRegularExpression>` where needed. Remove the dead `mute` branch stub
comment — final logic: accept only booleans.

- [ ] **Step 5: Build and run to green**

Run: `cmake --build build/dev && cd build/dev && ctest -R rule-model --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/rule_model.h src/core/rule_model.cpp src/core/CMakeLists.txt \
        tests/unit/rule_model_test.cpp tests/CMakeLists.txt
git commit -m "feat(core): typed automation rule model with strict parsing"
```

---

### Task 2: Rule validation rejection matrix

**Files:**
- Test: Modify `tests/unit/rule_model_test.cpp`

**Interfaces:**
- Consumes/Produces: same as Task 1 (this task hardens `parseRule`).

- [ ] **Step 1: Add the data-driven rejection test**

```cpp
void rejectsMalformedRules_data() {
    QTest::addColumn<QJsonObject>("document");
    QTest::addColumn<QString>("expectSubstring");
    auto base = [] {
        QJsonObject o{{"name"_L1, "x"_L1},
                      {"trigger"_L1, QJsonObject{{"type"_L1, "manual"_L1}}},
                      {"actions"_L1, QJsonArray{QJsonObject{
                          {"type"_L1, "keypress"_L1}, {"key"_L1, "Home"_L1}}}}};
        return o;
    };
    auto mutated = [&](std::function<void(QJsonObject&)> mutate) {
        QJsonObject o = base();
        mutate(o);
        return o;
    };
    QTest::newRow("empty-name") << mutated([](QJsonObject& o) { o.insert("name"_L1, ""_L1); }) << "rule name";
    QTest::newRow("unknown-trigger") << mutated([](QJsonObject& o) {
        o.insert("trigger"_L1, QJsonObject{{"type"_L1, "telepathy"_L1}});
    }) << "unknown trigger type";
    QTest::newRow("bad-time-format") << mutated([](QJsonObject& o) {
        o.insert("trigger"_L1, QJsonObject{{"type"_L1, "time"_L1},
                                           {"kind"_L1, "daily"_L1},
                                           {"time"_L1, "25:99"_L1}});
    }) << "HH:MM";
    QTest::newRow("interval-too-small") << mutated([](QJsonObject& o) {
        o.insert("trigger"_L1, QJsonObject{{"type"_L1, "interval"_L1},
                                           {"minutes"_L1, 1}});
    }) << "5..1440";
    QTest::newRow("unknown-field") << mutated([](QJsonObject& o) {
        o.insert("trigger"_L1, QJsonObject{{"type"_L1, "stateChange"_L1},
                                           {"field"_L1, "smell"_L1},
                                           {"op"_L1, "is"_L1},
                                           {"value"_L1, "on"_L1}});
    }) << "unknown field";
    QTest::newRow("between-not-pair") << mutated([](QJsonObject& o) {
        o.insert("trigger"_L1, QJsonObject{{"type"_L1, "stateChange"_L1},
                                           {"field"_L1, "volume"_L1},
                                           {"op"_L1, "between"_L1},
                                           {"value"_L1, QJsonArray{20}}});
    }) << "[low,high]";
    QTest::newRow("unknown-action") << mutated([](QJsonObject& o) {
        o.insert("actions"_L1, QJsonArray{QJsonObject{{"type"_L1, "teleport"_L1}}});
    }) << "unknown action type";
    QTest::newRow("volume-out-of-range") << mutated([](QJsonObject& o) {
        o.insert("actions"_L1, QJsonArray{QJsonObject{{"type"_L1, "setVolume"_L1},
                                                      {"volume"_L1, 140}}});
    }) << "0..100";
    QTest::newRow("delay-out-of-range") << mutated([](QJsonObject& o) {
        o.insert("actions"_L1, QJsonArray{QJsonObject{{"type"_L1, "delay"_L1},
                                                      {"seconds"_L1, 601}}});
    }) << "1..600";
    QTest::newRow("extra-keys") << mutated([](QJsonObject& o) { o.insert("wat"_L1, 1); })
                                << "keys mismatch";
    QTest::newRow("no-actions") << mutated([](QJsonObject& o) {
        o.insert("actions"_L1, QJsonArray{});
    }) << "1..32 actions";
    QTest::newRow("bad-mode") << mutated([](QJsonObject& o) { o.insert("mode"_L1, "chaos"_L1); })
                              << "unknown mode";
}

void rejectsMalformedRules() {
    QFETCH(QJsonObject, document);
    QFETCH(QString, expectSubstring);
    const auto parsed = parseRule(document);
    QVERIFY(!parsed.has_value());
    QVERIFY2(parsed.error().message.contains(expectSubstring.toUtf8()),
             qPrintable(parsed.error().message));
}
```

Also append a **depth-cap** test building 17 nested `not` wrappers around a leaf and expecting
failure mentioning `exceeds 16`.

- [ ] **Step 2: Run to green**

Run: `cd build/dev && ctest -R rule-model --output-on-failure`
Expected: PASS (Task 1's implementation already covers these; any miss is fixed here).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/rule_model_test.cpp
git commit -m "test(core): pin automation rule rejection matrix"
```

---

### Task 3: ConfigStore rules persistence

**Files:**
- Modify: `src/daemon/config_store.h`
- Modify: `src/daemon/config_store.cpp`
- Test: Modify `tests/unit/device_registry_test.cpp` **only if** that file hosts config-store
  coverage; otherwise Create `tests/unit/config_store_test.cpp` and register it in
  `tests/CMakeLists.txt` mirroring `device-registry-test` (links `RokuDeck::Daemon`).

**Interfaces:**
- Consumes: `RuleDocument`/`parseRule`/`ruleJson` from Task 1.
- Produces (Tasks 4–6):
  ```cpp
  // ConfigStore additions:
  [[nodiscard]] QList<RuleDocument> loadRules() const;
  void saveRules(const QList<RuleDocument>& rules);   // whole-group rewrite, order kept
  ```

- [ ] **Step 1: Write the failing persistence test**

```cpp
void ConfigStoreTest::rulesRoundTripPreserveOrder() {
    QTemporaryDir dir;
    auto config = KSharedConfig::openConfig(dir.path() + "/store.rc"_L1,
                                            KConfig::SimpleConfig);
    ConfigStore store(config);
    RuleDocument first;
    first.id = "rB"_L1;
    first.name = "Second saved"_L1;
    first.trigger.type = "manual"_L1;
    ActionNode keypress;
    keypress.type = "keypress"_L1;
    keypress.key = "Back"_L1;
    first.actions.append(keypress);
    RuleDocument second;
    second.id = "rA"_L1;
    second.name = "First saved"_L1;
    second.enabled = false;
    second.trigger.type = "manual"_L1;
    second.actions.append(keypress);

    store.saveRules({first, second});

    ConfigStore reload(config);
    const auto loaded = reload.loadRules();
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).id, "rB"_L1);
    QCOMPARE(loaded.at(0).name, "Second saved"_L1);
    QCOMPARE(loaded.at(1).enabled, false);
    QVERIFY(loaded.at(1).actions.size() == 1);
}
```

- [ ] **Step 2: Verify RED** — build fails: `loadRules` not declared.

- [ ] **Step 3: Implement**

In `config_store.cpp`, mirror the devices group pattern under `"Rules"`: each rule gets
subgroup `<index>`, writing `Name`, `Enabled`, `Mode`, `Definition` (the full
`ruleJson(rule)` compact string). Loading reads groups in `groupList()` order, parses each
`Definition` with `parseRule`, applies stored `Name`/`Enabled` overrides, and skips rules
that fail to parse (a corrupt entry must not hide the rest). `saveRules` deletes the
`"Rules"` group then rewrites, exactly like `saveDevices`.

- [ ] **Step 4: Green + commit**

Run: `cd build/dev && ctest -R config-store --output-on-failure` (or the registered name).
Commit: `feat(daemon): persist automation rules in the config store`.

---

### Task 4: DaemonService — ListRules/AddRule + RulesChanged

**Files:**
- Modify: `src/daemon/daemon_service.h`
- Modify: `src/daemon/daemon_service.cpp`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Interfaces:**
- Consumes: Task 1 model, Task 3 store. Existing `sendErrorReply(QDBusError::InvalidArgs, message)`.
- Produces:
  ```cpp
  // public slots:
  [[nodiscard]] QString ListRules() const;
  [[nodiscard]] QString AddRule(const QString& ruleJson);
  // signal:
  void RulesChanged(const QString& rulesJson);
  // private members/helpers:
  QList<RuleDocument> m_rules;
  void loadPersistedRules();                    // ctor
  [[nodiscard]] QString rulesJson() const;      // {"schemaVersion":1,"rules":[…]}
  [[nodiscard]] QString nextRuleId() const;     // "r" + QUuid createUuid base32-ish trimmed to 12
  void announceRules();                         // persist + emit RulesChanged
  ```

- [ ] **Step 1: Failing integration test**

Following the existing harness style at the top of `daemon_dbus_test.cpp` (temp
`KSharedConfig`, unique bus name, `registerOnBus`, `QDBusInterface` against
`io.github.josephur.RokuDeck1`):

```cpp
void DaemonDbusTest::addRuleAssignsIdPersistsAndAnnounces() {
    QSignalSpy changed(rulesInterface.data(), &OrgJosephurRokuDeck1Interface::RulesChanged);
    // Or QSignalSpy on QDBusInterface connection-level delivery; match harness style.
    const QString input = QJsonDocument(QJsonObject{
        {"schemaVersion"_L1, 1},
        {"name"_L1, "Movie time"_L1},
        {"trigger"_L1, QJsonObject{{"type"_L1, "manual"_L1}}},
        {"actions"_L1, QJsonArray{QJsonObject{{"type"_L1, "keypress"_L1},
                                              {"key"_L1, "Select"_L1}}}}}).toJson();
    const QString created = m_rules->AddRule(input);
    const QJsonObject createdObject =
        QJsonDocument::fromJson(created.toUtf8()).object();
    QVERIFY(createdObject.value("id"_L1).toString().startsWith('r'_L1));
    compare(createdObject.value("enabled"_L1), true);

    const QString listing = m_rules->ListRules();
    QVERIFY(listing.contains("Movie time"_L1));

    QCOMPARE(changed.count(), 1);

    // Restart the service against the same config file: the rule survives.
    restartServiceWithSameConfig();
    QVERIFY(m_rules->ListRules().contains("Movie time"_L1));
}
```

Adapt names to whatever the harness already uses for its service/interface handles
(`m_rules` = the harness's `QDBusInterface*`/generated-interface pointer to the service;
`restartServiceWithSameConfig()` = destroy + recreate `DaemonService` with the same
`KSharedConfig::Ptr` — the harness already has an equivalent for device persistence tests
to copy).

- [ ] **Step 2: Verify RED** — `AddRule` unknown method / compile failure.

- [ ] **Step 3: Implement**

- Constructor calls `loadPersistedRules()` (store `loadRules()` → `m_rules`).
- `AddRule`: parse with `parseRule` after injecting `schemaVersion`; reject if input contains
  `id`/`enabled` (spec §AddRule contract) with `sendErrorReply(QDBusError::InvalidArgs,
  QStringLiteral("invalidRule"))`; assign `nextRuleId()` (regenerate while colliding),
  `enabled=true`, append to `m_rules`, `announceRules()`, return `ruleJson(created)`.
- `ListRules` returns `rulesJson()`.
- `announceRules()` = `m_configStore->saveRules(m_rules)` + emit `RulesChanged(rulesJson())`.

- [ ] **Step 4: Green + commit**

Run: `cd build/dev && ctest -R daemon-dbus --output-on-failure`.
Commit: `feat(daemon): rule CRUD entry point with RulesChanged announcements`.

---

### Task 5: UpdateRule / DeleteRule / SetRuleEnabled + UnknownRule

**Files:** Same as Task 4.

**Interfaces:** Adds public slots `UpdateRule(ruleJson)`, `DeleteRule(ruleId)`,
`SetRuleEnabled(ruleId, enabled)`; error `UnknownRule` → `sendErrorReply(
QDBusError::InvalidArgs, QStringLiteral("unknownRule"))`.

- [ ] **Step 1: Failing tests** — three harness cases: update renames + persists across
  service recreation; delete removes and subsequent `UpdateRule` of the deleted id errors;
  `SetRuleEnabled` flips without touching definition (verify via `ListRules` JSON).
- [ ] **Step 2: RED run.**
- [ ] **Step 3: Implement** — find by id in `m_rules`; `UpdateRule` replaces definition in
  place preserving insertion position; all mutations end with `announceRules()`.
- [ ] **Step 4: Green + commit** — `feat(daemon): complete rule mutation API`.

---

### Task 6: RunRule — manual execution with consent and last-run record

**Files:**
- Modify: `src/daemon/daemon_service.{h,cpp}`
- Test: Modify `tests/integration/daemon_dbus_test.cpp`

**Interfaces:**
- Consumes: `SendCommand` internals — reuse `validateCommand(serial, commandJson)` +
  `processNextCommand(serial)` exactly as `SendCommand` does, so every safety gate
  (audio-key volume gating, FindRemote capability) applies to automation dispatch.
- Produces:
  ```cpp
  // public slot:
  QString RunRule(const QString& ruleId);   // returns outcome string
  // private:
  struct ActionResult { bool accepted; QString reason; };
  ActionResult executeActions(const RuleDocument& rule, const QStringList& targets);
  ```

- [ ] **Step 1: Failing tests**

```cpp
// a) consent-blocked: device exists with allowAutomations=false -> outcome recorded
// b) happy path: manual rule keypress Home -> fake runner saw the rds keypress args
// c) unsupported action: setVolume action -> outcome failed, reason unsupportedAction
// d) unknown rule id -> error reply contains unknownRule
// Each asserts via ListRules() that lastRun.outcome matches expectation.
```

Write these four cases following Task 4's harness patterns; drive the fake process runner
the way existing SendCommand tests do.

- [ ] **Step 2: RED run.**

- [ ] **Step 3: Implement**

```cpp
QString DaemonService::RunRule(const QString& ruleId) {
    const auto it = std::find_if(m_rules.cbegin(), m_rules.cend(),
        [&](const RuleDocument& r) { return r.id == ruleId; });
    if (it == m_rules.cend()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknownRule"));
        return {};
    }
    RuleDocument rule = *it;
    const QString outcome = executeManualRule(rule);
    rule.lastRun.firedAtIso =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    rule.lastRun.outcome = outcome;
    replaceRule(rule);
    announceRules();
    return outcome;
}
```

`executeManualRule`: resolve targets (`devices` empty ⇒ every known device serial;
otherwise listed serials that still exist). Drop devices whose `DeviceRecord::
allowAutomations` is false; none left ⇒ `consent-blocked`. Then run actions sequentially:
`keypress`/`launchApp` build the exact `commandJson` shapes `SendCommand` accepts, call the
internal validate+queue path per target (accepted ⇒ continue), `delay` sleeps on
`QEventLoop` + `QTimer` (bounded 1..600s, already validated), and `setVolume`/`mute`/
`runRule`/`ifThenElse` produce outcome `failed` with reason `unsupportedAction` in M1
(spec M3/M5 land them). Any rejected dispatch marks the run `partial` unless all targets
rejected everything ⇒ `failed`.

- [ ] **Step 4: Green + commit** — `feat(daemon): manual rule execution with consent gating`.

---

### Task 7: D-Bus introspection contract

**Files:**
- Modify: `data/dbus/io.github.josephur.RokuDeck1.xml`
- Test: existing contract/public-contract checks must stay green.

- [ ] **Step 1: Extend the XML** with the six methods + `RulesChanged` signal exactly as
  spec §6 (string JSON args, `b` for enabled), plus annotations:

```xml
<annotation name="io.github.josephur.RokuDeck1.Error.InvalidRule" value="exitCode=8"/>
<annotation name="io.github.josephur.RokuDeck1.Error.UnknownRule" value="exitCode=9"/>
```

- [ ] **Step 2:** Rebuild (qdbusxml2cpp regenerates `rokudeck1_interface.h`), fix any
  signature drift the compilers surface.
- [ ] **Step 3:** `cd build/dev && ctest` — full suite green.
- [ ] **Step 4:** Commit `feat(dbus): automation rule contract on RokuDeck1`.

---

### Task 8: CHANGELOG and release hygiene

- [ ] Add bullets under `## Unreleased` describing M1 user-visible behavior (scheduled-rule
  storage/API groundwork, manual macro execution honoring per-device consent).
- [ ] Full suite: `cd build/dev && ctest` — expect all green including new `rule-model`.
- [ ] Deploy smoke: `bash scripts/install-dev.sh`, recycle daemon happens automatically,
  then `busctl --user call io.github.josephur.RokuDeck1 /io/github/josephur/RokuDeck1
  io.github.josephur.RokuDeck1 ListRules` returns `{"schemaVersion":1,"rules":[]}`.
- [ ] Commit `docs(changelog): automations M1 groundwork`.

---

## Self-review notes

- Spec coverage for M1: §3 model (T1–T2), §5 storage (T3), §6 CRUD/signals/errors (T4–T5,
  T7), §4 manual firing subset + consent + last-run (T6), §8 harness patterns (throughout).
  Scheduler/stateChange/ifThenElse/ceiling/UI intentionally deferred to their own plans
  (M2–M6) per spec milestones.
- Type consistency: `RuleDocument` field names identical across T1 interfaces and T3–T6
  usage; `parseRule`/`ruleJson` signatures reused verbatim.
- Known simplification surfaced honestly: M1 executor reports `setVolume`/`mute`/`runRule`/
  `ifThenElse` as `failed:unsupportedAction`; validator already accepts them so later
  milestones extend behavior without schema changes.

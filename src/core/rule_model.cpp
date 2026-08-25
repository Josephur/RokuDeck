// SPDX-License-Identifier: GPL-3.0-or-later
#include "rule_model.h"

#include <QRegularExpression>
#include <QSet>

namespace RokuDeck {
using namespace Qt::Literals::StringLiterals;
namespace {

constexpr int maximumDepth = 16;

[[nodiscard]] bool exactKeys(const QJsonObject& object, const QStringList& expected)
{
    const QList<QString> actual = object.keys();
    const QSet<QString> actualSet(actual.cbegin(), actual.cend());
    const QSet<QString> expectedSet(expected.cbegin(), expected.cend());
    return actualSet == expectedSet;
}

[[nodiscard]] bool isKnownField(const QString& field)
{
    static const QSet<QString> fields{QStringLiteral("power"),
        QStringLiteral("screensaver"), QStringLiteral("app"), QStringLiteral("muted"),
        QStringLiteral("volume"), QStringLiteral("health"), QStringLiteral("playing"),
        QStringLiteral("media")};
    return fields.contains(field);
}

[[nodiscard]] bool isKnownOp(const QString& op)
{
    static const QSet<QString> ops{QStringLiteral("is"), QStringLiteral("isNot"),
        QStringLiteral("gt"), QStringLiteral("lt"), QStringLiteral("between")};
    return ops.contains(op);
}

// The optional activeWindow block shared by every trigger type: exact inner
// keys, HH:MM bounds, and a non-degenerate span (end < start wraps midnight).
[[nodiscard]] ParseResult<std::optional<TimeWindow>> parseActiveWindow(
    const QJsonObject& trigger)
{
    if (!trigger.contains(QStringLiteral("activeWindow"))) {
        return {std::nullopt};
    }
    const QJsonObject window = trigger.value(QStringLiteral("activeWindow")).toObject();
    if (!exactKeys(window,
            {QStringLiteral("start"), QStringLiteral("end")})) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("active window takes start and end")}};
    }
    const QString start = window.value(QStringLiteral("start")).toString();
    const QString end = window.value(QStringLiteral("end")).toString();
    const QTime startTime = QTime::fromString(start, QStringLiteral("HH:mm"));
    const QTime endTime = QTime::fromString(end, QStringLiteral("HH:mm"));
    if (!startTime.isValid() || !endTime.isValid()) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("active window bounds must be HH:MM")}};
    }
    if (startTime == endTime) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("degenerate window")}};
    }
    TimeWindow parsed;
    parsed.start = start;
    parsed.end = end;
    return {std::optional<TimeWindow>{std::move(parsed)}};
}

[[nodiscard]] ParseResult<FieldPredicate> parsePredicate(
    const QJsonObject& object, int depth)
{
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
    if (predicate.device.isEmpty()) {
        predicate.device = QStringLiteral("*");
    }
    predicate.field = object.value(QStringLiteral("field")).toString();
    predicate.op = object.value(QStringLiteral("op")).toString();
    if (!isKnownField(predicate.field)) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("unknown field %1").arg(predicate.field)}};
    }
    if (!isKnownOp(predicate.op)) {
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
    if (predicate.field == QStringLiteral("playing")
        && !object.value(QStringLiteral("value")).isBool()) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("playing needs boolean value")}};
    }
    static const QStringList mediaValues{QStringLiteral("playing"),
        QStringLiteral("paused"), QStringLiteral("stopped"), QStringLiteral("idle")};
    if (predicate.field == QStringLiteral("media")
        && (!object.value(QStringLiteral("value")).isString()
            || !mediaValues.contains(
                   object.value(QStringLiteral("value")).toString()))) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("media needs playing|paused|stopped|idle")}};
    }
    predicate.value = object.value(QStringLiteral("value"));
    return {std::move(predicate)};
}

[[nodiscard]] ParseResult<ConditionNode> parseCondition(
    const QJsonObject& object, int depth)
{
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("condition depth exceeds 16")}};
    }
    int present = 0;
    for (const auto& key : {u"all"_s, u"any"_s, u"not"_s, u"leaf"_s}) {
        if (object.contains(key)) {
            ++present;
        }
    }
    if (present != 1) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral(
                "condition needs exactly one of all/any/not/leaf")}};
    }

    ConditionNode node;
    if (object.contains(u"leaf"_s)) {
        node.kind = ConditionNode::Kind::Leaf;
        auto leaf = parsePredicate(object.value(u"leaf"_s).toObject(), depth + 1);
        if (!leaf) {
            return {ParseError{leaf.error()}};
        }
        node.leaf = std::move(leaf.value());
        return {std::move(node)};
    }

    const bool isNot = object.contains(u"not"_s);
    const bool isAll = object.contains(u"all"_s);
    node.kind = isNot ? ConditionNode::Kind::Not
                      : (isAll ? ConditionNode::Kind::All : ConditionNode::Kind::Any);
    const auto childKey = isNot ? u"not"_s : (isAll ? u"all"_s : u"any"_s);

    if (isNot) {
        auto child = parseCondition(object.value(childKey).toObject(), depth + 1);
        if (!child) {
            return {ParseError{child.error()}};
        }
        node.notChild.append(std::move(child.value()));
        return {std::move(node)};
    }

    const QJsonArray entries = object.value(childKey).toArray();
    for (const auto& entry : entries) {
        auto parsed = parseCondition(entry.toObject(), depth + 1);
        if (!parsed) {
            return parsed;
        }
        node.children.append(std::move(parsed.value()));
    }
    return {std::move(node)};
}

[[nodiscard]] ParseResult<QJsonObject> conditionJson(const ConditionNode& node, int depth)
{
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("condition depth exceeds 16")}};
    }
    QJsonObject object;
    switch (node.kind) {
    case ConditionNode::Kind::Leaf: {
        QJsonObject leaf{{QStringLiteral("device"), node.leaf.device},
            {QStringLiteral("field"), node.leaf.field},
            {QStringLiteral("op"), node.leaf.op},
            {QStringLiteral("value"), node.leaf.value}};
        object.insert(QStringLiteral("leaf"), leaf);
        break;
    }
    case ConditionNode::Kind::Not: {
        if (node.notChild.isEmpty()) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("not node without child")}};
        }
        auto child = conditionJson(node.notChild.first(), depth + 1);
        if (!child) {
            return {ParseError{child.error()}};
        }
        object.insert(QStringLiteral("not"), child.value());
        break;
    }
    case ConditionNode::Kind::All:
    case ConditionNode::Kind::Any: {
        QJsonArray entries;
        for (const ConditionNode& entry : node.children) {
            auto serialized = conditionJson(entry, depth + 1);
            if (!serialized) {
                return {ParseError{serialized.error()}};
            }
            entries.append(serialized.value());
        }
        object.insert(node.kind == ConditionNode::Kind::All
                          ? QStringLiteral("all")
                          : QStringLiteral("any"),
            entries);
        break;
    }
    }
    return {std::move(object)};
}

[[nodiscard]] ParseResult<TriggerSpec> parseTrigger(const QJsonObject& object)
{
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

    // Every type accepts the optional activeWindow block; validate it up
    // front so each branch only extends its exact-key set.
    auto window = parseActiveWindow(object);
    if (!window) {
        return {ParseError{window.error()}};
    }
    const bool hasWindow = window.value().has_value();

    if (type == QStringLiteral("manual")) {
        if (!exactKeys(object,
                hasWindow ? QStringList{QStringLiteral("type"),
                                             QStringLiteral("activeWindow")}
                          : QStringList{QStringLiteral("type")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("manual trigger takes no options")}};
        }
        trigger.activeWindow = std::move(window.value());
        return {std::move(trigger)};
    }

    if (type == QStringLiteral("time")) {
        const bool hasDate = object.contains(QStringLiteral("date"));
        QStringList expected{QStringLiteral("type"), QStringLiteral("kind"),
            QStringLiteral("time")};
        if (hasDate) {
            expected.append(QStringLiteral("date"));
        }
        if (hasWindow) {
            expected.append(QStringLiteral("activeWindow"));
        }
        if (!exactKeys(object, expected)) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("time trigger keys mismatch")}};
        }
        trigger.timeKind = object.value(QStringLiteral("kind")).toString();
        trigger.time = object.value(QStringLiteral("time")).toString();
        static const QStringList kinds{QStringLiteral("daily"),
            QStringLiteral("weekdays"), QStringLiteral("once")};
        if (!kinds.contains(trigger.timeKind)) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("unknown time kind %1").arg(trigger.timeKind)}};
        }
        if (!QTime::fromString(trigger.time, QStringLiteral("HH:mm")).isValid()) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("time must be HH:MM")}};
        }
        if (trigger.timeKind == QStringLiteral("once")) {
            if (!hasDate
                || QDate::fromString(trigger.date = object.value(
                                         QStringLiteral("date")).toString(),
                        QStringLiteral("yyyy-MM-dd")).isNull()) {
                return {ParseError{ParseError::Code::InvalidValue,
                    QStringLiteral("once needs date YYYY-MM-DD")}};
            }
        } else if (hasDate) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("date only valid for once")}};
        }
        trigger.activeWindow = std::move(window.value());
        return {std::move(trigger)};
    }

    if (type == QStringLiteral("interval")) {
        if (!exactKeys(object,
                hasWindow ? QStringList{QStringLiteral("type"),
                                             QStringLiteral("minutes"),
                                             QStringLiteral("activeWindow")}
                          : QStringList{QStringLiteral("type"),
                                QStringLiteral("minutes")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("interval keys mismatch")}};
        }
        trigger.minutes = object.value(QStringLiteral("minutes")).toInt(0);
        if (trigger.minutes < 5 || trigger.minutes > 24 * 60) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("interval minutes must be 5..1440")}};
        }
        trigger.activeWindow = std::move(window.value());
        return {std::move(trigger)};
    }

    // stateChange: the optional device key scopes the trigger to one serial;
    // absent keeps the wildcard "any device" meaning existing rules rely on.
    const bool hasDevice = object.contains(QStringLiteral("device"));
    QStringList stateChangeKeys{QStringLiteral("type"), QStringLiteral("field"),
        QStringLiteral("op"), QStringLiteral("value")};
    if (hasDevice) {
        stateChangeKeys.insert(1, QStringLiteral("device"));
    }
    if (hasWindow) {
        stateChangeKeys.append(QStringLiteral("activeWindow"));
    }
    if (!exactKeys(object, stateChangeKeys)) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("stateChange keys mismatch")}};
    }
    QString scopedDevice = QStringLiteral("*");
    if (hasDevice) {
        const QJsonValue device = object.value(QStringLiteral("device"));
        if (!device.isString()) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("stateChange device must be a string")}};
        }
        scopedDevice = device.toString();
        if (scopedDevice.isEmpty() || scopedDevice.size() > 64) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("stateChange device invalid")}};
        }
    }
    auto predicate = parsePredicate(
        QJsonObject{{QStringLiteral("device"), scopedDevice},
            {QStringLiteral("field"), object.value(QStringLiteral("field"))},
            {QStringLiteral("op"), object.value(QStringLiteral("op"))},
            {QStringLiteral("value"), object.value(QStringLiteral("value"))}},
        0);
    if (!predicate) {
        return {ParseError{predicate.error()}};
    }
    trigger.stateChange = std::move(predicate.value());
    trigger.activeWindow = std::move(window.value());
    return {std::move(trigger)};
}

[[nodiscard]] ParseResult<ActionNode> parseAction(
    const QJsonObject& object, int depth)
{
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("action depth exceeds 16")}};
    }
    static const QStringList allowedTypes{QStringLiteral("keypress"),
        QStringLiteral("launchApp"), QStringLiteral("setVolume"),
        QStringLiteral("mute"), QStringLiteral("delay"), QStringLiteral("runRule"),
        QStringLiteral("runScript"), QStringLiteral("ifThenElse")};
    ActionNode action;
    action.type = object.value(QStringLiteral("type")).toString();
    if (!allowedTypes.contains(action.type)) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("unknown action type %1").arg(action.type)}};
    }

    if (action.type == QStringLiteral("keypress")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("key")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("keypress keys mismatch")}};
        }
        action.key = object.value(QStringLiteral("key")).toString();
        if (action.key.isEmpty() || action.key.size() > 32) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("keypress key invalid")}};
        }
        return {std::move(action)};
    }

    if (action.type == QStringLiteral("launchApp")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("appId")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("launchApp keys mismatch")}};
        }
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
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("volume")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("setVolume keys mismatch")}};
        }
        action.volume = object.value(QStringLiteral("volume")).toInt(-1);
        if (action.volume < 0 || action.volume > 100) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("volume must be 0..100")}};
        }
        return {std::move(action)};
    }

    if (action.type == QStringLiteral("mute")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("muted")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("mute keys mismatch")}};
        }
        const QJsonValue value = object.value(QStringLiteral("muted"));
        if (!value.isBool()) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("mute needs boolean muted")}};
        }
        action.mutedFlag = value.toBool() ? 1 : 0;
        return {std::move(action)};
    }

    if (action.type == QStringLiteral("delay")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("seconds")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("delay keys mismatch")}};
        }
        action.delaySeconds = object.value(QStringLiteral("seconds")).toInt(-1);
        if (action.delaySeconds < 1 || action.delaySeconds > 600) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("delay seconds must be 1..600")}};
        }
        return {std::move(action)};
    }

    if (action.type == QStringLiteral("runRule")) {
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("id")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("runRule keys mismatch")}};
        }
        action.ruleId = object.value(QStringLiteral("id")).toString();
        if (action.ruleId.isEmpty() || action.ruleId.size() > 64) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("runRule id invalid")}};
        }
        return {std::move(action)};
    }

    if (action.type == QStringLiteral("runScript")) {
        // Rules reference scripts BY NAME: raw command strings never cross
        // D-Bus, and the name charset keeps the file inside one directory.
        if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("script")})) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("runScript keys mismatch")}};
        }
        const QJsonValue script = object.value(QStringLiteral("script"));
        if (!script.isString()) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("runScript name must be a string")}};
        }
        static const QRegularExpression safeScriptName(
            QStringLiteral("^[a-z0-9][a-z0-9_-]{0,63}$"));
        action.script = script.toString();
        if (!safeScriptName.match(action.script).hasMatch()) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("runScript name invalid")}};
        }
        return {std::move(action)};
    }

    // ifThenElse
    if (!exactKeys(object, {QStringLiteral("type"), QStringLiteral("cond"),
                            QStringLiteral("then"), QStringLiteral("else")})
        && !exactKeys(object, {QStringLiteral("type"), QStringLiteral("cond"),
                               QStringLiteral("then")})) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("ifThenElse keys mismatch")}};
    }
    auto condition =
        parseCondition(object.value(QStringLiteral("cond")).toObject(), depth + 1);
    if (!condition) {
        return {ParseError{condition.error()}};
    }
    action.condition = std::move(condition.value());
    const QJsonArray thenArray = object.value(QStringLiteral("then")).toArray();
    for (const auto& entry : thenArray) {
        auto parsed = parseAction(entry.toObject(), depth + 1);
        if (!parsed) {
            return {ParseError{parsed.error()}};
        }
        action.thenActions.append(std::move(parsed.value()));
    }
    if (object.contains(QStringLiteral("else"))) {
        const QJsonArray elseArray = object.value(QStringLiteral("else")).toArray();
        for (const auto& entry : elseArray) {
            auto parsed = parseAction(entry.toObject(), depth + 1);
            if (!parsed) {
                return {ParseError{parsed.error()}};
            }
            action.elseActions.append(std::move(parsed.value()));
        }
    }
    return {std::move(action)};
}

[[nodiscard]] ParseResult<QJsonObject> actionJson(const ActionNode& action, int depth)
{
    if (depth > maximumDepth) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("action depth exceeds 16")}};
    }
    QJsonObject object{{QStringLiteral("type"), action.type}};
    if (action.type == QStringLiteral("keypress")) {
        object.insert(QStringLiteral("key"), action.key);
    } else if (action.type == QStringLiteral("launchApp")) {
        object.insert(QStringLiteral("appId"), action.appId);
    } else if (action.type == QStringLiteral("setVolume")) {
        object.insert(QStringLiteral("volume"), action.volume);
    } else if (action.type == QStringLiteral("mute")) {
        object.insert(QStringLiteral("muted"), action.mutedFlag != 0);
    } else if (action.type == QStringLiteral("delay")) {
        object.insert(QStringLiteral("seconds"), action.delaySeconds);
    } else if (action.type == QStringLiteral("runRule")) {
        object.insert(QStringLiteral("id"), action.ruleId);
    } else if (action.type == QStringLiteral("runScript")) {
        object.insert(QStringLiteral("script"), action.script);
    } else if (action.type == QStringLiteral("ifThenElse")) {
        auto cond = conditionJson(action.condition, depth + 1);
        if (!cond) {
            return {ParseError{cond.error()}};
        }
        QJsonArray thenArray;
        for (const ActionNode& entry : action.thenActions) {
            auto serialized = actionJson(entry, depth + 1);
            if (!serialized) {
                return {ParseError{serialized.error()}};
            }
            thenArray.append(serialized.value());
        }
        object.insert(QStringLiteral("cond"), cond.value());
        object.insert(QStringLiteral("then"), thenArray);
        if (!action.elseActions.isEmpty()) {
            QJsonArray elseArray;
            for (const ActionNode& entry : action.elseActions) {
                auto serialized = actionJson(entry, depth + 1);
                if (!serialized) {
                    return serialized;
                }
                elseArray.append(serialized.value());
            }
            object.insert(QStringLiteral("else"), elseArray);
        }
    }
    return {std::move(object)};
}

} // namespace

ParseResult<RuleDocument> parseRule(const QJsonObject& object)
{
    static const QStringList modes{QStringLiteral("single"), QStringLiteral("restart"),
        QStringLiteral("queued")};
    static const QSet<QString> knownKeys{QStringLiteral("schemaVersion"),
        QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("enabled"),
        QStringLiteral("mode"), QStringLiteral("devices"),
        QStringLiteral("trigger"), QStringLiteral("conditions"),
        QStringLiteral("actions"), QStringLiteral("lastRun")};

    // Strict about unknown keys, permissive about which optional blocks are
    // present: stored documents carry everything, AddRule input may omit the
    // daemon-owned fields and the wildcard device list.
    const QList<QString> presentKeys = object.keys();
    for (const QString& key : presentKeys) {
        if (!knownKeys.contains(key)) {
            return {ParseError{ParseError::Code::InvalidValue,
                QStringLiteral("rule keys mismatch: unknown %1").arg(key)}};
        }
    }
    if (!object.contains(QStringLiteral("name"))
        || !object.contains(QStringLiteral("trigger"))
        || !object.contains(QStringLiteral("actions"))) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("rule keys mismatch: name/trigger/actions required")}};
    }

    const bool hasConditions = object.contains(QStringLiteral("conditions"))
        && !object.value(QStringLiteral("conditions")).isNull();
    const bool hasLastRun = object.contains(QStringLiteral("lastRun"));

    RuleDocument rule;
    rule.name = object.value(QStringLiteral("name")).toString();
    if (rule.name.isEmpty() || rule.name.size() > 80) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("rule name must be 1..80 chars")}};
    }
    rule.id = object.value(QStringLiteral("id")).toString();
    if (!rule.id.isEmpty()
        && !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"))
                .match(rule.id)
                .hasMatch()) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("rule id invalid")}};
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

    auto trigger = parseTrigger(object.value(QStringLiteral("trigger")).toObject());
    if (!trigger) {
        return {ParseError{trigger.error()}};
    }
    rule.trigger = std::move(trigger.value());

    if (hasConditions) {
        auto conditions =
            parseCondition(object.value(QStringLiteral("conditions")).toObject(), 0);
        if (!conditions) {
            return {ParseError{conditions.error()}};
        }
        rule.conditions = std::move(conditions.value());
    }

    const QJsonArray actions = object.value(QStringLiteral("actions")).toArray();
    if (actions.isEmpty() || actions.size() > 32) {
        return {ParseError{ParseError::Code::InvalidValue,
            QStringLiteral("rule needs 1..32 actions")}};
    }
    for (const auto& entry : actions) {
        auto parsed = parseAction(entry.toObject(), 0);
        if (!parsed) {
            return {ParseError{parsed.error()}};
        }
        rule.actions.append(std::move(parsed.value()));
    }

    if (hasLastRun) {
        const QJsonObject lastRun = object.value(QStringLiteral("lastRun")).toObject();
        rule.lastRun.firedAtIso =
            lastRun.value(QStringLiteral("lastFiredAt")).toString();
        rule.lastRun.outcome = lastRun.value(QStringLiteral("outcome")).toString();
    }
    return {std::move(rule)};
}

QJsonObject ruleJson(const RuleDocument& rule)
{
    QJsonObject object{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("name"), rule.name},
        {QStringLiteral("mode"), rule.mode},
        {QStringLiteral("enabled"), rule.enabled}};
    if (!rule.id.isEmpty()) {
        object.insert(QStringLiteral("id"), rule.id);
    }
    QJsonArray devices;
    for (const QString& serial : rule.devices) {
        devices.append(serial);
    }
    object.insert(QStringLiteral("devices"), devices);

    QJsonObject trigger{{QStringLiteral("type"), rule.trigger.type}};
    if (rule.trigger.type == QStringLiteral("time")) {
        trigger.insert(QStringLiteral("kind"), rule.trigger.timeKind);
        trigger.insert(QStringLiteral("time"), rule.trigger.time);
        if (!rule.trigger.date.isEmpty()) {
            trigger.insert(QStringLiteral("date"), rule.trigger.date);
        }
    } else if (rule.trigger.type == QStringLiteral("interval")) {
        trigger.insert(QStringLiteral("minutes"), rule.trigger.minutes);
    } else if (rule.trigger.type == QStringLiteral("stateChange")) {
        if (rule.trigger.stateChange.device != QStringLiteral("*")) {
            trigger.insert(QStringLiteral("device"), rule.trigger.stateChange.device);
        }
        trigger.insert(QStringLiteral("field"), rule.trigger.stateChange.field);
        trigger.insert(QStringLiteral("op"), rule.trigger.stateChange.op);
        trigger.insert(QStringLiteral("value"), rule.trigger.stateChange.value);
    }
    if (rule.trigger.activeWindow.has_value()) {
        trigger.insert(QStringLiteral("activeWindow"),
            QJsonObject{{QStringLiteral("start"), rule.trigger.activeWindow->start},
                {QStringLiteral("end"), rule.trigger.activeWindow->end}});
    }
    object.insert(QStringLiteral("trigger"), trigger);

    // Depth overflows cannot occur here: parseRule enforces the cap before
    // any document reaches the serializer.
    if (rule.conditions.has_value()) {
        auto serialized = conditionJson(*rule.conditions, 0);
        if (!serialized) {
        }
        Q_ASSERT(serialized.has_value());
        object.insert(QStringLiteral("conditions"), serialized.value());
    }

    QJsonArray actions;
    for (const ActionNode& entry : rule.actions) {
        auto serialized = actionJson(entry, 0);
        if (!serialized) {
        }
        Q_ASSERT(serialized.has_value());
        actions.append(serialized.value());
    }
    object.insert(QStringLiteral("actions"), actions);

    if (!rule.lastRun.firedAtIso.isEmpty() || !rule.lastRun.outcome.isEmpty()) {
        QJsonObject lastRun;
        if (!rule.lastRun.firedAtIso.isEmpty()) {
            lastRun.insert(QStringLiteral("lastFiredAt"), rule.lastRun.firedAtIso);
        }
        if (!rule.lastRun.outcome.isEmpty()) {
            lastRun.insert(QStringLiteral("outcome"), rule.lastRun.outcome);
        }
        object.insert(QStringLiteral("lastRun"), lastRun);
    }
    return object;
}

} // namespace RokuDeck

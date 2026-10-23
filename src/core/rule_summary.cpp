// SPDX-License-Identifier: GPL-3.0-or-later
#include "rule_summary.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <limits>

namespace RokuDeck {
namespace {

[[nodiscard]] QString renderNumber(const QJsonValue& value) {
    const double raw = value.toDouble();
    if (std::isfinite(raw) && std::trunc(raw) == raw &&
        raw >= std::numeric_limits<int>::min() && raw <= std::numeric_limits<int>::max()) {
        return QString::number(static_cast<int>(raw));
    }
    return QString::number(raw);
}

// A predicate value renders as its literal: strings bare, booleans as
// true/false, numbers trimmed. Anything exotic degrades the whole rule.
[[nodiscard]] bool renderValue(const QJsonValue& value, QString* destination) {
    if (value.isString()) {
        *destination = value.toString();
        return true;
    }
    if (value.isBool()) {
        *destination = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        return true;
    }
    if (value.isDouble()) {
        *destination = renderNumber(value);
        return true;
    }
    return false;
}

// Returns an empty string for anything this renderer does not recognize; the
// caller treats that as the signal to degrade to "Custom rule".
[[nodiscard]] QString renderTrigger(const QJsonObject& trigger) {
    const QString type = trigger.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("manual")) {
        return QStringLiteral("Manually run");
    }
    if (type == QStringLiteral("interval")) {
        const QJsonValue minutes = trigger.value(QStringLiteral("minutes"));
        if (!minutes.isDouble() || minutes.toDouble() < 1 ||
            minutes.toDouble() != std::trunc(minutes.toDouble())) {
            return {};
        }
        return QStringLiteral("Every %1 minutes").arg(renderNumber(minutes));
    }
    if (type == QStringLiteral("time")) {
        const QString kind = trigger.value(QStringLiteral("kind")).toString();
        const QString time = trigger.value(QStringLiteral("time")).toString();
        if (time.isEmpty()) {
            return {};
        }
        if (kind == QStringLiteral("daily")) {
            return QStringLiteral("Daily at %1").arg(time);
        }
        if (kind == QStringLiteral("weekdays")) {
            return QStringLiteral("Weekdays at %1").arg(time);
        }
        if (kind == QStringLiteral("once")) {
            const QString date = trigger.value(QStringLiteral("date")).toString();
            if (date.isEmpty()) {
                return {};
            }
            return QStringLiteral("Once on %1 at %2").arg(date, time);
        }
        return {};
    }
    if (type == QStringLiteral("stateChange")) {
        const QString field = trigger.value(QStringLiteral("field")).toString();
        const QString op = trigger.value(QStringLiteral("op")).toString();
        const QJsonValue value = trigger.value(QStringLiteral("value"));
        QString renderedValue;
        QString renderedPredicate;
        if (field.isEmpty()) {
            return {};
        }
        if (op == QStringLiteral("between")) {
            if (!value.isArray() || value.toArray().size() != 2) {
                return {};
            }
            const QJsonArray bounds = value.toArray();
            QString low;
            QString high;
            if (!renderValue(bounds.at(0), &low) || !renderValue(bounds.at(1), &high)) {
                return {};
            }
            renderedPredicate = QStringLiteral("%1 between %2 and %3").arg(field, low, high);
        } else {
            QString comparison;
            if (op == QStringLiteral("is")) {
                comparison = QStringLiteral("is");
            } else if (op == QStringLiteral("isNot")) {
                comparison = QStringLiteral("is not");
            } else if (op == QStringLiteral("gt")) {
                comparison = QStringLiteral("above");
            } else if (op == QStringLiteral("lt")) {
                comparison = QStringLiteral("below");
            } else {
                return {};
            }
            if (!renderValue(value, &renderedValue)) {
                return {};
            }
            renderedPredicate =
                QStringLiteral("%1 %2 %3").arg(field, comparison, renderedValue);
        }
        return QStringLiteral("When %1").arg(renderedPredicate);
    }
    return {};
}

[[nodiscard]] QString renderAction(const QJsonObject& action) {
    const QString type = action.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("keypress")) {
        const QString key = action.value(QStringLiteral("key")).toString();
        return key.isEmpty() ? QString() : QStringLiteral("Press %1").arg(key);
    }
    if (type == QStringLiteral("launchApp")) {
        const QString appId = action.value(QStringLiteral("appId")).toString();
        return appId.isEmpty() ? QString() : QStringLiteral("Launch %1").arg(appId);
    }
    if (type == QStringLiteral("setVolume")) {
        const QJsonValue volume = action.value(QStringLiteral("volume"));
        if (!volume.isDouble() || volume.toDouble() < 0 || volume.toDouble() > 100 ||
            volume.toDouble() != std::trunc(volume.toDouble())) {
            return {};
        }
        return QStringLiteral("Set volume to %1").arg(renderNumber(volume));
    }
    if (type == QStringLiteral("mute")) {
        const QJsonValue muted = action.value(QStringLiteral("muted"));
        if (!muted.isBool()) {
            return {};
        }
        return muted.toBool() ? QStringLiteral("Mute") : QStringLiteral("Unmute");
    }
    if (type == QStringLiteral("delay")) {
        const QJsonValue seconds = action.value(QStringLiteral("seconds"));
        if (!seconds.isDouble() || seconds.toDouble() < 1 ||
            seconds.toDouble() != std::trunc(seconds.toDouble())) {
            return {};
        }
        return QStringLiteral("Wait %1 s").arg(renderNumber(seconds));
    }
    if (type == QStringLiteral("runRule")) {
        const QString id = action.value(QStringLiteral("id")).toString();
        return id.isEmpty() ? QString() : QStringLiteral("Run rule %1").arg(id);
    }
    if (type == QStringLiteral("runScript")) {
        const QString script = action.value(QStringLiteral("script")).toString();
        return script.isEmpty() ? QString() : QStringLiteral("Run script %1").arg(script);
    }
    if (type == QStringLiteral("ifThenElse")) {
        if (!action.value(QStringLiteral("cond")).isObject() ||
            !action.value(QStringLiteral("then")).isArray()) {
            return {};
        }
        return QStringLiteral("Conditional step");
    }
    return {};
}

} // namespace

QString ruleSummary(const QJsonObject& rule) {
    QStringList parts;

    const QJsonObject triggerObject =
        rule.value(QStringLiteral("trigger")).toObject();
    const QString trigger = renderTrigger(triggerObject);
    if (trigger.isEmpty()) {
        return QStringLiteral("Custom rule");
    }
    parts.append(trigger);

    // An activeWindow block appends its span; one the renderer cannot read
    // degrades the whole rule rather than showing a half-truth.
    if (triggerObject.contains(QStringLiteral("activeWindow"))) {
        const QJsonObject window =
            triggerObject.value(QStringLiteral("activeWindow")).toObject();
        const QString start = window.value(QStringLiteral("start")).toString();
        const QString end = window.value(QStringLiteral("end")).toString();
        if (start.isEmpty() || end.isEmpty()) {
            return QStringLiteral("Custom rule");
        }
        parts.append(QStringLiteral("active %1\u2013%2").arg(start, end));
    }

    const QJsonArray actions = rule.value(QStringLiteral("actions")).toArray();
    if (actions.isEmpty()) {
        return QStringLiteral("Custom rule");
    }
    for (const QJsonValue& entry : actions) {
        if (!entry.isObject()) {
            return QStringLiteral("Custom rule");
        }
        const QString action = renderAction(entry.toObject());
        if (action.isEmpty()) {
            return QStringLiteral("Custom rule");
        }
        parts.append(action);
    }

    const QJsonValue conditions = rule.value(QStringLiteral("conditions"));
    if (conditions.isObject() && conditions.toObject().size() > 0) {
        parts.append(QStringLiteral("extra conditions"));
    }

    return parts.join(QStringLiteral(" \u00b7 "));
}

} // namespace RokuDeck

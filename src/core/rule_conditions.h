// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device_state.h"
#include "rule_model.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace RokuDeck {

// Headless condition evaluation over device snapshots. A field value that is
// absent or Unknown never matches - including for isNot - so a default
// (all-Unknown) snapshot can never satisfy a predicate on its own.

[[nodiscard]] bool predicateMatches(const FieldPredicate& predicate,
                                    const QHash<QString, DeviceSnapshot>& snapshots,
                                    const QStringList& targets);

[[nodiscard]] bool conditionsMet(const ConditionNode& conditions,
                                 const QHash<QString, DeviceSnapshot>& snapshots,
                                 const QStringList& targets);

// Enabled stateChange rules whose trigger predicate flips false->true between
// previous and current, in rule-list order.
[[nodiscard]] QStringList dueStateChangeRuleIds(const QList<RuleDocument>& rules,
                                                const QString& serial,
                                                const DeviceSnapshot& previous,
                                                const DeviceSnapshot& current);

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>

namespace RokuDeck {

// Plain-language one-line summary of a stored automation rule, per spec §7:
// "Weekdays at 23:00 · When volume > 20 · Set Living Room TV volume to 15".
//
// The renderer reads the raw JSON rather than a parsed RuleDocument on
// purpose: parseRule rejects shapes it does not know, while this widget-side
// view must keep showing rules a newer daemon understands by degrading any
// unrecognized trigger or action type to "Custom rule".
[[nodiscard]] QString ruleSummary(const QJsonObject& rule);

} // namespace RokuDeck

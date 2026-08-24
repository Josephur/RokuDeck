// SPDX-License-Identifier: GPL-3.0-or-later

#include "automationsmodel.h"

namespace RokuDeck {

AutomationsModel::AutomationsModel(QObject* parent) : QAbstractListModel(parent) {}

int AutomationsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rules.size());
}

QVariant AutomationsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rules.size())) {
        return {};
    }
    const RuleEntry& entry = m_rules.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case NameRole:
        return entry.name;
    case EnabledRole:
        return entry.enabled;
    case SummaryRole:
        return entry.summary;
    case OutcomeRole:
        return entry.outcome;
    case LastFiredRole:
        return entry.firedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> AutomationsModel::roleNames() const {
    static const QHash<int, QByteArray> names{
        {IdRole, "ruleId"},         {NameRole, "name"},
        {EnabledRole, "enabled"},   {SummaryRole, "summary"},
        {OutcomeRole, "outcome"},   {LastFiredRole, "lastFired"}};
    return names;
}

void AutomationsModel::replaceRules(const QList<RuleEntry>& rules) {
    beginResetModel();
    m_rules = rules;
    endResetModel();
}

QVariantList AutomationsModel::ruleEntries(const QString& excludingId) const {
    QVariantList entries;
    entries.reserve(m_rules.size());
    for (const RuleEntry& rule : m_rules) {
        if (rule.id == excludingId) {
            continue;
        }
        entries.append(QVariantMap{{QStringLiteral("id"), rule.id},
                                   {QStringLiteral("name"), rule.name}});
    }
    return entries;
}

} // namespace RokuDeck

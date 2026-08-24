// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariant>

namespace RokuDeck {

// One display row for a daemon-owned automation rule. Everything here is
// presentation-ready: the summary sentence is rendered by the core
// rule_summary helper before it reaches this model.
struct RuleEntry {
    QString id;
    QString name;
    bool enabled = true;
    QString summary;
    QString outcome;  // last-run outcome; empty when the rule never ran
    QString firedAt;  // ISO timestamp of the last run; empty when never run
};

// Mirrors DevicesModel: a plain list model with no D-Bus inside. DaemonProxy
// owns all transport and hands over fully formed rows.
class AutomationsModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        EnabledRole,
        SummaryRole,
        OutcomeRole,
        LastFiredRole,
    };
    Q_ENUM(Role)

    explicit AutomationsModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replaceRules(const QList<RuleEntry>& rules);

    // QML runRule pickers need [{id, name}, ...]; the rule being edited is
    // excluded so a macro cannot target itself.
    Q_INVOKABLE QVariantList ruleEntries(const QString& excludingId = {}) const;

  private:
    QList<RuleEntry> m_rules;
};

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/device_registry.h"
#include "core/rule_model.h"

#include <KSharedConfig>

#include <QList>

namespace RokuDeck {

class ConfigStore final {
  public:
    explicit ConfigStore(
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("rokudeckrc")));

    [[nodiscard]] QList<DeviceRecord> loadDevices() const;
    void saveDevices(const QList<DeviceRecord>& devices);

    // Automation rules persist under a [Rules] parent group, one subgroup per
    // rule in insertion order. Entries that fail to parse are skipped on load
    // so one corrupt record cannot hide the rest.
    [[nodiscard]] QList<RuleDocument> loadRules() const;
    void saveRules(const QList<RuleDocument>& rules);

  private:
    KSharedConfig::Ptr m_config;
};

} // namespace RokuDeck

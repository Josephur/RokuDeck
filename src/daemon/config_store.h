// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/device_registry.h"

#include <KSharedConfig>

#include <QList>

namespace RokuDeck {

class ConfigStore final {
  public:
    explicit ConfigStore(
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("rokudeckrc")));

    [[nodiscard]] QList<DeviceRecord> loadDevices() const;
    void saveDevices(const QList<DeviceRecord>& devices);

  private:
    KSharedConfig::Ptr m_config;
};

} // namespace RokuDeck

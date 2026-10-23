// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_store.h"

#include <KConfigGroup>

#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <utility>

namespace RokuDeck {

ConfigStore::ConfigStore(KSharedConfig::Ptr config) : m_config(std::move(config)) {
    Q_ASSERT(m_config);
}

QList<DeviceRecord> ConfigStore::loadDevices() const {
    const KConfigGroup devicesGroup(m_config, QStringLiteral("Devices"));
    QList<DeviceRecord> devices;
    const QStringList groups = devicesGroup.groupList();
    devices.reserve(groups.size());
    for (const QString& groupName : groups) {
        const KConfigGroup deviceGroup(&devicesGroup, groupName);
        DeviceRecord device;
        device.serial = deviceGroup.readEntry("Serial", QString());
        device.displayName = deviceGroup.readEntry("DisplayName", QString());
        device.address = deviceGroup.readEntry("Address", QString());
        device.nameIsUserChosen = deviceGroup.readEntry("NameIsUserChosen", false);
        device.allowAutomations = deviceGroup.readEntry("AllowAutomations", true);
        device.wakeOnWlan = deviceGroup.readEntry("WakeOnWlan", false);
        device.refreshPolicy =
            deviceGroup.readEntry("RefreshPolicy", QStringLiteral("balanced"));
        // Files written before ceilings existed carry no key: unrestricted.
        device.volumeCeiling = deviceGroup.readEntry("VolumeCeiling", 100);
        // A file written before ordering existed has no SortIndex; falling back
        // to the group position keeps those devices in their recorded order.
        device.sortIndex = deviceGroup.readEntry("SortIndex", static_cast<int>(devices.size()));
        if (!device.serial.isEmpty()) {
            devices.append(std::move(device));
        }
    }
    std::ranges::stable_sort(devices, [](const DeviceRecord& left, const DeviceRecord& right) {
        if (left.sortIndex != right.sortIndex) {
            return left.sortIndex < right.sortIndex;
        }
        return left.serial < right.serial;
    });
    return devices;
}

void ConfigStore::saveDevices(const QList<DeviceRecord>& devices) {
    m_config->deleteGroup(QStringLiteral("Devices"));
    KConfigGroup devicesGroup(m_config, QStringLiteral("Devices"));
    for (qsizetype index = 0; index < devices.size(); ++index) {
        const DeviceRecord& device = devices.at(index);
        KConfigGroup deviceGroup(&devicesGroup, QStringLiteral("Device%1").arg(index));
        deviceGroup.writeEntry("Serial", device.serial);
        deviceGroup.writeEntry("DisplayName", device.displayName);
        deviceGroup.writeEntry("Address", device.address);
        deviceGroup.writeEntry("NameIsUserChosen", device.nameIsUserChosen);
        deviceGroup.writeEntry("AllowAutomations", device.allowAutomations);
        deviceGroup.writeEntry("WakeOnWlan", device.wakeOnWlan);
        deviceGroup.writeEntry("RefreshPolicy", device.refreshPolicy);
        deviceGroup.writeEntry("VolumeCeiling", device.volumeCeiling);
        deviceGroup.writeEntry("SortIndex", device.sortIndex);
    }
    m_config->sync();
}

QList<RuleDocument> ConfigStore::loadRules() const {
    const KConfigGroup rulesGroup(m_config, QStringLiteral("Rules"));
    QList<RuleDocument> rules;
    const QStringList groups = rulesGroup.groupList();
    rules.reserve(groups.size());
    struct Loaded { RuleDocument rule; int sortIndex = 0; QString groupName; };
    QList<Loaded> loaded;
    for (const QString& groupName : groups) {
        const KConfigGroup ruleGroup(&rulesGroup, groupName);
        const QString definition = ruleGroup.readEntry("Definition", QString());
        if (definition.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(definition.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }
        auto parsed = parseRule(document.object());
        if (!parsed) {
            continue;
        }
        RuleDocument rule = std::move(parsed.value());
        // The mirrored metadata wins over the definition so a listing never
        // disagrees with the group entries.
        rule.name = ruleGroup.readEntry("Name", rule.name);
        rule.enabled = ruleGroup.readEntry("Enabled", true);
        loaded.append({std::move(rule),
            ruleGroup.readEntry("SortIndex", static_cast<int>(loaded.size())),
            groupName});
    }
    std::ranges::stable_sort(loaded, [](const Loaded& left, const Loaded& right) {
        if (left.sortIndex != right.sortIndex) {
            return left.sortIndex < right.sortIndex;
        }
        return left.groupName < right.groupName;
    });
    for (Loaded& entry : loaded) {
        rules.append(std::move(entry.rule));
    }
    return rules;
}

void ConfigStore::saveRules(const QList<RuleDocument>& rules) {
    // A rule this build cannot parse is preserved verbatim. loadRules() skips
    // such entries for display, but rewriting [Rules] from the parsed list
    // alone would delete them from disk — a downgrade or a hand edit must not
    // turn into permanent data loss.
    struct PreservedRule {
        QString name;
        bool enabled = true;
        QString definition;
        int sortIndex = 0;
    };
    QList<PreservedRule> preserved;
    {
        const KConfigGroup existing(m_config, QStringLiteral("Rules"));
        for (const QString& groupName : existing.groupList()) {
            const KConfigGroup group(&existing, groupName);
            const QString definition = group.readEntry("Definition", QString());
            if (definition.isEmpty()) {
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(definition.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()
                && parseRule(document.object()).has_value()) {
                continue;  // a live rule; rewritten from `rules` below
            }
            preserved.append({group.readEntry("Name", QString()),
                group.readEntry("Enabled", true), definition,
                group.readEntry("SortIndex", static_cast<int>(preserved.size()))});
        }
    }

    m_config->deleteGroup(QStringLiteral("Rules"));
    KConfigGroup rulesGroup(m_config, QStringLiteral("Rules"));
    for (qsizetype index = 0; index < rules.size(); ++index) {
        const RuleDocument& rule = rules.at(index);
        KConfigGroup ruleGroup(&rulesGroup, QStringLiteral("Rule%1").arg(index));
        ruleGroup.writeEntry("Name", rule.name);
        ruleGroup.writeEntry("Enabled", rule.enabled);
        ruleGroup.writeEntry("Definition",
            QString::fromUtf8(QJsonDocument(ruleJson(rule)).toJson()));
        ruleGroup.writeEntry("SortIndex", static_cast<int>(index));
    }
    for (qsizetype index = 0; index < preserved.size(); ++index) {
        const PreservedRule& entry = preserved.at(index);
        KConfigGroup ruleGroup(&rulesGroup,
            QStringLiteral("Rule%1").arg(rules.size() + index));
        ruleGroup.writeEntry("Name", entry.name);
        ruleGroup.writeEntry("Enabled", entry.enabled);
        ruleGroup.writeEntry("Definition", entry.definition);
        ruleGroup.writeEntry("SortIndex", entry.sortIndex);
    }
    m_config->sync();
}

} // namespace RokuDeck

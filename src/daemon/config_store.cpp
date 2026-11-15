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

PeerListenerSettings ConfigStore::loadPeerListener() const {
    const KConfigGroup group(m_config, QStringLiteral("PeerListener"));
    PeerListenerSettings settings;
    settings.enabled = group.readEntry("Enabled", false);
    settings.port = static_cast<quint16>(
        group.readEntry("Port", static_cast<int>(PeerListenerSettings{}.port)));
    return settings;
}

void ConfigStore::savePeerListener(const PeerListenerSettings& settings) {
    KConfigGroup group(m_config, QStringLiteral("PeerListener"));
    group.writeEntry("Enabled", settings.enabled);
    group.writeEntry("Port", static_cast<int>(settings.port));
    m_config->sync();
}

QList<PairedController> ConfigStore::loadPairedControllers() const {
    const KConfigGroup peersGroup(m_config, QStringLiteral("Peers"));
    QList<PairedController> controllers;
    const QStringList groups = peersGroup.groupList();
    controllers.reserve(groups.size());
    for (const QString& groupName : groups) {
        const KConfigGroup controllerGroup(&peersGroup, groupName);
        PairedController controller;
        controller.machineId = controllerGroup.readEntry("MachineId", QString());
        controller.token = controllerGroup.readEntry("Token", QString());
        controller.addedAt = QDateTime::fromString(
            controllerGroup.readEntry("AddedAt", QString()), Qt::ISODateWithMs);
        if (!controller.machineId.isEmpty() && !controller.token.isEmpty()) {
            controllers.append(std::move(controller));
        }
    }
    return controllers;
}

void ConfigStore::savePairedControllers(const QList<PairedController>& controllers) {
    m_config->deleteGroup(QStringLiteral("Peers"));
    KConfigGroup peersGroup(m_config, QStringLiteral("Peers"));
    // The machine id is the identity and may contain characters KConfig group
    // names mangle, so entries stay named by index and carry the id as data.
    for (qsizetype index = 0; index < controllers.size(); ++index) {
        const PairedController& controller = controllers.at(index);
        KConfigGroup controllerGroup(&peersGroup, QStringLiteral("Controller%1").arg(index));
        controllerGroup.writeEntry("MachineId", controller.machineId);
        controllerGroup.writeEntry("Token", controller.token);
        if (controller.addedAt.isValid()) {
            controllerGroup.writeEntry("AddedAt",
                controller.addedAt.toString(Qt::ISODateWithMs));
        }
    }
    m_config->sync();
}

QList<PeerTarget> ConfigStore::loadPeerTargets() const {
    const KConfigGroup targetsGroup(m_config, QStringLiteral("PeerTargets"));
    QList<PeerTarget> targets;
    const QStringList groups = targetsGroup.groupList();
    targets.reserve(groups.size());
    for (const QString& groupName : groups) {
        const KConfigGroup targetGroup(&targetsGroup, groupName);
        PeerTarget target;
        target.machineId = targetGroup.readEntry("MachineId", QString());
        target.displayName = targetGroup.readEntry("DisplayName", QString());
        target.address = targetGroup.readEntry("Address", QString());
        target.port = static_cast<quint16>(targetGroup.readEntry("Port", 0));
        target.token = targetGroup.readEntry("Token", QString());
        if (!target.machineId.isEmpty() && !target.token.isEmpty() && !target.address.isEmpty()
            && target.port > 0) {
            targets.append(std::move(target));
        }
    }
    return targets;
}

void ConfigStore::savePeerTargets(const QList<PeerTarget>& targets) {
    m_config->deleteGroup(QStringLiteral("PeerTargets"));
    KConfigGroup targetsGroup(m_config, QStringLiteral("PeerTargets"));
    for (qsizetype index = 0; index < targets.size(); ++index) {
        const PeerTarget& target = targets.at(index);
        KConfigGroup targetGroup(&targetsGroup, QStringLiteral("Target%1").arg(index));
        targetGroup.writeEntry("MachineId", target.machineId);
        targetGroup.writeEntry("DisplayName", target.displayName);
        targetGroup.writeEntry("Address", target.address);
        targetGroup.writeEntry("Port", static_cast<int>(target.port));
        targetGroup.writeEntry("Token", target.token);
    }
    m_config->sync();
}

} // namespace RokuDeck

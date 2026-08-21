// SPDX-License-Identifier: GPL-3.0-or-later
#include "client_registry.h"

namespace RokuDeck {

bool ClientRegistry::isEmpty() const {
    return m_clientsByOwner.isEmpty();
}

bool ClientRegistry::containsOwner(const QString& owner) const {
    return m_clientsByOwner.contains(owner);
}

std::optional<ClientRegistration> ClientRegistry::registration(const QString& owner,
                                                               const QString& clientId) const {
    const auto ownerIt = m_clientsByOwner.constFind(owner);
    if (ownerIt == m_clientsByOwner.cend()) {
        return std::nullopt;
    }
    const auto clientIt = ownerIt->constFind(clientId);
    if (clientIt == ownerIt->cend()) {
        return std::nullopt;
    }
    return clientIt.value();
}

std::optional<ClientRegistration> ClientRegistry::normalized(const QString& owner,
                                                             const QString& clientId,
                                                             const QStringList& serials,
                                                             quint32 intervalMs) {
    if (owner.isEmpty() || clientId.trimmed().isEmpty() || intervalMs == 0) {
        return std::nullopt;
    }

    ClientRegistration registration;
    registration.owner = owner;
    registration.clientId = clientId;
    registration.intervalMs = intervalMs;
    for (const QString& serial : serials) {
        if (!serial.trimmed().isEmpty()) {
            registration.serials.insert(serial);
        }
    }
    return registration;
}

QList<ClientRegistration> ClientRegistry::registrations() const {
    QList<ClientRegistration> all;
    for (const auto& byClientId : m_clientsByOwner) {
        for (const ClientRegistration& registration : byClientId) {
            all.append(registration);
        }
    }
    return all;
}

bool ClientRegistry::registerClient(const QString& owner, const QString& clientId,
                                    const QStringList& serials, quint32 intervalMs) {
    const auto value = normalized(owner, clientId, serials, intervalMs);
    if (!value) {
        return false;
    }
    m_clientsByOwner[owner].insert(clientId, *value);
    return true;
}

bool ClientRegistry::updateClient(const QString& owner, const QString& clientId,
                                  const QStringList& serials, quint32 intervalMs) {
    if (!registration(owner, clientId)) {
        return false;
    }
    return registerClient(owner, clientId, serials, intervalMs);
}

std::optional<ClientRegistration> ClientRegistry::takeClient(const QString& owner,
                                                             const QString& clientId) {
    auto ownerIt = m_clientsByOwner.find(owner);
    if (ownerIt == m_clientsByOwner.end()) {
        return std::nullopt;
    }
    auto clientIt = ownerIt->find(clientId);
    if (clientIt == ownerIt->end()) {
        return std::nullopt;
    }

    ClientRegistration registration = clientIt.value();
    ownerIt->erase(clientIt);
    if (ownerIt->isEmpty()) {
        m_clientsByOwner.erase(ownerIt);
    }
    return registration;
}

QList<ClientRegistration> ClientRegistry::takeOwner(const QString& owner) {
    const auto ownerIt = m_clientsByOwner.find(owner);
    if (ownerIt == m_clientsByOwner.end()) {
        return {};
    }
    const QList<ClientRegistration> registrations = ownerIt->values();
    m_clientsByOwner.erase(ownerIt);
    return registrations;
}

} // namespace RokuDeck

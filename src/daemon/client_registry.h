// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace RokuDeck {

struct ClientRegistration {
    QString owner;
    QString clientId;
    QSet<QString> serials;
    quint32 intervalMs = 0;
};

class ClientRegistry final {
  public:
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool containsOwner(const QString& owner) const;
    [[nodiscard]] std::optional<ClientRegistration> registration(const QString& owner,
                                                                 const QString& clientId) const;

    bool registerClient(const QString& owner, const QString& clientId, const QStringList& serials,
                        quint32 intervalMs);
    bool updateClient(const QString& owner, const QString& clientId, const QStringList& serials,
                      quint32 intervalMs);
    [[nodiscard]] std::optional<ClientRegistration> takeClient(const QString& owner,
                                                               const QString& clientId);
    [[nodiscard]] QList<ClientRegistration> takeOwner(const QString& owner);

  private:
    [[nodiscard]] static std::optional<ClientRegistration> normalized(const QString& owner,
                                                                      const QString& clientId,
                                                                      const QStringList& serials,
                                                                      quint32 intervalMs);

    QHash<QString, QHash<QString, ClientRegistration>> m_clientsByOwner;
};

} // namespace RokuDeck

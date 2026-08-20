// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "process_runner.h"
#include "rds_request.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace RokuDeck {

using SnapshotCallback = std::function<void(RdsQueryResult)>;

class RdsClient final : public QObject {
  public:
    explicit RdsClient(ProcessRunner* runner);

    void queryDevice(const QString& address, SnapshotCallback callback);
    [[nodiscard]] bool pollingAvailable() const;
    [[nodiscard]] QString diagnostic() const;

  private:
    enum class CompatibilityState { Unchecked, Checking, Compatible, Disabled };

    struct PendingQuery {
        QString address;
        SnapshotCallback callback;
    };

    void checkCompatibility();
    void startQuery(PendingQuery query);
    void disablePolling(RdsError error);

    ProcessRunner* m_runner;
    CompatibilityState m_compatibility = CompatibilityState::Unchecked;
    QList<PendingQuery> m_pendingQueries;
    RdsError m_disabledError{RdsError::Code::DependencyUnavailable,
                             QStringLiteral("rds compatibility has not been checked")};
};

} // namespace RokuDeck

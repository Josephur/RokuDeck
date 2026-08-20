// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "process_runner.h"
#include "rds_request.h"

#include <functional>

namespace RokuDeck {

using SnapshotCallback = std::function<void(RdsQueryResult)>;

class RdsClient final {
public:
    explicit RdsClient(ProcessRunner *runner);

    void queryDevice(const QString &address, SnapshotCallback callback);

private:
    ProcessRunner *m_runner;
};

} // namespace RokuDeck

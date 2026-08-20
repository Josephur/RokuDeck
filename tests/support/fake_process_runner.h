// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/process_runner.h"

#include <QList>

#include <utility>

namespace RokuDeck::Test {

class FakeProcessRunner final : public ProcessRunner {
public:
    using ProcessRunner::ProcessRunner;

    void start(const ProcessRequest &request, ProcessCallback callback) override
    {
        lastRequest = request;
        requests.append(request);
        callbacks.append(std::move(callback));
    }

    void complete(ProcessResult result)
    {
        Q_ASSERT(!callbacks.isEmpty());
        ProcessCallback callback = callbacks.takeFirst();
        callback(std::move(result));
    }

    ProcessRequest lastRequest;
    QList<ProcessRequest> requests;
    QList<ProcessCallback> callbacks;
};

} // namespace RokuDeck::Test

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "process_runner.h"

#include <QList>

#include <memory>

namespace RokuDeck {

class QProcessRunner final : public ProcessRunner {
public:
    using ProcessRunner::ProcessRunner;
    ~QProcessRunner() override;

    static constexpr qsizetype maximumCapturedBytes() { return 4096; }
    void start(const ProcessRequest &request, ProcessCallback callback) override;

private:
    struct RunState;

    void drain(const std::shared_ptr<RunState> &state);
    void complete(const std::shared_ptr<RunState> &state, int exitCode);

    QList<std::shared_ptr<RunState>> m_activeStates;
};

} // namespace RokuDeck

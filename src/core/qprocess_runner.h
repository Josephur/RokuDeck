// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "process_runner.h"

namespace RokuDeck {

class QProcessRunner final : public ProcessRunner {
public:
    using ProcessRunner::ProcessRunner;

    static constexpr qsizetype maximumCapturedBytes() { return 4096; }
    void start(const ProcessRequest &request, ProcessCallback callback) override;
};

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#include "qprocess_runner.h"

#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace RokuDeck {
namespace {

struct RunState {
    QProcess *process = nullptr;
    ProcessCallback callback;
    QByteArray standardOutput;
    QByteArray standardError;
    bool completed = false;
    bool timedOut = false;
};

void appendCapped(QByteArray &destination, QByteArray chunk)
{
    const qsizetype available = QProcessRunner::maximumCapturedBytes() - destination.size();
    if (available > 0) {
        destination.append(chunk.first(std::min(available, chunk.size())));
    }
}

} // namespace

void QProcessRunner::start(const ProcessRequest &request, ProcessCallback callback)
{
    auto state = std::make_shared<RunState>();
    state->process = new QProcess(this);
    state->callback = std::move(callback);

    QTimer *timer = new QTimer(state->process);
    timer->setSingleShot(true);
    const auto timeoutCount = std::clamp<std::chrono::milliseconds::rep>(
        request.timeout.count(), 0, std::numeric_limits<int>::max());
    timer->setInterval(static_cast<int>(timeoutCount));

    const auto readOutput = [state] {
        appendCapped(state->standardOutput, state->process->readAllStandardOutput());
        appendCapped(state->standardError, state->process->readAllStandardError());
    };
    const auto finish = [state, timer, readOutput](int exitCode) {
        if (state->completed) {
            return;
        }
        state->completed = true;
        timer->stop();
        readOutput();

        ProcessResult result;
        result.exitCode = exitCode;
        result.standardOutput = std::move(state->standardOutput);
        result.standardError = std::move(state->standardError);
        result.timedOut = state->timedOut;
        ProcessCallback callback = std::move(state->callback);
        state->process->deleteLater();
        callback(std::move(result));
    };

    connect(state->process, &QProcess::readyReadStandardOutput, this, readOutput);
    connect(state->process, &QProcess::readyReadStandardError, this, readOutput);
    connect(state->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finish](int exitCode, QProcess::ExitStatus) mutable { finish(exitCode); });
    connect(state->process, &QProcess::errorOccurred, this, [state, finish](QProcess::ProcessError error) mutable {
        if (error == QProcess::FailedToStart) {
            finish(-1);
        }
    });
    connect(timer, &QTimer::timeout, this, [state, finish]() mutable {
        state->timedOut = true;
        state->process->kill();
        finish(-1);
    });

    state->process->setProgram(request.program);
    state->process->setArguments(request.arguments);
    state->process->start();
    timer->start();
}

} // namespace RokuDeck

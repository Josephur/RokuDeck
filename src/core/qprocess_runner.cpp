// SPDX-License-Identifier: GPL-3.0-or-later
#include "qprocess_runner.h"

#include <QDeadlineTimer>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace RokuDeck {
namespace {

void appendCapped(QByteArray& destination, QByteArray chunk) {
    const qsizetype available = QProcessRunner::maximumCapturedBytes() - destination.size();
    if (available > 0) {
        destination.append(chunk.first(std::min(available, chunk.size())));
    }
}

} // namespace

struct QProcessRunner::RunState {
    QProcess* process = nullptr;
    QTimer* timer = nullptr;
    QTimer* terminationTimer = nullptr;
    ProcessCallback callback;
    QByteArray standardOutput;
    QByteArray standardError;
    bool completed = false;
    bool timedOut = false;
};

QProcessRunner::~QProcessRunner() {
    const QList<std::shared_ptr<RunState>> activeStates = m_activeStates;
    for (const auto& state : activeStates) {
        if (state->completed) {
            continue;
        }
        state->timer->stop();
        disconnect(state->process, nullptr, this, nullptr);
        if (state->process->state() != QProcess::NotRunning) {
            state->process->kill();
        }
    }

    QDeadlineTimer deadline(static_cast<qint64>(terminationGracePeriod().count()),
                            Qt::PreciseTimer);
    for (const auto& state : activeStates) {
        if (state->completed) {
            continue;
        }
        if (state->process->state() != QProcess::NotRunning && deadline.remainingTime() > 0) {
            state->process->waitForFinished(static_cast<int>(deadline.remainingTime()));
        }
        complete(state, -1);
    }
}

void QProcessRunner::drain(const std::shared_ptr<RunState>& state) {
    appendCapped(state->standardOutput, state->process->readAllStandardOutput());
    appendCapped(state->standardError, state->process->readAllStandardError());
}

void QProcessRunner::complete(const std::shared_ptr<RunState>& state, int exitCode) {
    if (state->completed) {
        return;
    }
    state->completed = true;
    state->timer->stop();
    state->terminationTimer->stop();
    drain(state);

    ProcessResult result;
    result.exitCode = exitCode;
    result.standardOutput = std::move(state->standardOutput);
    result.standardError = std::move(state->standardError);
    result.timedOut = state->timedOut;
    ProcessCallback callback = std::move(state->callback);
    m_activeStates.removeOne(state);
    disconnect(state->process, nullptr, this, nullptr);
    if (state->process->state() == QProcess::NotRunning) {
        state->process->deleteLater();
    } else {
        state->process->setParent(nullptr);
        connect(state->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                state->process, &QObject::deleteLater);
    }
    callback(std::move(result));
}

void QProcessRunner::start(const ProcessRequest& request, ProcessCallback callback) {
    auto state = std::make_shared<RunState>();
    state->process = new QProcess(this);
    state->callback = std::move(callback);
    m_activeStates.append(state);

    state->timer = new QTimer(state->process);
    state->timer->setSingleShot(true);
    state->terminationTimer = new QTimer(state->process);
    state->terminationTimer->setSingleShot(true);
    state->terminationTimer->setInterval(static_cast<int>(terminationGracePeriod().count()));
    const auto timeoutCount = std::clamp<std::chrono::milliseconds::rep>(
        request.timeout.count(), 0, std::numeric_limits<int>::max());
    state->timer->setInterval(static_cast<int>(timeoutCount));

    QPointer<QProcessRunner> runner(this);
    const auto readOutput = [runner, state] {
        if (runner) {
            runner->drain(state);
        }
    };
    connect(state->process, &QProcess::readyReadStandardOutput, this, readOutput);
    connect(state->process, &QProcess::readyReadStandardError, this, readOutput);
    connect(state->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [runner, state](int exitCode, QProcess::ExitStatus) {
                if (runner) {
                    runner->complete(state, exitCode);
                }
            });
    connect(state->process, &QProcess::errorOccurred, this,
            [runner, state](QProcess::ProcessError error) {
                if (runner && error == QProcess::FailedToStart) {
                    runner->complete(state, -1);
                }
            });
    connect(state->timer, &QTimer::timeout, this, [state] {
        state->timedOut = true;
        state->process->kill();
        state->terminationTimer->start();
    });
    connect(state->terminationTimer, &QTimer::timeout, this, [runner, state] {
        if (runner) {
            runner->complete(state, -1);
        }
    });

    state->process->setProgram(request.program);
    state->process->setArguments(request.arguments);
    state->process->start();
    state->timer->start();
}

} // namespace RokuDeck

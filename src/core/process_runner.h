// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>

namespace RokuDeck {

struct ProcessRequest {
    QString program;
    QStringList arguments;
    std::chrono::milliseconds timeout{3000};
};

struct ProcessResult {
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
    bool timedOut = false;
};

using ProcessCallback = std::function<void(ProcessResult)>;

class ProcessRunner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~ProcessRunner() override = default;

    virtual void start(const ProcessRequest &request, ProcessCallback callback) = 0;
};

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device_state.h"

#include <QString>

#include <utility>
#include <variant>

namespace RokuDeck {

struct RdsError {
    enum class Code {
        TimedOut,
        ProcessFailed,
        MalformedEnvelope,
        MissingDeviceInfo,
        MalformedPayload,
    };

    Code code = Code::ProcessFailed;
    QString message;
};

class RdsQueryResult {
public:
    RdsQueryResult(DeviceSnapshot snapshot)
        : m_result(std::move(snapshot))
    {
    }

    RdsQueryResult(RdsError error)
        : m_result(std::move(error))
    {
    }

    [[nodiscard]] bool has_value() const { return std::holds_alternative<DeviceSnapshot>(m_result); }
    [[nodiscard]] explicit operator bool() const { return has_value(); }
    [[nodiscard]] const DeviceSnapshot &value() const { return std::get<DeviceSnapshot>(m_result); }
    [[nodiscard]] const RdsError &error() const { return std::get<RdsError>(m_result); }

private:
    std::variant<DeviceSnapshot, RdsError> m_result;
};

} // namespace RokuDeck

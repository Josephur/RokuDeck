// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace RokuDeck::CommandContract {

inline constexpr int maximumOutstandingPerDevice = 4;
inline constexpr int executionTimeoutMs = 3000;
inline constexpr int daemonCompletionDeadlineMs =
    maximumOutstandingPerDevice * executionTimeoutMs + 500;
inline constexpr int clientCompletionTimeoutMs = daemonCompletionDeadlineMs + 1500;

} // namespace RokuDeck::CommandContract

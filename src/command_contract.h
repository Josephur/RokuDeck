// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace RokuDeck::CommandContract {

inline constexpr int maximumOutstandingPerDevice = 4;
inline constexpr int executionTimeoutMs = 3000;
// Local user scripts may legitimately take longer than any device round
// trip; their queue deadline and process budget both use this (ruling 4).
inline constexpr int scriptTimeoutMs = 30000;
inline constexpr int daemonCompletionDeadlineMs =
    maximumOutstandingPerDevice * executionTimeoutMs + 500;
inline constexpr int clientCompletionTimeoutMs = daemonCompletionDeadlineMs + 1500;

} // namespace RokuDeck::CommandContract

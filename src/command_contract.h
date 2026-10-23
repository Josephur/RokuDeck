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
// Commands dispatched by a rule share the per-device queue with its script
// steps, and one script may run for scriptTimeoutMs before the next command
// even starts. Automation dispatches therefore wait for a full queue's worth
// of script time before their own deadline expires; widget-issued SendCommand
// calls keep the tighter daemonCompletionDeadlineMs.
inline constexpr int automationCommandDeadlineMs =
    daemonCompletionDeadlineMs + maximumOutstandingPerDevice * scriptTimeoutMs;
// RunRule replies only after the whole action list has finished, and one
// script step alone may legitimately run for scriptTimeoutMs. libdbus falls
// back to a 25 s reply timeout when a call leaves it unset, so the plasmoid
// gives RunRule this budget explicitly instead of inheriting that default.
inline constexpr int automationReplyTimeoutMs = scriptTimeoutMs + 5000;

} // namespace RokuDeck::CommandContract

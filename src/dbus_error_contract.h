// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace RokuDeck::DbusError {

inline constexpr auto unknownDevice = "io.github.josephur.RokuDeck1.Error.UnknownDevice";
inline constexpr auto unsupportedCommand = "io.github.josephur.RokuDeck1.Error.UnsupportedCommand";
inline constexpr auto unsupportedCapability =
    "io.github.josephur.RokuDeck1.Error.UnsupportedCapability";
inline constexpr auto safetyRejected = "io.github.josephur.RokuDeck1.Error.SafetyRejected";
inline constexpr auto timeout = "io.github.josephur.RokuDeck1.Error.Timeout";
inline constexpr auto transportFailure = "io.github.josephur.RokuDeck1.Error.TransportFailure";

} // namespace RokuDeck::DbusError

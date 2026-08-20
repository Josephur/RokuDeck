// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_state.h"

namespace RokuDeck {

void DeviceSnapshot::normalize()
{
    if (power == PowerState::On) {
        return;
    }

    screensaver = TriState::Unknown;
    activeApp.reset();
    media = MediaPlaybackState::Unknown;
    volume.reset();
    muted.reset();
}

} // namespace RokuDeck

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device_state.h"
#include "parse_result.h"

#include <QByteArrayView>

namespace RokuDeck {

[[nodiscard]] ParseResult<MediaPlaybackState> parseMediaPlayer(QByteArrayView xml);

} // namespace RokuDeck

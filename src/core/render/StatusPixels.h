#pragma once

#include <cstdint>

#include "core/net/LinkStatus.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace render {

// Pulsing brightness for a diagnostic pixel: a triangle wave over periodMs, dark to full and back
// again. A period of zero leaves the colour alone.
uint32_t pulse(uint32_t rgb, int64_t nowMs, uint32_t periodMs);

// Left-edge connection pixels: top corner for Wi-Fi, bottom corner for MQTT. Nothing is drawn
// while both links are healthy, so a working device loses no pixels to diagnostics.
void drawLinkStatus(Canvas& out, const net::LinkStatus& wifi, const net::LinkStatus& mqtt,
                    int64_t nowMs);

}
}

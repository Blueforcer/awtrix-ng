#include "core/render/StatusPixels.h"

#include <cmath>

namespace awtrix {
namespace render {

namespace {

constexpr uint32_t kPulseMs = 2000;
constexpr uint32_t kWifiColor = 0xFF0000u;
constexpr uint32_t kMqttColor = 0xFFFF00u;

// A link nobody configured is not a fault, so an MQTT-less device shows no dot.
bool isDown(const net::LinkStatus& s) {
  return s.enabled && s.phase != net::LinkPhase::Connected && s.phase != net::LinkPhase::Disabled;
}

}

uint32_t pulse(uint32_t rgb, int64_t nowMs, uint32_t periodMs) {
  if (periodMs == 0) return rgb;
  const int64_t period = static_cast<int64_t>(periodMs);
  const float t = static_cast<float>(nowMs % period) / static_cast<float>(period);
  const float f = 1.0f - std::fabs(2.0f * t - 1.0f);
  const uint32_t r = static_cast<uint32_t>(((rgb >> 16) & 0xFF) * f);
  const uint32_t g = static_cast<uint32_t>(((rgb >> 8) & 0xFF) * f);
  const uint32_t b = static_cast<uint32_t>((rgb & 0xFF) * f);
  return (r << 16) | (g << 8) | b;
}

void drawLinkStatus(Canvas& out, const net::LinkStatus& wifi, const net::LinkStatus& mqtt,
                    int64_t nowMs) {
  if (isDown(wifi)) out.setPixel(0, 0, pulse(kWifiColor, nowMs, kPulseMs));
  // MQTT sitting offline because the Wi-Fi is down is not a second fault - it is the same outage,
  // and the Wi-Fi pixel already reports it. One dot lit always means one thing to go and fix.
  if (isDown(mqtt) && mqtt.error != net::LinkError::NoWifi)
    out.setPixel(0, out.height() - 1, pulse(kMqttColor, nowMs, kPulseMs));
}

}
}

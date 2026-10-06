#pragma once
#include <cstdint>

namespace awtrix::tc002::voice {
// The daemon uses boot-relative CLOCK_MONOTONIC; the host Arduino clock starts
// at runtime launch. Translate by age at receipt, never compare their origins.
inline bool localizeCaptureTime(int64_t capturedHostMs, int64_t receivedHostMs,
                                int64_t runtimeNowMs,
                                int64_t& runtimeCaptureMs) {
  if (capturedHostMs < 0 || receivedHostMs < capturedHostMs ||
      receivedHostMs - capturedHostMs > 250)
    return false;
  runtimeCaptureMs = runtimeNowMs - (receivedHostMs - capturedHostMs);
  return true;
}
}  // namespace awtrix::tc002::voice

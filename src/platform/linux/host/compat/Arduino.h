#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include "system/MonotonicClock.h"

using boolean = unsigned char;
inline void yield() { std::this_thread::yield(); }

#ifndef pgm_read_byte_near
#define pgm_read_byte_near(addr) (*reinterpret_cast<const unsigned char*>(addr))
#endif

// PubSubClient's elapsed-time counters use Arduino's 32-bit wrap.
inline unsigned long millis() {
  return static_cast<uint32_t>(awtrix::monotonicMs());
}

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <esp_timer.h>
#include <cstdio>
#include <ctime>
#include <random>
#include <string.h>
#include <thread>

typedef unsigned char boolean;
inline void yield() { std::this_thread::yield(); }

// No PROGMEM on the host, so the font and lookup tables are read with a plain dereference.
#ifndef pgm_read_byte_near
#define pgm_read_byte_near(addr) (*reinterpret_cast<const unsigned char*>(addr))
#endif

#if defined(_WIN32)
inline struct tm* localtime_r(const time_t* t, struct tm* out) {
  *out = *std::localtime(t);
  return out;
}
#endif

// Truncated to 32 bits on purpose: the simulator has to wrap after ~49.7 days like the device does.
inline unsigned long millis() {
  return static_cast<unsigned long>((esp_timer_get_time() / 1000) & 0xffffffffLL);
}

inline void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline long random(long minv, long maxv) {
  static std::mt19937 rng{std::random_device{}()};
  if (maxv <= minv) return minv;
  return minv + static_cast<long>(rng() % static_cast<unsigned long>(maxv - minv));
}
inline long random(long maxv) { return random(0, maxv); }

struct SimSerial {
  void begin(unsigned long) {}
  void println(const char* s) {
    std::printf("%s\n", s);
    std::fflush(stdout);
  }
  void println() { println(""); }
  void print(const char* s) {
    std::printf("%s", s);
    std::fflush(stdout);
  }
};
inline SimSerial Serial;

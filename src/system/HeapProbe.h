#pragma once

#include <cstddef>

namespace awtrix {
namespace probe {

struct Watch {
  std::size_t lowWater = 0;
  std::size_t maxAlloc = 0;
  std::size_t bytes = 0;
  std::size_t count = 0;
};

#ifdef AWTRIX_HEAP_PROBE

void begin();

void report(const char* label, std::size_t minBytes);

std::size_t bytesAllocated();
std::size_t allocations();
long peakLiveBytes();

void note(const char* label, std::size_t a, std::size_t b, std::size_t c);

void watchBegin();
Watch watchPeek();
Watch watchEnd();

#else

inline void begin() {}
inline void report(const char*, std::size_t) {}
inline void note(const char*, std::size_t, std::size_t, std::size_t) {}
inline std::size_t bytesAllocated() { return 0; }
inline std::size_t allocations() { return 0; }
inline long peakLiveBytes() { return 0; }
inline void watchBegin() {}
inline Watch watchPeek() { return {}; }
inline Watch watchEnd() { return {}; }

#endif

}
}

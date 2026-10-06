#pragma once
#include <cstdint>
#include "core/render/FrameDiagnostics.h"

namespace awtrix::displayprobe {
using Phase = render::FramePhase;
#ifdef AWTRIX_HEAP_PROBE
uint32_t start();
void record(Phase phase, uint32_t startUs);
void rendered();
void presented();
void report(int width, int height);
#else
inline uint32_t start() { return 0; }
inline void record(Phase, uint32_t) {}
inline void rendered() {}
inline void presented() {}
inline void report(int, int) {}
#endif
}

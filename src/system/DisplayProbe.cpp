#include "system/DisplayProbe.h"

#ifdef AWTRIX_HEAP_PROBE
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "system/Log.h"

namespace awtrix::displayprobe {
namespace {
render::FrameDiagnostics g_diagnostics;
int64_t g_lastReportUs = 0;
}

uint32_t start() { return micros(); }
void record(Phase phase, uint32_t startUs) { g_diagnostics.record(phase, micros() - startUs); }
void rendered() { g_diagnostics.rendered(esp_timer_get_time()); }
void presented() { g_diagnostics.presented(esp_timer_get_time()); }

void report(int width, int height) {
  const int64_t now = esp_timer_get_time();
  if (!g_lastReportUs) { g_lastReportUs = now; g_diagnostics = {}; return; }
  if (now - g_lastReportUs < 5000000) return;
  const uint32_t renderRate = g_diagnostics.renderFpsMilli();
  const uint32_t outputRate = g_diagnostics.presentationFpsMilli();
  logf("display-probe %dx%d dt_us=%lu render=%lu.%03lu present=%lu.%03lu",
       width, height, static_cast<unsigned long>(now - g_lastReportUs),
       static_cast<unsigned long>(renderRate / 1000), static_cast<unsigned long>(renderRate % 1000),
       static_cast<unsigned long>(outputRate / 1000), static_cast<unsigned long>(outputRate % 1000));
  logf("display-probe heap internal=%u largest=%u psram=%u",
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
       static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  static const char* const names[] = {"services", "tick", "render", "mapping", "output", "interval"};
  for (unsigned i = 0; i < static_cast<unsigned>(Phase::Count); ++i) {
    const auto sample = g_diagnostics.snapshot(static_cast<Phase>(i));
    logf("display-probe %s n=%lu us(mean/p50/p95/p99/max)=%lu/%lu/%lu/%lu/%lu",
         names[i], static_cast<unsigned long>(sample.count), static_cast<unsigned long>(sample.meanUs),
         static_cast<unsigned long>(sample.p50Us), static_cast<unsigned long>(sample.p95Us),
         static_cast<unsigned long>(sample.p99Us), static_cast<unsigned long>(sample.maxUs));
  }
  g_diagnostics = {};
  g_lastReportUs = esp_timer_get_time();
}
}
#endif

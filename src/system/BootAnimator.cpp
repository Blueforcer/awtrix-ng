#include "system/BootAnimator.h"

#include <Arduino.h>

#include "core/render/BootScreen.h"
#include "hal/IBoard.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix {

namespace {
constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = 2;
constexpr BaseType_t kCore = 1;
constexpr uint32_t kFrameMs = 33;
constexpr int64_t kStopWaitMs = 500;
}

// Keeps the logo moving while setup() is blocked on Wi-Fi and the filesystem. It owns the canvas
// and the panel for its whole lifetime, so nothing else may draw until stop() has returned.
void BootAnimator::start(IBoard& board, Canvas& canvas, const GfxFont& font, int64_t startMs) {
  if (running_.load()) return;
  board_ = &board;
  canvas_ = &canvas;
  font_ = &font;
  startMs_ = startMs;
  running_.store(true);
  exited_.store(false);
  if (xTaskCreatePinnedToCore(taskEntry, "bootanim", kStackBytes, this, kPriority, nullptr,
                              kCore) != pdPASS) {
    running_.store(false);
    exited_.store(true);
    logf("boot: animation task could not be created");
  }
}

// Blocks until the task has actually left run(), because the caller takes the canvas back the
// moment this returns. Gives up after kStopWaitMs so a wedged task cannot hang the boot.
void BootAnimator::stop() {
  if (!running_.load()) return;
  running_.store(false);
  const int64_t deadline = monotonicMs() + kStopWaitMs;
  while (!exited_.load() && monotonicMs() < deadline) delay(1);
  if (!exited_.load()) logf("boot: animation task did not stop within %d ms", (int)kStopWaitMs);
}

void BootAnimator::taskEntry(void* self) { static_cast<BootAnimator*>(self)->run(); }

void BootAnimator::run() {
  TickType_t last = xTaskGetTickCount();
  while (running_.load()) {
    render::drawBootLogo(*canvas_, *font_, startMs_, monotonicMs());
    board_->show(*canvas_);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(kFrameMs));
  }
  exited_.store(true);
  vTaskDelete(nullptr);
}

}

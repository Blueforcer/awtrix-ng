#pragma once

#include <atomic>
#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {

class IBoard;

class BootAnimator {
 public:
  void start(IBoard& board, Canvas& canvas, const GfxFont& font, int64_t startMs);
  void stop();

 private:
  static void taskEntry(void* self);
  void run();

  IBoard* board_ = nullptr;
  Canvas* canvas_ = nullptr;
  const GfxFont* font_ = nullptr;
  int64_t startMs_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<bool> exited_{true};
};

}

#pragma once

#include <cstdint>

#include "core/Command.h"
#include "core/payload/ScrollSpec.h"
#include "core/render/Canvas.h"

namespace awtrix {

struct PageFrameContext {
  int64_t nowMs = 0;
  uint32_t defaultColor = 0xFFFFFF;
  int repeat = 0;
  ScrollDefaults scrollDefaults;
  bool parkAfterPasses = true;
  bool uppercase = false;
};

struct PageFrameResult {
  bool wantsMoreTime = false;
  bool passesDone = false;
  uint64_t revision = 0;
  bool hasOverlay = false;
};

// Prepared content supplied by a registered payload extension. Used on the render thread.
class PageContent {
 public:
  virtual ~PageContent() = default;
  virtual bool prepare(DispatchDetail& error) = 0;
  virtual PageFrameResult draw(Canvas& canvas, const PageFrameContext& frame) = 0;
  virtual void restart() = 0;
  virtual void invalidateAssets() = 0;
  virtual void inheritState(PageContent& previous) = 0;
  virtual const void* type() const = 0;
  virtual bool repeats() const = 0;
  virtual uint64_t revision() const = 0;
};

}

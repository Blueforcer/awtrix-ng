#pragma once

#include <cstdint>

#include <memory>
#include <string>

#include "core/apps/AppRegistry.h"
#include "core/apps/IApp.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/AppSpec.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "core/render/ScrollController.h"

namespace awtrix {

class CoreEngine;

class IPageIcon {
 public:
  virtual ~IPageIcon() = default;
  virtual bool begin(const std::string& iconId) = 0;
  virtual void clear() = 0;
  virtual void advance(int64_t nowMs) = 0;
  virtual void blit(Canvas& dst, int xOffset) const = 0;
  virtual int width() const = 0;
};

class IPageSound {
 public:
  virtual ~IPageSound() = default;
  virtual void play(const AppSpec& spec) = 0;
  virtual bool isPlaying() const = 0;
};

class IPageClock {
 public:
  virtual ~IPageClock() = default;
  virtual void fill(RenderCtx& ctx, int64_t nowMs) = 0;
};

// Everything the pipeline needs from the platform layer. None of these pointers are owned, and
// all of them must outlive the pipeline.
struct RenderPipelineDeps {
  CoreEngine* engine = nullptr;
  AppRegistry* apps = nullptr;
  EffectRegistry* effects = nullptr;
  EffectRegistry* overlays = nullptr;
  const GfxFont* fonts[kFontCount] = {nullptr, nullptr};
  IPageIcon* icons = nullptr;
  IPageIcon* iconsB = nullptr;
  IPageSound* sound = nullptr;
  IPageClock* clock = nullptr;
};

class RenderPipeline {
 public:
  RenderPipeline(int width, int height, const RenderPipelineDeps& deps);

  void renderFrame(Canvas& out, int64_t nowMs);

  float textX() const { return slotA_.scroll.x(); }
  const std::string& currentPageId() const { return lastRenderId_; }

 private:
  // Per-page state that has to survive between frames. slotA_ is whatever is on screen, slotB_
  // the page being transitioned in; the two are swapped when that page takes over.
  struct PageSlot {
    IPageIcon* icon = nullptr;
    std::string pageId;
    std::string iconId;
    bool valid = false;
    int64_t retryAtMs = 0;
    render::ScrollController scroll;
    bool iconPushed = false;
  };

  void renderPage(Canvas& dst, const std::string& id, int64_t nowMs, bool isNotif, PageSlot* slot);
  void drawIndicators(Canvas& out, int64_t nowMs) const;
  void onPageChanged(int64_t nowMs, bool isNotif);
  void refreshPageContent(int64_t nowMs, bool isNotif);
  const GfxFont& fontFor(const AppSpec* spec) const;

  render::ScrollLayout scrollLayoutFor(const AppSpec* spec, int canvasWidth,
                                       bool iconReservesColumn) const;
  void applyScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs);
  void advanceScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs);
  void loadIcon(PageSlot& slot, const std::string& pageId, const AppSpec* spec, int64_t nowMs);
  bool iconIsFullScreen(const PageSlot* slot, int canvasWidth) const;
  const AppSpec* pageSpec(const std::string& id, bool isNotif) const;
  int iconShift(const AppSpec& spec, const PageSlot& slot) const;

  RenderPipelineDeps d_;
  int width_, height_;
  std::unique_ptr<Canvas> transA_, transB_;
  std::string lastRenderId_;
  PageSlot slotA_, slotB_;
};

}

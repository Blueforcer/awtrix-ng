#pragma once

#include <atomic>
#include <cstdint>

#include <memory>
#include <string>

#include "core/apps/AppRegistry.h"
#include "core/apps/IApp.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/AppSpec.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "core/render/FontCatalog.h"
#include "core/render/PageInfo.h"
#include "core/render/PageContent.h"
#include "core/render/PageIcon.h"
#include "core/render/PageZoom.h"
#include "core/render/ScrollController.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {

class CoreEngine;

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
  // Required: every font a page, layout or script may name.
  const FontCatalog* fonts = nullptr;
  IPageIcon* icons = nullptr;
  IPageIcon* iconsB = nullptr;
  sound::AudioRouter* audio = nullptr;
  IPageClock* clock = nullptr;
  IExternalPage* external = nullptr;
  IPageZoom* zoom = nullptr;
};

class RenderPipeline {
 public:
  RenderPipeline(int width, int height, const RenderPipelineDeps& deps);

  void renderFrame(Canvas& out, int64_t nowMs);
  // For frames the panel spends on something else: the page then counts as shown anew once
  // renderFrame runs again.
  void skipFrame() { skipped_ = true; }
  bool prepareFrames();

  float textX() const { return slotA_.scroll.x(); }
  const std::string& currentPageId() const { return lastRenderId_; }
  const PageInfo& shownPage() const { return shown_; }
  void invalidateIcons();

 private:
  struct PlacedIcon {
    std::unique_ptr<IPageIcon> player;
    std::string iconId;
    int x = 0;
    int y = 0;
    bool valid = false;
    bool missing = false;
    int64_t retryAtMs = 0;
  };

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
    std::unique_ptr<PlacedIcon[]> placedIcons;
    std::size_t placedIconCount = 0;
    int64_t placedRetryAtMs = 0;
    bool missing = false;
    uint32_t iconGeneration = 0;
    bool iconsPending = false;
    PageFrameResult contentFrame;
    bool enlarged = false;
  };

  PageKind pageKind() const;
  std::string pageId(PageKind kind) const;
  void renderPage(Canvas& dst, const std::string& id, int64_t nowMs, PageKind kind, PageSlot* slot);
  void drawIndicators(Canvas& out, int64_t nowMs) const;
  void onPageChanged(int64_t nowMs, PageKind kind);
  void playPageSound(const AppSpec& spec);
  void refreshPageContent(int64_t nowMs, PageKind kind);
  const FontEntry& fontFor(const AppSpec* spec) const;

  render::ScrollLayout scrollLayoutFor(const AppSpec* spec, int canvasWidth, int column) const;
  void applyScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs);
  void advanceScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs, int parkAfter);
  int scrollParkAfter(const AppSpec* spec, PageKind kind) const;
  void loadIcon(PageSlot& slot, const std::string& pageId, const AppSpec* spec, int64_t nowMs);
  void loadPlacedIcons(PageSlot& slot, const AppSpec* spec, int64_t nowMs);
  void advanceIcons(PageSlot& slot, int64_t nowMs);
  bool iconIsFullScreen(const PageSlot* slot, int canvasWidth) const;
  int iconColumn(const AppSpec& spec, const PageSlot* slot) const;
  bool enlargeFor(const AppSpec* spec, uint32_t assets) const;
  int pageWidth(const PageSlot& slot) const {
    return slot.enlarged ? d_.zoom->stage().width() : width_;
  }
  int pageHeight(const PageSlot& slot) const {
    return slot.enlarged ? d_.zoom->stage().height() : height_;
  }
  const AppSpec* pageSpec(const std::string& id, PageKind kind) const;
  int iconShift(const AppSpec& spec, const PageSlot& slot) const;

  RenderPipelineDeps d_;
  int width_, height_;
  std::unique_ptr<Canvas> transA_, transB_;
  std::string lastRenderId_;
  PageInfo shown_;
  int64_t shownSinceMs_ = -1;
  bool skipped_ = false;
  std::atomic<uint32_t> iconGeneration_{0};
  bool iconLoadedThisFrame_ = false;
  // The alert the notification on screen started and that repeats until it leaves; 0 for none.
  uint32_t alertRepeat_ = 0;
  PageSlot slotA_, slotB_;
};

}

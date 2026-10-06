#pragma once

#include <cstdint>
#include <string>

#include "core/apps/IApp.h"

namespace awtrix {

// The TC002's own clock for its 52 x 16 panel. It registers under the id "Time" and so takes the
// place of the shared TimeApp; Settings::clockFace picks one of five faces.
class Tc002ClockApp : public IApp {
 public:
  const std::string& id() const override { return id_; }
  void render(Canvas& canvas, const RenderCtx& ctx) override;

 private:
  int64_t tearSince(const RenderCtx& ctx);
  void trackFlap(const RenderCtx& ctx);
  void cacheText(const RenderCtx& ctx);

  std::string id_ = "Time";
  int64_t shownSinceMs_ = -1;
  int64_t tearStartMs_ = -1;
  int day_ = -1;
  int flapShown_ = -1, flapFrom_ = -1;
  int64_t flapSeenMs_ = -1;
  const FontCatalog* catalog_ = nullptr;
  const GfxFont* chunky_ = nullptr;
  int smallCellWidth_ = 0, chunkyCellWidth_ = 0;
  int textHour_ = -1, textMinute_ = -1, textSecond_ = -1, textFlapFrom_ = -1;
  bool text24h_ = false, textLeadingZero_ = false, textSeconds_ = false;
  std::string timeText_, previousText_;
};

}

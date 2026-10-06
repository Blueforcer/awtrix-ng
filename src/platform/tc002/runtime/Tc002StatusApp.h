#pragma once

#include <cstdint>
#include <string>

#include "core/apps/IApp.h"
#include "platform/tc002/runtime/Tc002Paint.h"

namespace awtrix {

// The TC002 status page: battery and Wi-Fi signal in the top row, the address below. A change seen
// between two consecutive frames animates; every frame is a function of the runtime state, the
// frame time and the times of those changes. After a gap in the frames - the page was away, under a
// notification or on a switched-off panel - it starts from the current state without replaying.
class Tc002StatusApp : public IApp {
 public:
  const std::string& id() const override { return id_; }
  void render(Canvas& canvas, const RenderCtx& ctx) override;

 private:
  static constexpr int64_t kNever = INT64_MIN / 4;
  static constexpr int kEffects = 4;
  using Fx = paint::ParticleKind;

  void observe(const RuntimeState& rt, int64_t now);
  float batteryValue(int64_t now) const;
  void drawBattery(Canvas& c, const GfxFont& font, bool low, int64_t now) const;
  void drawBolt(Canvas& c, int64_t now) const;
  void drawSignal(Canvas& c, int64_t now) const;
  void drawEffects(Canvas& c, int64_t now) const;
  void drawAddress(Canvas& c, const GfxFont& font, int64_t now) const;
  void drawMessage(Canvas& c, const GfxFont& font, int64_t now) const;

  std::string id_ = "Status";
  int64_t lastFrameAt_ = kNever;
  float batteryFrom_ = 0.0f;
  int battery_ = -1;
  int64_t batteryAt_ = kNever;
  bool power_ = false;
  int64_t powerAt_ = kNever;
  int bars_ = 0, barsFrom_ = 0;
  int64_t barsAt_ = kNever;
  net::LinkPhase phase_ = net::LinkPhase::Offline;
  std::string address_;
  int64_t textAt_ = kNever;
  paint::ParticleRing<kEffects> effects_;
};

}

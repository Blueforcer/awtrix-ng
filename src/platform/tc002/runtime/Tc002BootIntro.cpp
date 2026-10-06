#include "platform/tc002/runtime/Tc002BootIntro.h"

#include <algorithm>

#include "platform/tc002/runtime/BootInfoWide.h"
#include "platform/tc002/runtime/BootIntroWide.h"

namespace awtrix {

void Tc002BootIntro::start(int64_t originMs, bool synchronized) {
  originMs_ = originMs;
  synchronized_ = synchronized;
  phase_ = Phase::Intro;
}

bool Tc002BootIntro::draw(Canvas& canvas, const GfxFont& font, int64_t nowMs) {
  if (phase_ == Phase::Done) return false;
  if (firstMs_ < 0) firstMs_ = nowMs;
  if (phase_ == Phase::Waiting) {
    int64_t audibleAtMs = -1;
    const BootSoundClock::Start sound = sound_ ? sound_->poll(audibleAtMs) : BootSoundClock::Start::Failed;
    if (sound == BootSoundClock::Start::Audible) {
      start(audibleAtMs, true);
    } else if (sound == BootSoundClock::Start::Failed) {
      start(sound_ ? nowMs : firstMs_, false);
    } else if (nowMs - firstMs_ >= kSoundWaitMs) {
      sound_->cancel();
      start(nowMs, false);
    } else {
      render::drawBootIntroWide(canvas, font, nowMs, nowMs);
      return true;
    }
  }
  if (phase_ == Phase::Intro) {
    if (nowMs < originMs_ + render::kBootIntroWideMs) {
      render::drawBootIntroWide(canvas, font, originMs_, std::max(nowMs, originMs_));
      return true;
    }
    info_ = {version_, address_ ? address_() : std::string()};
    infoMs_ = nowMs;
    phase_ = Phase::Info;
  }
  if (render::drawBootInfoWide(canvas, info_, infoMs_, nowMs)) return true;
  phase_ = Phase::Done;
  return false;
}

}

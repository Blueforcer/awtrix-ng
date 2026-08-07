#pragma once

#include <cstdint>
#include <string>

#include "core/CoreEngine.h"
#include "core/Services.h"

namespace awtrix {
namespace sim {

// No audio output on the host: a clip just flips the runtime state on and back off after a fixed
// fake duration, which is all the web UI and the e2e tests observe.
class FakeClipService : public IClipService {
 public:
  explicit FakeClipService(CoreEngine& engine) : engine_(engine) {}

  bool playClip(const std::string& path) override {
    std::string name = path;
    if (name.size() > 12) name = name.substr(8, name.size() - 12);
    engine_.state().runtime().clipPlaying = true;
    engine_.state().runtime().clipName = name;
    engine_.state().emit(StateEvent::RadioChanged);
    endsAtMs_ = 0;
    running_ = true;
    return true;
  }

  void stopClip() override { finish(); }
  bool clipPlaying() const override { return running_; }

  void tick(int64_t nowMs) {
    if (!running_) return;
    if (endsAtMs_ == 0) {
      endsAtMs_ = nowMs + kClipDurationMs;
      return;
    }
    if (nowMs >= endsAtMs_) finish();
  }

 private:
  static constexpr long kClipDurationMs = 1500;

  void finish() {
    if (!running_) return;
    running_ = false;
    endsAtMs_ = 0;
    engine_.state().runtime().clipPlaying = false;
    engine_.state().runtime().clipName.clear();
    engine_.state().emit(StateEvent::RadioChanged);
  }

  CoreEngine& engine_;
  bool running_ = false;
  int64_t endsAtMs_ = 0;
};

}
}

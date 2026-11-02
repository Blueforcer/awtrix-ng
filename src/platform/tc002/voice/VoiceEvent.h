#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "platform/tc002/voice/ClipStore.h"
#include "platform/tc002/voice/VoiceStatus.h"

namespace awtrix::tc002::voice {

struct VoiceEvent {
  enum class Kind { Record, Play, Send, Fetch };

  std::string app;
  Kind kind = Kind::Record;
  int number = 0, status = 0;
  ClipId clip = 0;
  uint32_t ms = 0;
  std::string format;
  std::optional<std::string> body;

  static VoiceEvent outcome(std::string app, Kind kind, int number, int status) {
    VoiceEvent event;
    event.app = std::move(app);
    event.kind = kind;
    event.number = number;
    event.status = status;
    return event;
  }
  static VoiceEvent ofClip(std::string app, Kind kind, int number, ClipId clip, uint32_t ms, std::string format) {
    VoiceEvent event = outcome(std::move(app), kind, number, kOk);
    event.clip = clip;
    event.ms = ms;
    event.format = std::move(format);
    return event;
  }
  bool transfer() const { return kind == Kind::Send || kind == Kind::Fetch; }
};

}

#include "platform/tc002/voice/VoiceMqtt.h"

#include "core/api/ApiRouter.h"

namespace awtrix::tc002::voice {
bool handleMqtt(const std::string& topic, const std::function<bool()>& start, std::string& result) {
  if (topic != kStartTopic) return false;
  DispatchDetail detail;
  detail.message = "voice not ready";
  result = api::mqttResult(start() ? DispatchResult::Ok : DispatchResult::Unavailable, detail);
  return true;
}
}  // namespace awtrix::tc002::voice

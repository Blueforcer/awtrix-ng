#pragma once
#include <functional>
#include <string>
#include <vector>

#include "platform/tc002/voice/VoiceRuntime.h"
namespace httplib {
struct Request;
struct Response;
}  // namespace httplib
namespace awtrix::tc002::voice {
// GET and POST /api/v1/voice and POST /api/v1/voice/start, over Home Assistant Voice and the assistant.
class VoiceApi {
 public:
  struct Hooks {
    std::function<std::vector<std::string>()> assistantApps;
    std::function<bool()> startAssistant;
    std::function<bool()> wakeListening;
    std::function<const char*()> appProblem;
  };
  VoiceApi(VoiceRuntime& runtime, bool wakeWordAvailable, Hooks hooks);
  bool handle(const httplib::Request& request, httplib::Response& response);

 private:
  std::string status() const;
  VoiceRuntime& runtime_;
  const bool wakeWordAvailable_;
  const Hooks hooks_;
};
}  // namespace awtrix::tc002::voice

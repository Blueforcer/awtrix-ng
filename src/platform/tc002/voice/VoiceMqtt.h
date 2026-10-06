#pragma once
#include <functional>
#include <string>

#include "core/mqtt/HaDiscovery.h"

namespace awtrix::tc002::voice {
// Starts a voice request like holding the knob; Home Assistant shows it as a button.
inline constexpr char kStartTopic[] = "cmd/voice/start";
inline constexpr ha::Entity kStartButton{
    "assist",
    R"J("p":"button","name":"Assist","ic":"mdi:microphone","cmd_t":"~/cmd/voice/start","pl_prs":"{}")J"};

// Answers kStartTopic with its MQTT result, false for any other topic. start says whether a
// request began.
bool handleMqtt(const std::string& topic, const std::function<bool()>& start, std::string& result);
}  // namespace awtrix::tc002::voice

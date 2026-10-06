#pragma once

#include <string>

namespace awtrix::input {

// The buttonCallback body. The buttons are named by position, whatever rotate and swapButtons do.
constexpr const char* kWebhookButtonNames[3] = {"left", "middle", "right"};
constexpr const char* kWebhookKnobName = "knob";

std::string webhookPressBody(const char* control, bool state, const std::string& uid);
// turn counts knob detents, clockwise positive.
std::string webhookTurnBody(int turn, const std::string& uid);

}

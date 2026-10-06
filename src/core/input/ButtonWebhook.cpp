#include "core/input/ButtonWebhook.h"

namespace awtrix::input {

std::string webhookPressBody(const char* control, bool state, const std::string& uid) {
  return std::string("{\"button\":\"") + control + "\",\"state\":" + (state ? "true" : "false") +
         ",\"uid\":\"" + uid + "\"}";
}

std::string webhookTurnBody(int turn, const std::string& uid) {
  return std::string("{\"button\":\"") + kWebhookKnobName + "\",\"turn\":" + std::to_string(turn) +
         ",\"uid\":\"" + uid + "\"}";
}

}

#pragma once

#include <string_view>

#include "platform/tc002/speech/SpeechTypes.h"

namespace awtrix::speech {

// English text, UTF-8, as a plan. Letters lose their accents, numbers, times and the signs %, &
// and ° become words, punctuation becomes pauses, and any other character separates words. A text
// longer than a plan holds ends with the last word that fits. False when no word is left to say.
bool prepare(std::string_view text, Plan& out);

}

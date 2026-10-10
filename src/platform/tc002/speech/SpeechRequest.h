#pragma once

#include <cstddef>
#include <string_view>

#include "core/Command.h"
#include "platform/tc002/speech/SpeechVoice.h"

namespace awtrix::speech {

constexpr std::size_t kMaxTextBytes = 512;

// The text of a `speech` sound, read into the voice's plan. On failure detail says what is wrong
// with it; the caller names the field.
bool readText(const SpeechVoice& voice, std::string_view text, Plan& out, DispatchDetail& detail);

}

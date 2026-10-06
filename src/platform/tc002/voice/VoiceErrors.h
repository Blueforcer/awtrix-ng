#pragma once

namespace awtrix::tc002::voice::errors {
#define AWTRIX_VOICE_ERROR(name) inline constexpr char name[] = #name;
#include "VoiceErrors.def"
#undef AWTRIX_VOICE_ERROR
}

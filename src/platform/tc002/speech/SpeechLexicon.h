#pragma once

#include "platform/tc002/speech/SpeechPronunciation.h"

namespace awtrix::speech::detail {
// A binary search over the blocks, then at most 32 front-coded keys. No heap. opensSentence picks
// the verb where a word that opens a sentence is more likely a command.
bool englishPronunciation(std::string_view word, bool opensSentence, Pronunciation& out);
}

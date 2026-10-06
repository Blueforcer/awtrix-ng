#pragma once

#include <string_view>

#include "platform/tc002/speech/SpeechTypes.h"

namespace awtrix::speech::detail {

constexpr std::size_t kMaxWordPhones = 64;
struct Pronunciation {
  std::array<Phone, kMaxWordPhones> phones{};
  std::size_t count = 0;
};

// A lower-case word as phones with lexical stress: from the lexicon, else by spelling rules.
// opensSentence is true for a sentence's first word. False for a word of more than kMaxWordPhones
// phones.
bool pronounce(std::string_view word, bool opensSentence, Pronunciation& out);

}

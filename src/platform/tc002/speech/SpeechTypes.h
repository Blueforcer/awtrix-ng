#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix::speech {

constexpr std::size_t kMaxPhones = 1536;

// What the text frontend hands a voice. A voice is trained on these values, so a new phone goes
// at the end. A diphthong is one phone, not two.
enum class PhoneId : uint8_t {
  Silence, Ae, Aa, Eh, Ih, Iy, Ao, Uh, Uw, Ah, Schwa, Er,
  Ai, Au, Oi, Ou, Ei,
  P, B, T, D, K, G, Ch, Jh, F, V, Th, Dh, S, Z, Sh, Zh, H,
  M, N, Ng, L, R, W, J
};
// A vowel's lexical stress in the low two bits, as the lexicon gives it; WordEnd on the last
// phone of a word.
enum Prosody : uint8_t {
  Primary = 1, Secondary = 2, WordEnd = 4
};
struct Phone {
  PhoneId sound = PhoneId::Silence;
  uint8_t prosody = 0;
};
// Phones with a pause after every phrase; boundaries holds the punctuation that ended it
// ('.', ',', '?', '!', ';' or ':', 0 for none) at the pause's index. Timing and intonation are
// the voice's to choose.
struct Plan {
  std::array<Phone, kMaxPhones> phones{};
  std::array<char, kMaxPhones> boundaries{};
  std::size_t count = 0;
};
constexpr bool isVowel(PhoneId sound) {
  return sound >= PhoneId::Ae && sound <= PhoneId::Ei;
}

}

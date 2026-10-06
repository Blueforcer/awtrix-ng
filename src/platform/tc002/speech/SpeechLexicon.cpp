#include "platform/tc002/speech/SpeechLexicon.h"
#include "platform/tc002/speech/SpeechLexiconData.h"

#include <algorithm>
#include <cstring>

namespace awtrix::speech::detail {
namespace {
using P = PhoneId;
// The lexicon's phone numbers: ARPAbet in alphabetical order.
constexpr P kPhone[] = {
    P::Aa, P::Ae, P::Ah, P::Ao, P::Au, P::Ai, P::B, P::Ch, P::D, P::Dh,
    P::Eh, P::Er, P::Ei, P::F, P::G, P::H, P::Ih, P::Iy, P::Jh, P::K,
    P::L, P::M, P::N, P::Ng, P::Ou, P::Oi, P::P, P::R, P::S, P::Sh,
    P::T, P::Th, P::Uh, P::Uw, P::V, P::W, P::J, P::Z, P::Zh};
uint32_t word32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
const uint8_t* block(unsigned index) {
  const unsigned blocks = word32(kEnglishLexicon + 12);
  return kEnglishLexicon + 16 + blocks * 4 + word32(kEnglishLexicon + 16 + index * 4);
}
std::string_view key(const uint8_t*& p, std::array<char, 64>& scratch) {
  const unsigned lengths = *p++;
  unsigned prefix = lengths >> 4, suffix = lengths & 15;
  if (prefix == 15) prefix = *p++;
  if (suffix == 15) suffix = *p++;
  std::memcpy(scratch.data() + prefix, p, suffix);
  p += suffix;
  return {scratch.data(), prefix + suffix};
}
unsigned bits(const uint8_t* p, unsigned& used, unsigned n) {
  unsigned value = 0;
  for (unsigned i = 0; i < n; ++i, ++used)
    value |= ((p[used / 8] >> (used % 8)) & 1u) << i;
  return value;
}
void pronunciation(const uint8_t*& p, Pronunciation* out) {
  const unsigned count = *p++;
  unsigned used = 0;
  for (unsigned i = 0; i < count; ++i) {
    const unsigned id = bits(p, used, 6);
    P phone = kPhone[id];
    unsigned stress = 0;
    if (isVowel(phone)) stress = bits(p, used, 2);
    if (phone == P::Ah && stress == 0) phone = P::Schwa;
    if (out) out->phones[i] = {phone, static_cast<uint8_t>(stress)};
  }
  p += (used + 7) / 8;
  if (out) out->count = count;
}
// CMUdict's order says nothing about which reading is common. These pick the weather's wind, the
// present tense of live, read and lead, a weak "can", perfect as the adjective, excuse as the verb,
// and close and use as verbs at the start of a sentence.
unsigned preferredVariant(std::string_view word, bool opensSentence) {
  if (opensSentence && (word == "close" || word == "use")) return 1;
  return word == "wind" || word == "live" || word == "read" || word == "lead" || word == "can" ||
                 word == "perfect" || word == "excuse"
             ? 1
             : 0;
}
}

bool englishPronunciation(std::string_view word, bool opensSentence, Pronunciation& out) {
  out.count = 0;
  if (word.empty() || word.size() >= 64) return false;
  const unsigned blocks = word32(kEnglishLexicon + 12);
  unsigned lower = 0, upper = blocks;
  std::array<char, 64> scratch{};
  while (lower < upper) {
    const unsigned middle = lower + (upper - lower) / 2;
    auto p = block(middle);
    if (key(p, scratch) <= word) lower = middle + 1;
    else upper = middle;
  }
  if (!lower) return false;
  auto p = block(lower - 1);
  scratch.fill(0);
  const unsigned totalWords = word32(kEnglishLexicon + 8);
  const unsigned count = std::min(32u, totalWords - (lower - 1) * 32);
  for (unsigned i = 0; i < count; ++i) {
    const auto candidate = key(p, scratch);
    if (candidate > word) return false;
    const unsigned variants = *p++;
    const bool found = candidate == word;
    const unsigned chosen = std::min(preferredVariant(word, opensSentence), variants - 1);
    for (unsigned variant = 0; variant < variants; ++variant)
      pronunciation(p, found && variant == chosen ? &out : nullptr);
    if (found) return true;
  }
  return false;
}
}

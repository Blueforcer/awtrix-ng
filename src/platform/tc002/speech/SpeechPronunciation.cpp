#include "platform/tc002/speech/SpeechPronunciation.h"
#include "platform/tc002/speech/SpeechLexicon.h"

#include <cstring>

namespace awtrix::speech::detail {
namespace {
using P = PhoneId;

bool writtenVowel(char c) { return c != '\0' && std::strchr("aeiouy", c) != nullptr; }

// The spelling rules' notation, one character per phone.
P symbol(char c) {
  switch (c) {
    case 'a': return P::Ae; case 'A': return P::Aa;
    case 'E': return P::Eh; case 'e': return P::Ei;
    case 'I': return P::Ih; case 'i': return P::Iy;
    case 'O': return P::Ao; case 'o': return P::Ou;
    case 'u': return P::Uw; case 'U': return P::Ah;
    case '@': return P::Schwa; case 'R': return P::Er;
    case '1': return P::Ai; case '2': return P::Au; case '3': return P::Oi;
    case 'p': return P::P; case 'b': return P::B;
    case 't': return P::T; case 'd': return P::D;
    case 'k': return P::K; case 'g': return P::G;
    case 'C': return P::Ch; case 'J': return P::Jh;
    case 'f': return P::F; case 'v': return P::V;
    case 'T': return P::Th; case 'D': return P::Dh;
    case 's': return P::S; case 'z': return P::Z;
    case 'S': return P::Sh; case 'Z': return P::Zh;
    case 'h': return P::H; case 'm': return P::M;
    case 'n': return P::N; case 'N': return P::Ng;
    case 'l': return P::L; case 'r': return P::R;
    case 'w': return P::W; case 'j': return P::J;
    default: return P::Silence;
  }
}

class Rules {
 public:
  explicit Rules(Pronunciation& out) : out_(out) {}
  bool append(std::string_view notation) {
    for (std::size_t i = 0; i < notation.size(); ++i) {
      char c = notation[i];
      if (i + 1 < notation.size()) {
        const auto pair = notation.substr(i, 2);
        if (pair == "Ai") { c = '1'; ++i; }
        else if (pair == "Au") { c = '2'; ++i; }
        else if (pair == "Oi") { c = '3'; ++i; }
      }
      if (out_.count == out_.phones.size()) return false;
      out_.phones[out_.count++] = {symbol(c), 0};
    }
    return true;
  }
 private:
  Pronunciation& out_;
};

bool spelling(std::string_view word, Pronunciation& out) {
  Rules rules(out);
  auto at = [&](std::size_t i) { return i < word.size() ? word[i] : '\0'; };
  for (std::size_t i = 0; i < word.size();) {
    const char c = word[i];
    bool okay = true;
    auto match = [&](std::string_view graphemes, std::string_view phones) {
      if (word.substr(i, graphemes.size()) != graphemes) return false;
      okay = rules.append(phones);
      i += graphemes.size();
      return true;
    };
    if ((i == 0 && (match("kn", "n") || match("wr", "r"))) ||
        match("tion", "S@n") || match("sion", "Z@n") || match("tch", "C") ||
        match("ch", "C") || match("sh", "S") || match("th", "T") ||
        match("ng", "N") || match("ph", "f") || match("qu", "kw") ||
        match("ee", "i") || match("ea", "i") || match("oo", "u") ||
        match("ai", "e") || match("ay", "e") || match("oa", "o") ||
        match("ou", "Au") || match("ow", "Au") || match("oi", "Oi") ||
        match("oy", "Oi") || match("er", "R") || match("ir", "R") ||
        match("ur", "R") || match("ar", "Ar") || match("or", "Or")) {
      if (!okay) return false;
      continue;
    }
    const bool magic = i + 2 < word.size() && !writtenVowel(at(i + 1)) && at(i + 2) == 'e' &&
                       (i + 3 == word.size() || (i + 4 == word.size() && at(i + 3) == 's'));
    std::string_view phones;
    char consonant[1] = {c};
    switch (c) {
      case 'a': phones = magic ? "e" : "a"; break;
      case 'e': phones = i + 1 != word.size() || word.size() <= 2 ? "E" : ""; break;
      case 'i': phones = magic ? "Ai" : "I"; break;
      case 'o': phones = magic || i + 1 == word.size() ? "o" : "A"; break;
      case 'u': phones = magic ? "ju" : "U"; break;
      case 'y': phones = i + 1 == word.size() ? (word.size() <= 3 ? "Ai" : "i") : "j"; break;
      case 'c': phones = at(i + 1) == 'e' || at(i + 1) == 'i' || at(i + 1) == 'y' ? "s" : "k"; break;
      case 'g': phones = at(i + 1) == 'e' || at(i + 1) == 'i' ? "J" : "g"; break;
      case 'j': phones = "J"; break;
      case 'q': phones = "kju"; break;
      case 'x': phones = "ks"; break;
      case 's': phones = i + 1 == word.size() && i && std::strchr("aeioulmnrbdgv", word[i - 1]) ? "z" : "s"; break;
      case '\'': break;
      default: if (std::strchr("bdfhklmnprtvwz", c)) phones = {consonant, 1}; break;
    }
    if (!rules.append(phones)) return false;
    ++i;
    if (at(i) == c && !writtenVowel(c)) ++i;
  }
  // The lexicon knows the stress; here the first vowel of a longer word takes it.
  if (word.size() > 2) {
    for (std::size_t i = 0; i < out.count; ++i) {
      if (isVowel(out.phones[i].sound)) {
        out.phones[i].prosody |= Primary;
        break;
      }
    }
  }
  return out.count != 0;
}

}

bool pronounce(std::string_view word, bool opensSentence, Pronunciation& out) {
  out.count = 0;
  if (englishPronunciation(word, opensSentence, out)) return true;
  if (spelling(word, out)) return true;
  out.count = 0;
  return false;
}

}

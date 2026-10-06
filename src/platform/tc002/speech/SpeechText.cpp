#include "core/render/TextEncoding.h"
#include "platform/tc002/speech/SpeechText.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "platform/tc002/speech/SpeechPronunciation.h"

namespace awtrix::speech {
namespace {

bool letter(char c) { return c >= 'a' && c <= 'z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

// A Latin-1 letter without its accent, À to ÿ; nullptr for × and ÷.
const char* const kLatin1[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y"};

// "°C" and "°F", with or without a space before the letter, name their scale; any other "°" is
// just "degrees". Moves at past the letter it reads.
const char* degrees(std::string_view text, std::size_t& at) {
  std::size_t unit = at;
  if (unit < text.size() && text[unit] == ' ') ++unit;
  const bool alone = unit + 1 >= text.size() || !std::isalpha(static_cast<unsigned char>(text[unit + 1]));
  if (unit < text.size() && alone) {
    const char c = text[unit];
    if (c == 'C' || c == 'c' || c == 'F' || c == 'f') {
      at = unit + 1;
      return c == 'C' || c == 'c' ? " degrees celsius " : " degrees fahrenheit ";
    }
  }
  return " degrees ";
}

// Lower-case ASCII the scanner reads; a space wherever words part.
const char* ascii(int32_t code) {
  switch (code) {
    case 0x2018: case 0x2019: case 0x201B: return "'";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212:
      return "-";
    case 0x2026: return "...";
    default: break;
  }
  if (code >= 0xC0 && code <= 0xFF && kLatin1[code - 0xC0]) return kLatin1[code - 0xC0];
  return " ";
}

std::string fold(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size();) {
    const uint32_t code = awtrix::text::nextCodepoint(text, at);
    if (code >= 'A' && code <= 'Z') out += static_cast<char>(code - 'A' + 'a');
    else if (code == '\n' || (code > ' ' && code < 0x7F)) out += static_cast<char>(code);
    else if (code < 0x80) out += ' ';
    else if (code == 0xB0) out += degrees(text, at);
    else out += ascii(code == awtrix::text::kInvalidCodepoint ? -1 : static_cast<int32_t>(code));
  }
  return out;
}

class Builder {
 public:
  explicit Builder(Plan& plan) : plan_(plan) {}
  bool full() const { return full_; }

  // A word that cannot be said is left out; one that no longer fits ends the plan.
  void word(std::string_view word) {
    detail::Pronunciation pronunciation;
    const bool opensSentence = opensSentence_;
    opensSentence_ = false;
    if (full_ || !detail::pronounce(word, opensSentence, pronunciation)) return;
    if (plan_.count + pronunciation.count >= kMaxPhones) {
      full_ = true;
      return;
    }
    std::copy_n(pronunciation.phones.begin(), pronunciation.count, plan_.phones.begin() + plan_.count);
    plan_.count += pronunciation.count;
    plan_.phones[plan_.count - 1].prosody |= WordEnd;
  }

  // Words leave room for one pause, so a plan always ends with one.
  void boundary(char punctuation = 0) {
    if (punctuation == '.' || punctuation == '?' || punctuation == '!') opensSentence_ = true;
    if (!plan_.count) return;
    if (plan_.phones[plan_.count - 1].sound == PhoneId::Silence) {
      if (punctuation) plan_.boundaries[plan_.count - 1] = punctuation;
      return;
    }
    plan_.boundaries[plan_.count] = punctuation;
    plan_.phones[plan_.count++] = {PhoneId::Silence, 0};
  }

  void number(uint32_t n) {
    static constexpr const char* units[] = {
        "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
        "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
        "seventeen", "eighteen", "nineteen"};
    static constexpr const char* tens[] = {
        "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n >= 1000) {
      number(n / 1000);
      word("thousand");
      n %= 1000;
      if (!n) return;
    }
    if (n >= 100) {
      word(units[n / 100]);
      word("hundred");
      n %= 100;
      if (!n) return;
    }
    if (n < 20) {
      word(units[n]);
      return;
    }
    word(tens[n / 10]);
    if (n % 10) word(units[n % 10]);
  }

 private:
  Plan& plan_;
  bool full_ = false;
  bool opensSentence_ = true;
};

}

bool prepare(std::string_view text, Plan& out) {
  out.count = 0;
  out.boundaries.fill(0);
  const std::string folded = fold(text);
  const std::size_t size = folded.size();
  Builder builder(out);
  for (std::size_t i = 0; i < size && !builder.full();) {
    const char c = folded[i];
    if (letter(c)) {
      const auto begin = i++;
      while (i < size && (letter(folded[i]) || folded[i] == '\'')) ++i;
      auto end = i;
      while (end > begin && folded[end - 1] == '\'') --end;
      builder.word(std::string_view(folded).substr(begin, end - begin));
    } else if (digit(c)) {
      const auto begin = i;
      uint32_t number = 0;
      while (i < size && digit(folded[i])) {
        if (i - begin < 6) number = number * 10 + folded[i] - '0';
        ++i;
      }
      // A time, h:mm or hh:mm: "seven oh five", "seven o'clock".
      if (i - begin <= 2 && number < 24 && i + 2 < size && folded[i] == ':' && digit(folded[i + 1]) &&
          digit(folded[i + 2]) && (i + 3 == size || !digit(folded[i + 3]))) {
        const uint32_t minutes = (folded[i + 1] - '0') * 10u + (folded[i + 2] - '0');
        if (minutes < 60) {
          builder.number(number);
          if (!minutes) builder.word("o'clock");
          else if (minutes < 10) builder.word("oh");
          if (minutes) builder.number(minutes);
          i += 3;
          continue;
        }
      }
      if (i - begin <= 6) builder.number(number);
      else for (auto j = begin; j < i; ++j) builder.number(folded[j] - '0');
      if (i + 1 < size && folded[i] == '.' && digit(folded[i + 1])) {
        builder.word("point");
        ++i;
        while (i < size && digit(folded[i])) builder.number(folded[i++] - '0');
      }
    } else {
      const bool wordStart = i == 0 || !(letter(folded[i - 1]) || digit(folded[i - 1]));
      if (c == '?' || c == '!' || c == '.' || c == ',' || c == ';' || c == ':') builder.boundary(c);
      else if (c == '\n') builder.boundary(',');
      else if (c == '-' && wordStart && i + 1 < size && digit(folded[i + 1])) builder.word("minus");
      else if (c == '%') builder.word("percent");
      else if (c == '&') builder.word("and");
      ++i;
    }
  }
  builder.boundary();
  return out.count != 0;
}

}

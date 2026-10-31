#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/tc002/speech/SpeechTypes.h"

namespace awtrix::speech {

// A language's text frontend, read from a voice pack: which characters are letters and what the
// others become, how numbers and clock times are said, a lexicon and spelling rules for the rest.
// parse() checks everything once; prepare() only reads, so any number of threads may share it.
class Frontend {
 public:
  // `text` is the pack's fe.text, `lexicon` its fe.lexicon (may be empty), which must outlive the
  // frontend: it is read in place. nullptr and a short reason when either is malformed.
  static std::unique_ptr<const Frontend> parse(std::string_view text, std::string_view lexicon, std::string& error);

  // UTF-8 text as a plan. A text longer than a plan holds ends with the last word that fits. False
  // when no word is left to say.
  bool prepare(std::string_view text, Plan& out) const;

  std::size_t phoneCount() const { return phones_.size(); }
  const std::string& phoneName(std::size_t phone) const { return phones_[phone].name; }
  bool vowel(std::size_t phone) const { return phone < phones_.size() && phones_[phone].vowel; }

  static constexpr std::size_t kMaxWordPhones = 64;
  struct Word {
    std::array<Phone, kMaxWordPhones> phones{};
    std::size_t count = 0;
  };
  // A folded word as phones with stress: from the lexicon, else by the rules. opensSentence is true
  // for a sentence's first word. False when the word yields no phone or more than kMaxWordPhones.
  bool pronounce(std::string_view word, bool opensSentence, Word& out) const;
  // The lexicon's reading of a folded word alone; false when the lexicon lacks it.
  bool lookup(std::string_view word, bool opensSentence, Word& out) const;

 private:
  struct PhoneInfo {
    std::string name;
    bool vowel = false;
  };
  struct Fold {
    std::string from, to;
    bool alone = false;  // the character after `from` must not be an ASCII letter
  };
  struct Context {
    enum Kind : uint8_t { Boundary, Any, In, NotIn } kind = Any;
    std::u32string set;
  };
  struct Rule {
    std::u32string letters;
    std::vector<Context> left, right;
    std::vector<Phone> phones;
    bool stressed = false;
  };
  struct Variant {
    int any = 0, opening = -1;
  };

  std::string fold(std::string_view text) const;
  bool letter(uint32_t c) const;
  // afterPlace: n follows a higher place of the same number (Mandarin yī shí in 110).
  std::string number(uint32_t n, bool afterPlace = false) const;
  // The written abbreviation of [abbreviations] that starts at `at`, or nullptr.
  const std::pair<std::string, std::string>* abbreviation(std::string_view folded, std::size_t at) const;
  bool rules(const std::u32string& word, Word& out) const;
  bool matches(const Rule& rule, const std::u32string& word, std::size_t at) const;
  void stress(const std::u32string& word, const std::vector<std::size_t>& phonesAt, Word& out) const;
  bool parseLine(const std::string& section, std::string_view line, std::string& error);
  bool parsePhones(std::string_view list, std::vector<Phone>& out, bool& stressed, std::string& error) const;

  std::vector<PhoneInfo> phones_;
  std::u32string letters_;
  std::vector<Fold> folds_;
  std::map<char, std::string> symbols_;
  std::vector<std::string> units_ = std::vector<std::string>(100);
  std::vector<std::string> hundreds_ = std::vector<std::string>(10);
  std::vector<std::string> hundredsExact_ = std::vector<std::string>(10);  // N·100 alone (Spanish cien)
  std::string thousand_, oneThousand_, thousandJoin_ = " ", restJoin_ = " ";
  std::string thousandFew_;  // after a count ending in 2-4 but not 12-14 (Polish tysiące)
  std::string tenThousand_;  // set: numbers group by 10 000 (Mandarin wàn)
  std::string zero_;         // said once for skipped places (Mandarin líng in 105)
  std::string tenPrefix_;    // before 10-19 after a higher place (Mandarin yī in 110)
  std::string twoCount_;     // 2 counting thousands and ten thousands (Mandarin liǎng)
  std::string percentFirst_; // set: n% reads this word before the number (Mandarin bǎi fēn zhī)
  std::string yearWord_;     // a four-digit number before this word reads digit by digit
  std::vector<std::pair<std::string, std::string>> oneBeforeWord_;  // number ending -> ending before a word
  char decimal_ = 0, group_ = 0;
  std::string decimalWord_, minus_;
  std::string timeSeparators_;
  std::string timeExact_, timeEarly_, timeOther_;
  std::vector<std::string> hours_ = std::vector<std::string>(24);  // an hour of a time, else its number
  std::string timeWord_;  // a written word after a time that the templates already say ("Uhr")
  std::vector<std::pair<std::string, std::string>> abbreviations_;  // written (folded) -> words, longest first
  std::vector<std::string> spell_ = std::vector<std::string>(26);   // letter names a-z for acronyms
  std::vector<Rule> rules_;
  std::map<char32_t, std::vector<std::size_t>> rulesByLetter_;
  std::size_t stressMinLetters_ = 0;
  std::u32string onsetLetters_;  // a word that starts with one of these begins with onsetPhones_
  std::vector<Phone> onsetPhones_;
  std::vector<std::u32string> unstressedPrefixes_;
  std::map<std::string, Variant, std::less<>> variants_;
  std::string_view lexicon_;
  uint32_t lexiconWords_ = 0, lexiconBlocks_ = 0, lexiconStride_ = 0;
};

}

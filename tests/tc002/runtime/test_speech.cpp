#include "../../support.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/speech/SpeechLexicon.h"
#include "platform/tc002/speech/SpeechRequest.h"
#include "platform/tc002/speech/SpeechText.h"

using namespace awtrix::speech;
namespace {
auto& passed = awtrix::test::passed();
constexpr auto check = awtrix::test::require;
Plan plan(const std::string& text) {
  Plan p;
  check(prepare(text, p), ("nothing to say in " + text).c_str());
  return p;
}
std::vector<PhoneId> sounds(const Plan& p) {
  std::vector<PhoneId> s;
  for (std::size_t i = 0; i < p.count; ++i) if (p.phones[i].sound != PhoneId::Silence) s.push_back(p.phones[i].sound);
  return s;
}

}
// The table against the TSV it was built from, where a decoder goes wrong: the ends of the table,
// block starts, lengths escaped past 14, words with several variants and the words after them, and
// the variant a word prefers.
void checkLexicon(const char* file) {
  using P = PhoneId;
  const std::map<std::string, P> arpabet = {
      {"AA",P::Aa},{"AE",P::Ae},{"AH",P::Ah},{"AO",P::Ao},{"AW",P::Au},{"AY",P::Ai},
      {"B",P::B},{"CH",P::Ch},{"D",P::D},{"DH",P::Dh},{"EH",P::Eh},{"ER",P::Er},
      {"EY",P::Ei},{"F",P::F},{"G",P::G},{"HH",P::H},{"IH",P::Ih},{"IY",P::Iy},
      {"JH",P::Jh},{"K",P::K},{"L",P::L},{"M",P::M},{"N",P::N},{"NG",P::Ng},
      {"OW",P::Ou},{"OY",P::Oi},{"P",P::P},{"R",P::R},{"S",P::S},{"SH",P::Sh},
      {"T",P::T},{"TH",P::Th},{"UH",P::Uh},{"UW",P::Uw},{"V",P::V},{"W",P::W},
      {"Y",P::J},{"Z",P::Z},{"ZH",P::Zh}};
  std::ifstream input(file);
  check(input.good(), "the TSV opens");
  std::map<std::string, std::map<int, std::string>> entries;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#' || line.rfind("word\tcmu_variant\t", 0) == 0) continue;
    const auto first = line.find('\t'), second = line.find('\t', first + 1);
    entries[line.substr(0, first)][std::stoi(line.substr(first + 1, second - first - 1))] = line.substr(second + 1);
  }
  std::vector<std::string> words;
  for (const auto& entry : entries) words.push_back(entry.first);
  constexpr std::size_t kStride = 32;
  const auto shared = [&](std::size_t i) {
    if (i % kStride == 0) return std::size_t{0};
    std::size_t n = 0;
    while (n < words[i].size() && n < words[i - 1].size() && words[i][n] == words[i - 1][n]) ++n;
    return n;
  };
  const auto prefers = [](const std::string& word) {
    return word == "wind" || word == "live" || word == "read" || word == "lead" || word == "can" ||
           word == "perfect" || word == "excuse";
  };
  unsigned starts = 0, longPrefix = 0, longSuffix = 0, several = 0, after = 0, preferred = 0, checked = 0;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const std::string& word = words[i];
    const std::size_t prefix = shared(i);
    const bool start = i % kStride == 0 && (i < 3 * kStride || i + kStride > words.size());
    const bool severalHere = entries[word].size() >= 3;
    const bool afterSeveral = i % kStride != 0 && entries[words[i - 1]].size() >= 3;
    starts += start;
    longPrefix += prefix >= 15;
    longSuffix += word.size() - prefix >= 15;
    several += severalHere;
    after += afterSeveral;
    preferred += prefers(word);
    if (!(i == 0 || i + 1 == words.size() || start || prefix >= 15 || word.size() - prefix >= 15 ||
          severalHere || afterSeveral || prefers(word)))
      continue;
    const auto& variants = entries[word];
    auto chosen = variants.begin();
    if (prefers(word) && variants.size() > 1) ++chosen;
    detail::Pronunciation actual;
    check(detail::englishPronunciation(word, false, actual), ("missing word " + word).c_str());
    std::istringstream phones(chosen->second);
    std::string phone;
    std::size_t at = 0;
    while (phones >> phone) {
      unsigned stress = 0;
      if (phone.back() >= '0' && phone.back() <= '2') { stress = phone.back() - '0'; phone.pop_back(); }
      const auto id = phone == "AH" && stress == 0 ? P::Schwa : arpabet.at(phone);
      check(at < actual.count && actual.phones[at].sound == id && actual.phones[at].prosody == stress,
            ("phone and stress of " + word).c_str());
      ++at;
    }
    check(at == actual.count, ("number of phones of " + word).c_str());
    ++checked;
  }
  check(starts && longPrefix && longSuffix && several && after && preferred == 7, "every edge is in the table");
  detail::Pronunciation missing;
  for (const std::string& word : {std::string(), std::string("aaaaaawordmissing"), std::string("zzzzzzzzzzz"),
                                  std::string("Hello"), std::string("hello!"), words.back() + "s",
                                  std::string(64, 'a')})
    check(!detail::englishPronunciation(word, false, missing) && missing.count == 0, "an absent word finds nothing");
  std::printf("lexicon: %u of %zu words at the decoder's edges checked\n", checked, words.size());
}
int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--lexicon") { checkLexicon(argv[2]); return 0; }
  if (argc != 1) { std::fprintf(stderr, "usage: tc002-speech-test [--lexicon FILE]\n"); return 2; }
  using P = PhoneId;
  // Independent CMUdict fixtures: HH AH0 L OW1, G UH1 D, AH1 DH ER0.
  check(sounds(plan("hello")) == std::vector<P>{P::H, P::Schwa, P::L, P::Ou}, "hello: lexical diphthong");
  check(sounds(plan("good")) == std::vector<P>{P::G, P::Uh, P::D}, "good: FOOT distinct from STRUT");
  check(sounds(plan("other")) == std::vector<P>{P::Ah, P::Dh, P::Er}, "other: voiced dental and rhotic vowel");
  check(sounds(plan("computer")) == std::vector<P>{P::K, P::Schwa, P::M, P::P, P::J, P::Uw, P::T, P::Er}, "computer phones");
  const Plan functionWord = plan("the");
  check((functionWord.phones[1].prosody & Primary) == 0, "known unstressed function word stays reduced");
  const Plan hello = plan("hello");
  check((hello.phones[3].prosody & Primary) && !(hello.phones[1].prosody & Primary), "hello lexical stress");
  check(hello.phones[3].prosody & WordEnd, "a word's last phone ends it");
  check(sounds(plan("five")) == std::vector<P>{P::F, P::Ai, P::V}, "diphthong is one event");
  check(sounds(plan("23.5")) == sounds(plan("twenty three point five")), "English decimal expansion");
  check(sounds(plan("the")) != sounds(plan("thin")), "voiced and voiceless dental fricatives");
  const Plan question = plan("Ready?");
  check(question.boundaries[question.count - 1] == '?', "a question mark reaches the voice");
  const Plan period = plan("ready."); const Plan ellipsis = plan("ready...");
  check(period.count == ellipsis.count &&
        std::equal(period.boundaries.begin(), period.boundaries.begin() + period.count,
                   ellipsis.boundaries.begin()), "repeated punctuation does not add extra boundaries");
  check(sounds(plan("'hello'")) == sounds(plan("hello")), "quotation marks do not change spelling");

  check(sounds(plan("Caf\xC3\xA9 Na\xC3\xAFve")) == sounds(plan("cafe naive")), "accents fold away");
  check(sounds(plan("Gr\xC3\xBC\xC3\x9F" "e")) == sounds(plan("grusse")), "sharp s folds to ss");
  check(sounds(plan("21\xC2\xB0")) == sounds(plan("21 degrees")), "the degree sign is a word");
  check(sounds(plan("21\xC2\xB0" "C")) == sounds(plan("21 degrees celsius")), "degrees Celsius");
  check(sounds(plan("70 \xC2\xB0" "F.")) == sounds(plan("70 degrees fahrenheit.")), "degrees Fahrenheit");
  check(sounds(plan("21\xC2\xB0 c")) == sounds(plan("21 degrees celsius")), "a space may part the scale");
  check(sounds(plan("21\xC2\xB0 cold")) == sounds(plan("21 degrees cold")), "a word after the sign is no scale");
  check(sounds(plan("50%")) == sounds(plan("50 percent")), "the percent sign is a word");
  check(sounds(plan("salt & pepper")) == sounds(plan("salt and pepper")), "the ampersand is a word");
  check(sounds(plan("It\xE2\x80\x99s ready")) == sounds(plan("It's ready")), "a typographic apostrophe keeps a contraction");
  check(sounds(plan("\xE2\x80\x9Chello\xE2\x80\x9D")) == sounds(plan("hello")), "typographic quotes part words");
  check(sounds(plan("10\xC2\xA0km")) == sounds(plan("10 km")), "a no-break space parts words");
  check(sounds(plan("hi \xF0\x9F\x98\x80 there")) == sounds(plan("hi there")), "an emoji parts words");
  check(sounds(plan(std::string("on\0off", 6))) == sounds(plan("on off")), "a control character parts words");
  check(sounds(plan("on\xC3 off")) == sounds(plan("on off")), "a broken UTF-8 byte parts words");
  const Plan dots = plan("ready\xE2\x80\xA6");
  check(dots.boundaries[dots.count - 1] == '.', "an ellipsis ends a sentence");
  check(sounds(plan("-5")) == sounds(plan("minus five")), "minus before a number");
  check(sounds(plan("\xE2\x80\x93" "5")) == sounds(plan("minus five")), "a typographic dash is a hyphen");
  check(sounds(plan("5-7")) == sounds(plan("five seven")), "a hyphen between numbers is no minus");
  check(sounds(plan("7:05")) == sounds(plan("seven oh five")), "minutes below ten read with oh");
  check(sounds(plan("12:00")) == sounds(plan("twelve o'clock")), "a full hour reads o'clock");
  check(sounds(plan("23:45")) == sounds(plan("twenty three forty five")), "a time reads hours and minutes");
  check(sounds(plan("24:00")) == sounds(plan("twenty four, zero")), "no time past 23:59");
  check(sounds(plan("don't")) == std::vector<P>{P::D, P::Ou, P::N, P::T}, "contractions come from the lexicon");
  check(sounds(plan("perfect")) == std::vector<P>{P::P, P::Er, P::F, P::Ih, P::K, P::T}, "perfect is the adjective");
  check(sounds(plan("Close the door")).at(3) == P::Z && sounds(plan("Stay close")).back() == P::S,
        "close is the verb where it opens a sentence");
  check(sounds(plan("Wait. Use the stairs")).at(5) == P::Z, "a sentence opens after a full stop too");

  Plan cut;
  for (const char* text : {"", "...", " \xF0\x9F\x98\x80 ", "\xC3"})
    check(!prepare(text, cut) && cut.count == 0, "nothing to say");
  check(prepare(std::string(512, '7'), cut), "too much to say is cut short");
  check(cut.count + detail::kMaxWordPhones >= kMaxPhones && cut.phones[cut.count - 1].sound == PhoneId::Silence,
        "a cut plan is full and ends with a pause");
  std::string unsayable;
  for (int i = 0; i < 30; ++i) unsayable += "qx";
  check(prepare(unsayable + " ok", cut) && sounds(cut) == sounds(plan("ok")), "a word too long to say is left out");

  awtrix::DispatchDetail error;
  check(readText("Hello", cut, error) && sounds(cut) == sounds(plan("hello")), "a text is read as its words");
  std::string longest;
  while (longest.size() + 6 <= kMaxTextBytes) longest += "hello ";
  longest.resize(kMaxTextBytes, 'o');
  check(readText(longest, cut, error), "512 bytes of text are taken");
  const std::pair<std::string, const char*> refused[] = {
      {"", "must be 1..512 bytes"},
      {std::string(kMaxTextBytes + 1, 'a'), "must be 1..512 bytes"},
      {"?!", "no words to speak"},
  };
  for (const auto& text : refused) {
    error = {};
    check(!readText(text.first, cut, error) && error.field.empty() && error.message == text.second,
          text.first.c_str());
  }
  std::printf("speech frontend contracts: %u passed\n", passed.load());
}

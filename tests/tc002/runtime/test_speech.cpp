#include "../../support.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/speech/SpeechFrontend.h"
#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechRequest.h"

using namespace awtrix::speech;
namespace {
auto& passed = awtrix::test::passed();
constexpr auto check = awtrix::test::require;
// The English frontend of the release's voice pack (assets/speech/voice.atts).
std::shared_ptr<const SpeechModel> g_pack;
std::unique_ptr<const Frontend> g_english;
const Frontend& english() { return *g_english; }
bool prepare(std::string_view text, Plan& out) { return english().prepare(text, out); }
Plan plan(const std::string& text) {
  Plan p;
  check(prepare(text, p), ("nothing to say in " + text).c_str());
  return p;
}
// readText() asks a voice; this one only reads.
struct Reader final : SpeechVoice {
  uint32_t rate() const override { return 24000; }
  bool prepare(std::string_view text, Plan& out) const override { return english().prepare(text, out); }
  std::unique_ptr<SpeechUtterance> start(const Plan&) override { return nullptr; }
};
void loadPack(const char* path) {
  std::string error;
  g_pack = SpeechModel::load(path, error);
  check(g_pack != nullptr, ("the pack loads: " + error).c_str());
  g_english = Frontend::parse(g_pack->frontendText, g_pack->frontendLexicon, error);
  check(g_english != nullptr, ("its frontend parses: " + error).c_str());
  check(g_english->phoneCount() == 41 && g_pack->phoneCount == 41, "English has the 41 phones of contract 1");
}
std::vector<PhoneId> sounds(const Plan& p) {
  std::vector<PhoneId> s;
  for (std::size_t i = 0; i < p.count; ++i) if (p.phones[i].sound != PhoneId::Silence) s.push_back(p.phones[i].sound);
  return s;
}

}
// An uploaded pack is untrusted: a lexicon whose entries lie is refused when the pack loads.
void checkHostileLexicon() {
  const std::string_view good = g_pack->frontendLexicon;
  const auto word32 = [&](std::size_t at) {
    return uint32_t(uint8_t(good[at])) | uint32_t(uint8_t(good[at + 1])) << 8 | uint32_t(uint8_t(good[at + 2])) << 16 |
           uint32_t(uint8_t(good[at + 3])) << 24;
  };
  const std::size_t first = 16 + std::size_t{word32(12)} * 4 + word32(16);
  const unsigned lengths = uint8_t(good[first]);
  check((lengths >> 4) == 0 && (lengths & 15) < 15, "the first word is stored whole and short");
  const std::size_t variants = first + 1 + (lengths & 15), phones = variants + 1;
  const auto refused = [&](const char* what, const std::function<void(std::string&)>& change) {
    std::string lexicon(good);
    change(lexicon);
    std::string error;
    check(!Frontend::parse(g_pack->frontendText, lexicon, error) && error == "fe.lexicon: bad block",
          (std::string(what) + ": " + error).c_str());
  };
  refused("a word longer than a word", [&](std::string& l) { l[phones] = char(200); });
  refused("a word without phones", [&](std::string& l) { l[phones] = 0; });
  refused("a word without variants", [&](std::string& l) { l[variants] = 0; });
  refused("a block that starts with a shared prefix", [&](std::string& l) { l[first] = char(0x30 | (lengths & 15)); });
  refused("a phone outside the inventory", [&](std::string& l) { l[phones + 1] = char(0x3F); });
  refused("a lexicon cut short", [&](std::string& l) { l.resize(l.size() - 3); });
}
int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: tc002-speech-test PACK\n"); return 2; }
  loadPack(argv[1]);
  checkHostileLexicon();
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
  check(cut.count + Frontend::kMaxWordPhones >= kMaxPhones && cut.phones[cut.count - 1].sound == PhoneId::Silence,
        "a cut plan is full and ends with a pause");
  std::string unsayable;
  for (int i = 0; i < 30; ++i) unsayable += "qx";
  check(prepare(unsayable + " ok", cut) && sounds(cut) == sounds(plan("ok")), "a word too long to say is left out");

  awtrix::DispatchDetail error;
  const Reader reader;
  check(readText(reader, "Hello", cut, error) && sounds(cut) == sounds(plan("hello")), "a text is read as its words");
  std::string longest;
  while (longest.size() + 6 <= kMaxTextBytes) longest += "hello ";
  longest.resize(kMaxTextBytes, 'o');
  check(readText(reader, longest, cut, error), "512 bytes of text are taken");
  const std::pair<std::string, const char*> refused[] = {
      {"", "must be 1..512 bytes"},
      {std::string(kMaxTextBytes + 1, 'a'), "must be 1..512 bytes"},
      {"?!", "no words to speak"},
  };
  for (const auto& text : refused) {
    error = {};
    check(!readText(reader, text.first, cut, error) && error.field.empty() && error.message == text.second,
          text.first.c_str());
  }
  std::printf("speech frontend contracts: %u passed\n", passed.load());
}

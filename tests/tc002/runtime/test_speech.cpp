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
// prepare() runs on the loop thread, so the frontend reads its own lexicon and never the pack's file.
void checkLexiconIsItsOwn() {
  std::string lexicon(g_pack->frontendLexicon), error;
  const auto frontend = Frontend::parse(g_pack->frontendText, lexicon, error);
  check(frontend != nullptr, ("the frontend parses: " + error).c_str());
  Frontend::Word before, after;
  check(frontend->lookup("hello", false, before) && before.count > 0, "the lexicon knows a word");
  std::fill(lexicon.begin(), lexicon.end(), '\0');
  check(frontend->lookup("hello", false, after) && after.count == before.count, "and still does without the pack's bytes");
}
// A one-block fe.lexicon as Frontend::parse() reads it, keys in byte order, one pronunciation each:
// a phone's id in 6 bits, a vowel's stress in 2 more.
std::string lexiconOf(const std::vector<std::pair<std::string, std::vector<Phone>>>& entries) {
  const auto u32 = [](std::string& out, uint32_t v) { for (int b = 0; b < 4; ++b) out += char(v >> (8 * b)); };
  std::string out = "LXR1";
  out += char(entries.size());
  out += char(0);
  out += char(english().phoneCount());
  out += char(0);
  u32(out, uint32_t(entries.size()));
  u32(out, 1);
  u32(out, 0);
  for (const auto& [word, phones] : entries) {
    out += char(word.size());
    out += word;
    out += char(1);
    out += char(phones.size());
    uint64_t bits = 0;
    unsigned used = 0;
    for (const Phone& phone : phones) {
      bits |= uint64_t(phone.sound) << used;
      used += 6;
      if (english().vowel(std::size_t(phone.sound))) {
        bits |= uint64_t(phone.prosody & 3) << used;
        used += 2;
      }
    }
    for (unsigned b = 0; b < used; b += 8) out += char(bits >> b);
  }
  return out;
}

// Words joined by hyphens are one word when the lexicon knows them joined, else separate words.
void checkHyphenatedWords() {
  using P = PhoneId;
  check(sounds(plan("x-ray")) == sounds(plan("x ray")), "a hyphen the lexicon does not know separates words");
  check(sounds(plan("e-mail")) == sounds(plan("e mail")), "the release lexicon has no hyphenated words");
  const std::vector<Phone> email{{P::Iy, Primary}, {P::M, 0}, {P::Ei, Secondary}, {P::L, 0}};
  const std::vector<Phone> tshirt{{P::T, 0}, {P::Iy, Primary}, {P::Sh, 0}, {P::Er, Secondary}, {P::T, 0}};
  static const std::string lexicon = lexiconOf({{"e-mail", email}, {"t-shirt", tshirt}});
  std::string error;
  const auto frontend = Frontend::parse(g_pack->frontendText, lexicon, error);
  check(frontend != nullptr, ("a lexicon with hyphenated words parses: " + error).c_str());
  const auto said = [&](std::string_view text) {
    Plan p;
    check(frontend->prepare(text, p), "something to say");
    return sounds(p);
  };
  const auto phones = [](std::initializer_list<std::vector<Phone>> words) {
    std::vector<P> s;
    for (const auto& word : words) for (const Phone& phone : word) s.push_back(phone.sound);
    return s;
  };
  check(said("e-mail") == phones({email}), "a hyphenated word from the lexicon is one word");
  check(said("E-Mail") == phones({email}), "folding comes before the lookup");
  Plan p;
  check(frontend->prepare("e-mail", p) && (p.phones[p.count - 2].prosody & WordEnd) &&
            !(p.phones[0].prosody & WordEnd), "it ends once, as one word");
  check(said("t-shirt-shop") == said("t-shirt shop"), "the longest known start of a chain is one word");
  check(said("a-t-shirt") == said("a t-shirt"), "a known word may follow an unknown part");
  check(said("x-ray") == said("x ray"), "an unknown chain still separates");
}

// The English tables with more lines; compares what two texts say in that frontend.
struct Extended {
  std::string text;
  std::unique_ptr<const Frontend> frontend;
  explicit Extended(const std::string& extra) : text(std::string(g_pack->frontendText) + extra) {
    std::string error;
    frontend = Frontend::parse(text, g_pack->frontendLexicon, error);
    check(frontend != nullptr, ("extended tables parse: " + error).c_str());
  }
  std::vector<PhoneId> said(std::string_view text) const {
    Plan p;
    check(frontend->prepare(text, p), "something to say");
    return sounds(p);
  }
  bool same(std::string_view a, std::string_view b) const { return said(a) == said(b); }
};

// [abbreviations] and [spell]: written units and acronyms.
void checkAbbreviationsAndAcronyms() {
  std::string letters = "\n[spell]\n";
  const char* names[] = {"ay", "bee", "see", "dee", "ee", "eff", "gee", "aitch", "eye", "jay", "kay", "el", "em",
                         "en", "oh", "pee", "cue", "ar", "ess", "tee", "you", "vee", "double you", "ex", "why", "zed"};
  for (char l = 'a'; l <= 'z'; ++l) letters += std::string(1, l) + "\t" + names[l - 'a'] + "\n";
  const Extended en("\n[abbreviations]\nkm/h\tkilometers per hour\nm/s\tmeters per second\nm\tmeters\n" + letters);
  check(en.same("25 km/h", "twenty five kilometers per hour"), "a written unit reads its words");
  check(en.same("25km/h", "twenty five kilometers per hour"), "a number may touch the unit");
  check(en.same("KM/H", "kilometers per hour"), "a unit is matched after folding");
  check(en.same("3 m/s", "three meters per second"), "the longest abbreviation wins");
  check(en.same("5 m away", "five meters away"), "a short abbreviation alone");
  check(en.same("my mom", "my mom"), "an abbreviation only matches a whole word");
  check(en.same("kmh", "kmh"), "an unlisted spelling stays a word");
  check(en.same("Parcel from DHL", "parcel from dee aitch el"), "an acronym is spelled");
  check(en.same("The SNCF strike", "the ess en see eff strike"), "four capitals without a vowel are spelled");
  check(en.same("The NASA team", "the nasa team"), "four capitals with a vowel are a word");
  check(en.same("CALL ME NOW", "call me now"), "a text all in capitals is read, not spelled");
  check(en.same("A new TV", "a new tee vee"), "two capitals are spelled");
  check(en.same("the Tv", "the tv"), "a word with lower case is no acronym");
  check(en.same("DHLs", "dhls"), "capitals inside a longer word are no acronym");
  check(en.same("Code ABCDEF here", "code abcdef here"), "six capitals are a word");
  check(en.same("Ship it with UPS", "ship it with you pee ess"), "an acronym is no word of the lexicon");
  check(en.same("Ship it with ups", "ship it with ups"), "the word in lower case is");
  const Extended plain("\n[abbreviations]\nkm/h\tkilometers per hour\n");
  check(plain.same("Parcel from DHL", "parcel from dhl"), "without letter names nothing is spelled");
  // A lexicon entry in capitals is the reading of that acronym.
  using P = PhoneId;
  const std::vector<Phone> nato{{P::N, 0}, {P::Ei, Primary}, {P::T, 0}, {P::Ou, 0}};
  static const std::string lexicon = lexiconOf({{"UPS", nato}});
  std::string error;
  const std::string text = std::string(g_pack->frontendText) + letters;
  const auto frontend = Frontend::parse(text, lexicon, error);
  check(frontend != nullptr, ("a lexicon with an acronym parses: " + error).c_str());
  Plan p;
  check(frontend->prepare("Ship it with UPS", p) && p.count > nato.size(), "something to say");
  check(std::equal(nato.begin(), nato.end(), p.phones.begin() + (p.count - 1 - nato.size()),
                   [](const Phone& a, const Phone& b) { return a.sound == b.sound; }),
        "an acronym reads its entry in capitals");
}

// The optional [numbers] lines: ten thousands, skipped places, counts, units and hours.
void checkNumberReadings() {
  // Mandarin-like grouping in English words: wan = 10 000, ling for skipped places.
  const Extended zh("\n[numbers]\nten thousand\twan\nzero\tling\nten prefix\tyi\ntwo count\tliang\n"
                    "percent first\tpercent of\nyear word\tnian\n");
  check(zh.same("105", "one hundred ling five"), "a skipped place says zero once");
  check(zh.same("110", "one hundred yi ten"), "ten after a place takes its prefix");
  check(zh.same("10", "ten"), "ten alone takes no prefix");
  check(zh.same("1005", "one thousand ling five"), "skipped hundreds and tens say zero once");
  check(zh.same("1010", "one thousand ling yi ten"), "zero and the ten prefix together");
  check(zh.same("2000", "liang thousand"), "two counting thousands");
  check(zh.same("20000", "liang wan"), "two counting ten thousands");
  check(zh.same("12345", "one wan liang thousand three hundred forty five"), "groups of ten thousand");
  check(zh.same("100000", "ten wan"), "a count of ten thousands");
  check(zh.same("100500", "ten wan ling five hundred"), "zero after ten thousands");
  check(zh.same("25%", "percent of twenty five"), "the percent word comes first");
  check(zh.same("2026 nian", "two zero two six nian"), "a year reads digit by digit");
  check(zh.same("2026 apples", "liang thousand ling twenty six apples"), "only before the year word");
  // Spanish, Polish and German forms.
  const Extended forms("\n[numbers]\nhundred 1 exact\ta hundred\nthousand few\tgrand\nhour 7\tseventh\n"
                       "hour 0\tmidnight\none before word\tone\ta\none before word\ttwenty one\ttwentyone\n");
  check(forms.same("100", "a hundred"), "a hundred alone has its own word");
  check(forms.same("101", "one hundred one"), "a hundred with a rest keeps the other");
  check(forms.same("3000", "three grand"), "counts ending in 2 to 4 take the few form");
  check(forms.same("13000", "thirteen thousand"), "12 to 14 do not");
  check(forms.same("23000", "twenty three grand"), "23 does");
  check(forms.same("25000", "twenty five thousand"), "25 does not");
  check(forms.same("7:05", "seventh oh five"), "an hour of a time may have its own word");
  check(forms.same("0:30", "midnight thirty"), "so may hour zero");
  check(forms.same("8:00", "eight o'clock"), "other hours read their number");
  check(forms.same("1 hour", "a hour"), "a count before a word takes its form");
  check(forms.same("21 hours", "twentyone hours"), "the longest ending wins");
  check(forms.same("101 hours", "one hundred a hours"), "the ending of a longer number");
  check(forms.same("1%", "a percent"), "before a unit sign too");
  check(forms.same("Number 1.", "number one."), "not before punctuation");
  check(forms.same("1.5 hours", "one point five hours"), "not after a decimal");
  check(forms.same("1 2", "one two"), "not before another number");
  check(Extended("").same("2026 nian", "two thousand twenty six nian"), "tables without the lines read as before");
}

int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: tc002-speech-test PACK\n"); return 2; }
  loadPack(argv[1]);
  checkHostileLexicon();
  checkLexiconIsItsOwn();
  checkHyphenatedWords();
  checkAbbreviationsAndAcronyms();
  checkNumberReadings();
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

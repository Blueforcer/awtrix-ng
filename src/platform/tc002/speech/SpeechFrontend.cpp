#include "platform/tc002/speech/SpeechFrontend.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "core/render/TextEncoding.h"

namespace awtrix::speech {
namespace {

constexpr std::size_t kMaxPhonesInInventory = 64;
constexpr std::size_t kMaxRules = 4096;
constexpr std::size_t kMaxContext = 8;
constexpr std::size_t kMaxWordBytes = 64;

bool digit(char c) { return c >= '0' && c <= '9'; }
bool asciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::vector<std::string_view> fields(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (;;) {
    const auto tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
    if (tab == std::string_view::npos) return out;
    start = tab + 1;
  }
}

std::vector<std::string_view> words(std::string_view text) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') ++i;
    const auto begin = i;
    while (i < text.size() && text[i] != ' ') ++i;
    if (i > begin) out.push_back(text.substr(begin, i - begin));
  }
  return out;
}

bool utf32(std::string_view text, std::u32string& out) {
  out.clear();
  for (std::size_t at = 0; at < text.size();) {
    const uint32_t c = awtrix::text::nextCodepoint(text, at);
    if (c == awtrix::text::kInvalidCodepoint) return false;
    out.push_back(static_cast<char32_t>(c));
  }
  return true;
}

bool parseNumber(std::string_view text, uint32_t low, uint32_t high, uint32_t& out) {
  if (text.empty() || text.size() > 6) return false;
  out = 0;
  for (const char c : text) {
    if (!digit(c)) return false;
    out = out * 10 + static_cast<uint32_t>(c - '0');
  }
  return out >= low && out <= high;
}

uint32_t word32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

}

bool Frontend::letter(uint32_t c) const {
  return (c >= 'a' && c <= 'z') || letters_.find(static_cast<char32_t>(c)) != std::u32string::npos;
}

std::string Frontend::fold(std::string_view text) const {
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size();) {
    const auto c = static_cast<unsigned char>(text[at]);
    if (c < 0x80) {
      if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
      else if (c == '\n' || (c > ' ' && c < 0x7F)) out += static_cast<char>(c);
      else out += ' ';
      ++at;
      continue;
    }
    bool folded = false;
    for (const Fold& f : folds_) {
      if (text.compare(at, f.from.size(), f.from) != 0) continue;
      const std::size_t next = at + f.from.size();
      if (f.alone && next < text.size() && asciiAlpha(text[next])) continue;
      out += f.to;
      at = next;
      folded = true;
      break;
    }
    if (folded) continue;
    const std::size_t begin = at;
    const uint32_t code = awtrix::text::nextCodepoint(text, at);
    if (code != awtrix::text::kInvalidCodepoint && letter(code)) out.append(text.substr(begin, at - begin));
    else out += ' ';
  }
  return out;
}

std::string Frontend::number(uint32_t n) const {
  if (n >= 1000) {
    const uint32_t t = n / 1000, rest = n % 1000;
    std::string head = t == 1 && !oneThousand_.empty() ? oneThousand_ : number(t) + thousandJoin_ + thousand_;
    return rest ? head + restJoin_ + number(rest) : head;
  }
  if (n >= 100) {
    const uint32_t rest = n % 100;
    return rest ? hundreds_[n / 100] + restJoin_ + number(rest) : hundreds_[n / 100];
  }
  return units_[n];
}

bool Frontend::lookup(std::string_view word, bool opensSentence, Word& out) const {
  out.count = 0;
  if (lexicon_.empty() || word.empty() || word.size() >= kMaxWordBytes) return false;
  const auto* base = reinterpret_cast<const uint8_t*>(lexicon_.data());
  const auto block = [&](uint32_t index) { return base + 16 + lexiconBlocks_ * 4 + word32(base + 16 + index * 4); };
  std::array<char, kMaxWordBytes> scratch{};
  const auto key = [&](const uint8_t*& p) {
    const unsigned lengths = *p++;
    unsigned prefix = lengths >> 4, suffix = lengths & 15;
    if (prefix == 15) prefix = *p++;
    if (suffix == 15) suffix = *p++;
    std::memcpy(scratch.data() + prefix, p, suffix);
    p += suffix;
    return std::string_view(scratch.data(), prefix + suffix);
  };
  uint32_t lower = 0, upper = lexiconBlocks_;
  while (lower < upper) {
    const uint32_t middle = lower + (upper - lower) / 2;
    auto p = block(middle);
    if (key(p) <= word) lower = middle + 1;
    else upper = middle;
  }
  if (!lower) return false;
  int chosen = 0;
  if (const auto it = variants_.find(word); it != variants_.end())
    chosen = opensSentence && it->second.opening >= 0 ? it->second.opening : it->second.any;
  auto p = block(lower - 1);
  scratch.fill(0);
  const uint32_t count = std::min(lexiconStride_, lexiconWords_ - (lower - 1) * lexiconStride_);
  for (uint32_t i = 0; i < count; ++i) {
    const auto candidate = key(p);
    if (candidate > word) return false;
    const unsigned variants = *p++;
    const bool found = candidate == word;
    const unsigned pick = std::min(static_cast<unsigned>(chosen), variants - 1);
    for (unsigned variant = 0; variant < variants; ++variant) {
      const unsigned phones = *p++;
      unsigned used = 0;
      const auto bits = [&](unsigned n) {
        unsigned value = 0;
        for (unsigned b = 0; b < n; ++b, ++used) value |= ((p[used / 8] >> (used % 8)) & 1u) << b;
        return value;
      };
      for (unsigned k = 0; k < phones; ++k) {
        const unsigned id = bits(6);
        const unsigned stress = vowel(id) ? bits(2) : 0;
        if (found && variant == pick) out.phones[k] = {static_cast<PhoneId>(id), static_cast<uint8_t>(stress)};
      }
      p += (used + 7) / 8;
      if (found && variant == pick) out.count = phones;
    }
    if (found) return true;
  }
  return false;
}

bool Frontend::matches(const Rule& rule, const std::u32string& word, std::size_t at) const {
  if (word.compare(at, rule.letters.size(), rule.letters) != 0) return false;
  const auto fits = [](const Context& c, char32_t ch) {
    switch (c.kind) {
      case Context::Any: return true;
      case Context::In: return c.set.find(ch) != std::u32string::npos;
      case Context::NotIn: return c.set.find(ch) == std::u32string::npos;
      default: return false;
    }
  };
  std::size_t j = at + rule.letters.size();
  for (const Context& c : rule.right) {
    if (c.kind == Context::Boundary) {
      if (j != word.size()) return false;
      continue;
    }
    if (j >= word.size() || !fits(c, word[j])) return false;
    ++j;
  }
  std::size_t k = at;
  for (auto it = rule.left.rbegin(); it != rule.left.rend(); ++it) {
    if (it->kind == Context::Boundary) {
      if (k != 0) return false;
      continue;
    }
    if (k == 0 || !fits(*it, word[k - 1])) return false;
    --k;
  }
  return true;
}

void Frontend::stress(const std::u32string& word, const std::vector<std::size_t>& phonesAt, Word& out) const {
  if (!stressMinLetters_ || word.size() < stressMinLetters_) return;
  std::size_t from = 0;
  for (const auto& prefix : unstressedPrefixes_) {
    if (word.size() > prefix.size() + 2 && word.compare(0, prefix.size(), prefix) == 0 &&
        phonesAt[prefix.size()] != SIZE_MAX) {
      from = phonesAt[prefix.size()];
      break;
    }
  }
  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t i = pass ? 0 : from; i < out.count; ++i) {
      if (vowel(static_cast<std::size_t>(out.phones[i].sound))) {
        out.phones[i].prosody |= Primary;
        return;
      }
    }
  }
}

bool Frontend::rules(const std::u32string& word, Word& out) const {
  out.count = 0;
  bool stressed = false;
  std::vector<std::size_t> phonesAt(word.size() + 1, SIZE_MAX);
  if (!word.empty() && onsetLetters_.find(word[0]) != std::u32string::npos)
    for (const Phone& phone : onsetPhones_) out.phones[out.count++] = phone;
  for (std::size_t i = 0; i < word.size();) {
    phonesAt[i] = out.count;
    const Rule* hit = nullptr;
    if (const auto it = rulesByLetter_.find(word[i]); it != rulesByLetter_.end()) {
      for (const std::size_t r : it->second) {
        if (matches(rules_[r], word, i)) {
          hit = &rules_[r];
          break;
        }
      }
    }
    if (!hit) {
      ++i;
      continue;
    }
    for (const Phone& phone : hit->phones) {
      if (out.count == out.phones.size()) return false;
      out.phones[out.count++] = phone;
    }
    stressed = stressed || hit->stressed;
    i += hit->letters.size();
  }
  phonesAt[word.size()] = out.count;
  if (!stressed) stress(word, phonesAt, out);
  return out.count != 0;
}

bool Frontend::pronounce(std::string_view word, bool opensSentence, Word& out) const {
  out.count = 0;
  if (lookup(word, opensSentence, out)) return true;
  std::u32string letters;
  if (utf32(word, letters) && rules(letters, out)) return true;
  out.count = 0;
  return false;
}

namespace {

class Builder {
 public:
  Builder(const Frontend& frontend, Plan& plan) : frontend_(frontend), plan_(plan) {}
  bool full() const { return full_; }

  // A word that cannot be said is left out; one that no longer fits ends the plan.
  void word(std::string_view word) {
    Frontend::Word pronunciation;
    const bool opensSentence = opensSentence_;
    opensSentence_ = false;
    if (full_ || !frontend_.pronounce(word, opensSentence, pronunciation)) return;
    if (plan_.count + pronunciation.count >= kMaxPhones) {
      full_ = true;
      return;
    }
    std::copy_n(pronunciation.phones.begin(), pronunciation.count, plan_.phones.begin() + plan_.count);
    plan_.count += pronunciation.count;
    plan_.phones[plan_.count - 1].prosody |= WordEnd;
  }

  void say(std::string_view text) {
    for (const auto w : words(text)) word(w);
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

 private:
  const Frontend& frontend_;
  Plan& plan_;
  bool full_ = false;
  bool opensSentence_ = true;
};

std::string timeWords(std::string_view pattern, std::string_view hour, std::string_view minutes) {
  std::string out;
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    if (pattern.compare(i, 3, "{h}") == 0) {
      out += hour;
      i += 2;
    } else if (pattern.compare(i, 3, "{m}") == 0) {
      out += minutes;
      i += 2;
    } else {
      out += pattern[i];
    }
  }
  return out;
}

}

bool Frontend::prepare(std::string_view text, Plan& out) const {
  out.count = 0;
  out.boundaries.fill(0);
  const std::string folded = fold(text);
  const std::size_t size = folded.size();
  Builder builder(*this, out);
  const auto letterAt = [&](std::size_t i, std::size_t* length) {
    std::size_t at = i;
    const uint32_t c = awtrix::text::nextCodepoint(folded, at);
    if (length) *length = at - i;
    return c != awtrix::text::kInvalidCodepoint && letter(c);
  };
  for (std::size_t i = 0; i < size && !builder.full();) {
    const char c = folded[i];
    std::size_t length = 0;
    if (letterAt(i, &length)) {
      const auto begin = i;
      i += length;
      while (i < size && (folded[i] == '\'' || letterAt(i, &length))) i += folded[i] == '\'' ? 1 : length;
      auto end = i;
      while (end > begin && folded[end - 1] == '\'') --end;
      builder.word(std::string_view(folded).substr(begin, end - begin));
    } else if (digit(c)) {
      const auto begin = i;
      uint32_t value = 0;
      std::size_t digits = 0;
      const auto take = [&]() {
        if (digits < 6) value = value * 10 + static_cast<uint32_t>(folded[i] - '0');
        ++digits;
        ++i;
      };
      while (i < size && digit(folded[i])) take();
      // 1.000 in languages that group thousands: the group sign before exactly three digits.
      while (group_ && i + 3 < size && folded[i] == group_ && digit(folded[i + 1]) && digit(folded[i + 2]) &&
             digit(folded[i + 3]) && (i + 4 == size || !digit(folded[i + 4]))) {
        ++i;
        for (int k = 0; k < 3; ++k) take();
      }
      // A time, h:mm or hh:mm.
      if (i - begin <= 2 && value < 24 && i + 2 < size && timeSeparators_.find(folded[i]) != std::string::npos &&
          digit(folded[i + 1]) && digit(folded[i + 2]) && (i + 3 == size || !digit(folded[i + 3]))) {
        const uint32_t minutes = (folded[i + 1] - '0') * 10u + (folded[i + 2] - '0');
        if (minutes < 60) {
          const std::string hour = value == 1 && !hourOne_.empty() ? hourOne_ : number(value);
          const std::string& pattern = !minutes ? timeExact_ : minutes < 10 ? timeEarly_ : timeOther_;
          builder.say(timeWords(pattern, hour, minutes ? number(minutes) : std::string()));
          i += 3;
          if (!timeWord_.empty()) {
            std::size_t j = i, after = 0;
            while (j < size && folded[j] == ' ') ++j;
            if (folded.compare(j, timeWord_.size(), timeWord_) == 0 &&
                (j + timeWord_.size() == size || !letterAt(j + timeWord_.size(), &after)))
              i = j + timeWord_.size();
          }
          continue;
        }
      }
      if (digits <= 6) builder.say(number(value));
      else for (auto j = begin; j < i; ++j) if (digit(folded[j])) builder.say(units_[folded[j] - '0']);
      if (decimal_ && i + 1 < size && folded[i] == decimal_ && digit(folded[i + 1])) {
        builder.say(decimalWord_);
        ++i;
        while (i < size && digit(folded[i])) builder.say(units_[folded[i++] - '0']);
      }
    } else {
      bool wordStart = i == 0;
      if (!wordStart) {
        std::size_t back = i - 1;
        while (back > 0 && (static_cast<unsigned char>(folded[back]) & 0xC0) == 0x80) --back;
        std::size_t len = 0;
        wordStart = !(digit(folded[i - 1]) || letterAt(back, &len));
      }
      if (c == '?' || c == '!' || c == '.' || c == ',' || c == ';' || c == ':') builder.boundary(c);
      else if (c == '\n') builder.boundary(',');
      else if (c == '-' && wordStart && i + 1 < size && digit(folded[i + 1])) builder.say(minus_);
      else if (const auto it = symbols_.find(c); it != symbols_.end()) builder.say(it->second);
      ++i;
    }
  }
  builder.boundary();
  return out.count != 0;
}

bool Frontend::parsePhones(std::string_view list, std::vector<Phone>& out, bool& stressed, std::string& error) const {
  out.clear();
  stressed = false;
  if (list == "-") return true;
  for (auto name : words(list)) {
    uint8_t prosody = 0;
    if (name.size() > 1 && (name.back() == '1' || name.back() == '2')) {
      prosody = static_cast<uint8_t>(name.back() - '0');
      name.remove_suffix(1);
      stressed = true;
    }
    std::size_t id = 0;
    while (id < phones_.size() && phones_[id].name != name) ++id;
    if (id == phones_.size() || id == 0) {
      error = "unknown phone " + std::string(name);
      return false;
    }
    out.push_back({static_cast<PhoneId>(id), prosody});
  }
  return true;
}

bool Frontend::parseLine(const std::string& section, std::string_view line, std::string& error) {
  const auto f = fields(line);
  const auto fail = [&](const std::string& why) {
    error = section + ": " + why;
    return false;
  };
  if (section == "phones") {
    if (f.size() != 2 || f[0].empty() || f[0].size() > 8 || (f[1] != "vowel" && f[1] != "consonant" && f[1] != "silence"))
      return fail("bad line");
    if ((f[1] == "silence") != phones_.empty()) return fail("silence must come first, once");
    for (const char ch : f[0])
      if (ch <= ' ' || ch > '~' || digit(ch)) return fail("bad phone name");
    if (phones_.size() == kMaxPhonesInInventory) return fail("too many phones");
    phones_.push_back({std::string(f[0]), f[1] == "vowel"});
    return true;
  }
  if (section == "letters") {
    std::u32string add;
    for (const auto w : words(line)) {
      if (!utf32(w, add) || add.size() != 1 || add[0] < 0x80) return fail("bad letter");
      letters_ += add;
    }
    return true;
  }
  if (section == "fold") {
    if (f.size() < 2 || f.size() > 3 || f[0].empty() || static_cast<unsigned char>(f[0][0]) < 0x80 ||
        (f.size() == 3 && f[2] != "alone"))
      return fail("bad line");
    folds_.push_back({std::string(f[0]), std::string(f[1]), f.size() == 3});
    return true;
  }
  if (section == "symbols") {
    if (f.size() != 2 || f[0].size() != 1 || static_cast<unsigned char>(f[0][0]) >= 0x80 || f[1].empty())
      return fail("bad line");
    symbols_[f[0][0]] = std::string(f[1]);
    return true;
  }
  if (section == "numbers") {
    uint32_t n = 0;
    if (f.size() == 2 && parseNumber(f[0], 0, 99, n)) units_[n] = std::string(f[1]);
    else if (f.size() == 2 && f[0].size() == 9 && f[0].substr(0, 8) == "hundred " && parseNumber(f[0].substr(8), 1, 9, n))
      hundreds_[n] = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "thousand") thousand_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "one thousand") oneThousand_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "thousand join" && (f[1] == "space" || f[1] == "none"))
      thousandJoin_ = f[1] == "space" ? " " : "";
    else if (f.size() == 2 && f[0] == "rest join" && (f[1] == "space" || f[1] == "none"))
      restJoin_ = f[1] == "space" ? " " : "";
    else if (f.size() == 3 && f[0] == "decimal" && f[1].size() == 1) {
      decimal_ = f[1][0];
      decimalWord_ = std::string(f[2]);
    } else if (f.size() == 2 && f[0] == "group" && f[1].size() == 1) group_ = f[1][0];
    else if (f.size() == 2 && f[0] == "minus") minus_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "time") timeSeparators_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "time exact") timeExact_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "time early") timeEarly_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "time other") timeOther_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "hour one") hourOne_ = std::string(f[1]);
    else if (f.size() == 2 && f[0] == "time word") timeWord_ = std::string(f[1]);
    else return fail("bad line");
    return true;
  }
  if (section == "stress") {
    uint32_t n = 0;
    std::u32string prefix;
    if (f.size() == 2 && f[0] == "first" && parseNumber(f[1], 1, 64, n)) stressMinLetters_ = n;
    else if (f.size() == 2 && f[0] == "prefix" && utf32(f[1], prefix) && !prefix.empty())
      unstressedPrefixes_.push_back(prefix);
    else if (f.size() == 3 && f[0] == "onset" && utf32(f[1], onsetLetters_) && !onsetLetters_.empty()) {
      bool stressed = false;
      std::string why;
      if (!parsePhones(f[2], onsetPhones_, stressed, why) || onsetPhones_.size() > 4 || stressed) return fail("bad onset");
    } else return fail("bad line");
    return true;
  }
  if (section == "variants") {
    uint32_t n = 0;
    if ((f.size() != 2 && f.size() != 3) || f[0].empty() || !parseNumber(f[1], 0, 15, n) ||
        (f.size() == 3 && f[2] != "opening"))
      return fail("bad line");
    Variant& v = variants_[std::string(f[0])];
    if (f.size() == 3) v.opening = static_cast<int>(n);
    else v.any = static_cast<int>(n);
    return true;
  }
  if (section == "rules") {
    if (f.size() != 4) return fail("bad line");
    Rule rule;
    if (!utf32(f[0], rule.letters) || rule.letters.empty() || rule.letters.size() > 8) return fail("bad letters");
    const auto context = [&](std::string_view text, std::vector<Context>& out) {
      if (text == "-") return true;
      std::u32string cs;
      if (!utf32(text, cs)) return false;
      for (std::size_t i = 0; i < cs.size(); ++i) {
        Context c;
        if (cs[i] == U'#') c.kind = Context::Boundary;
        else if (cs[i] == U'?') c.kind = Context::Any;
        else if (cs[i] == U'[') {
          const auto close = cs.find(U']', i);
          if (close == std::u32string::npos) return false;
          const bool negate = i + 1 < close && cs[i + 1] == U'^';
          c.kind = negate ? Context::NotIn : Context::In;
          c.set = cs.substr(i + 1 + negate, close - i - 1 - negate);
          if (c.set.empty()) return false;
          i = close;
        } else {
          c.kind = Context::In;
          c.set = cs.substr(i, 1);
        }
        out.push_back(c);
      }
      return out.size() <= kMaxContext;
    };
    if (!context(f[1], rule.left) || !context(f[2], rule.right)) return fail("bad context");
    std::string why;
    if (!parsePhones(f[3], rule.phones, rule.stressed, why)) return fail(why);
    if (rules_.size() == kMaxRules) return fail("too many rules");
    rulesByLetter_[rule.letters[0]].push_back(rules_.size());
    rules_.push_back(std::move(rule));
    return true;
  }
  return fail("unknown section");
}

std::unique_ptr<const Frontend> Frontend::parse(std::string_view text, std::string_view lexicon, std::string& error) {
  std::unique_ptr<Frontend> frontend(new Frontend);
  std::string section;
  std::size_t lineNumber = 0;
  for (std::size_t start = 0; start < text.size();) {
    auto end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    auto line = text.substr(start, end - start);
    start = end + 1;
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line[0] == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = std::string(line.substr(1, line.size() - 2));
      continue;
    }
    std::string why;
    if (!frontend->parseLine(section, line, why)) {
      error = "fe.text line " + std::to_string(lineNumber) + ": " + why;
      return nullptr;
    }
  }
  if (frontend->phones_.size() < 2) {
    error = "fe.text: no phones";
    return nullptr;
  }
  for (std::size_t n = 0; n < 100; ++n)
    if (frontend->units_[n].empty()) {
      error = "fe.text: numbers: no word for " + std::to_string(n);
      return nullptr;
    }
  for (std::size_t n = 1; n < 10; ++n)
    if (frontend->hundreds_[n].empty()) {
      error = "fe.text: numbers: no word for hundred " + std::to_string(n);
      return nullptr;
    }
  if (frontend->thousand_.empty() || frontend->timeExact_.empty() || frontend->timeEarly_.empty() ||
      frontend->timeOther_.empty() || frontend->minus_.empty()) {
    error = "fe.text: numbers incomplete";
    return nullptr;
  }
  std::stable_sort(frontend->folds_.begin(), frontend->folds_.end(),
                   [](const Fold& a, const Fold& b) { return a.from.size() > b.from.size(); });
  if (!lexicon.empty()) {
    const auto* p = reinterpret_cast<const uint8_t*>(lexicon.data());
    if (lexicon.size() < 16 || std::memcmp(p, "LXR1", 4) != 0) {
      error = "fe.lexicon: not a lexicon";
      return nullptr;
    }
    const uint32_t stride = p[4] | (p[5] << 8), phones = p[6], words = word32(p + 8), blocks = word32(p + 12);
    if (phones != frontend->phones_.size() || !stride || blocks != (words + stride - 1) / stride ||
        16 + uint64_t{blocks} * 4 > lexicon.size()) {
      error = "fe.lexicon: bad header";
      return nullptr;
    }
    // Every entry is walked once here, so lookup() reads an uploaded lexicon without further checks.
    const std::size_t body = 16 + std::size_t{blocks} * 4;
    for (uint32_t b = 0; b < blocks; ++b) {
      std::size_t at = body + word32(p + 16 + b * 4);
      const std::size_t end = b + 1 < blocks ? body + word32(p + 16 + (b + 1) * 4) : lexicon.size();
      const auto byte = [&](unsigned& value) {
        if (at >= end) return false;
        value = p[at++];
        return true;
      };
      bool ok = at < end && end <= lexicon.size();
      unsigned keyBytes = 0;
      const uint32_t count = std::min(stride, words - b * stride);
      for (uint32_t i = 0; ok && i < count; ++i) {
        unsigned lengths = 0, prefix = 0, suffix = 0, variants = 0;
        ok = byte(lengths);
        prefix = lengths >> 4;
        suffix = lengths & 15;
        if (ok && prefix == 15) ok = byte(prefix);
        if (ok && suffix == 15) ok = byte(suffix);
        ok = ok && prefix <= keyBytes && prefix + suffix < kMaxWordBytes && suffix <= end - at;
        if (!ok) break;
        at += suffix;
        keyBytes = prefix + suffix;
        ok = byte(variants) && variants != 0;
        for (unsigned variant = 0; ok && variant < variants; ++variant) {
          unsigned length = 0;
          ok = byte(length) && length != 0 && length <= kMaxWordPhones;
          std::size_t used = 0;
          const auto bits = [&](unsigned n, unsigned& value) {
            if ((used + n + 7) / 8 > end - at) return false;
            value = 0;
            for (unsigned k = 0; k < n; ++k, ++used) value |= ((p[at + used / 8] >> (used % 8)) & 1u) << k;
            return true;
          };
          for (unsigned k = 0; ok && k < length; ++k) {
            unsigned id = 0, stress = 0;
            ok = bits(6, id) && id < frontend->phones_.size() && (!frontend->vowel(id) || bits(2, stress));
          }
          at += (used + 7) / 8;
        }
      }
      if (!ok) {
        error = "fe.lexicon: bad block";
        return nullptr;
      }
    }
    frontend->lexicon_ = lexicon;
    frontend->lexiconWords_ = words;
    frontend->lexiconBlocks_ = blocks;
    frontend->lexiconStride_ = stride;
  }
  return frontend;
}

}

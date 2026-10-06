#include "core/synth/SongParser.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace awtrix {
namespace synth {
namespace {

constexpr std::size_t kMaxNameLength = 16;
constexpr std::size_t kMaxDepth = 4;
constexpr uint32_t kMaxRepeat = 256;
constexpr uint32_t kMaxLengthSixteenths = 256;
// Items a song may take to expand, repeats included: a bound on the parser's work, far above
// what kMaxNotes and kMaxBars let a real song use.
constexpr uint32_t kMaxWork = 200000;
constexpr int kDefaultOctave = 4;
constexpr uint32_t kDefaultLength = 4 * kTicksPerSixteenth;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_' || c == '-';
}

int semitone(char letter) {
  switch (letter) {
    case 'c': return 0;
    case 'd': return 2;
    case 'e': return 4;
    case 'f': return 5;
    case 'g': return 7;
    case 'a': return 9;
    case 'b': return 11;
    default: return -1;
  }
}

struct Span {
  std::size_t begin = 0;
  std::size_t end = 0;

  bool empty() const { return begin == end; }
  int size() const { return static_cast<int>(end - begin); }
};

struct Setting {
  const char* name;
  int32_t low;
  int32_t high;
};

struct Field {
  const char* name;
  float Patch::*member;
  int32_t low;
  int32_t high;
  bool percent;
  uint8_t part;
  uint16_t fallback;
};

enum : uint8_t { kBpm, kBeats, kLoop, kVolume, kEcho };

const Setting kSettings[] = {
    {"bpm", 2000, 30000}, {"beats", 100, 1600}, {"loop", 100, 100 * kMaxBars}, {"volume", 0, 20000}};

const Setting kEchoSettings[] = {{"time", 25, 6400}, {"feedback", 0, 9000}, {"damp", 20000, 1600000}};

const Field kFields[] = {
    {"duty", &Patch::duty, 100, 9900, true, 0, 0},
    {"unison", &Patch::unison, 0, 10000, false, 0, 0},
    {"sub", &Patch::sub, 0, 10000, true, 0, 0},
    {"noise", &Patch::noise, 0, 10000, true, 0, 0},
    {"noise decay", &Patch::noiseDecayMs, 0, 1000000, false, 1, 0},
    {"attack", &Patch::attackMs, 0, 1000000, false, 0, 0},
    {"decay", &Patch::decayMs, 100, 2000000, false, 0, 0},
    {"sustain", &Patch::sustain, 0, 10000, true, 0, 0},
    {"release", &Patch::releaseMs, 0, 2000000, false, 0, 0},
    {"pitch", &Patch::pitchSemis, -4800, 4800, false, 0, 0},
    {"pitch time", &Patch::pitchMs, 100, 500000, false, 1, 30},
    {"vibrato", &Patch::vibratoCents, 0, 20000, false, 0, 0},
    {"vibrato rate", &Patch::vibratoHz, 10, 2000, false, 1, 0},
    {"vibrato delay", &Patch::vibratoDelayMs, 0, 500000, false, 2, 0},
    {"glide", &Patch::glideMs, 0, 500000, false, 0, 0},
    {"cutoff", &Patch::cutoffHz, 2000, 2000000, false, 0, 0},
    {"resonance", &Patch::resonance, 0, 10000, true, 0, 0},
    {"filterenv", &Patch::filterEnvHz, -2000000, 2000000, false, 0, 0},
    {"filterenv time", &Patch::filterEnvMs, 100, 1000000, false, 1, 50},
    {"drive", &Patch::drive, 0, 10000, true, 0, 0},
    {"volume", &Patch::volume, 0, 20000, true, 0, 0},
    {"echo", &Patch::echo, 0, 10000, true, 0, 0},
    {"gate", &Patch::gate, 100, 10000, true, 0, 0},
};

const char* const kWaves[] = {"pulse", "saw", "tri", "sine", "noise"};
const char* const kFilters[] = {"lp", "bp", "hp"};

void decimal(char* out, std::size_t size, int32_t hundredths) {
  const char* sign = hundredths < 0 ? "-" : "";
  const uint32_t v = static_cast<uint32_t>(hundredths < 0 ? -hundredths : hundredths);
  const unsigned whole = v / 100;
  const unsigned fraction = v % 100;
  if (!fraction) std::snprintf(out, size, "%s%u", sign, whole);
  else if (fraction % 10 == 0) std::snprintf(out, size, "%s%u.%u", sign, whole, fraction / 10);
  else std::snprintf(out, size, "%s%u.%02u", sign, whole, fraction);
}

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  ParseResult run() {
    ParseResult result;
    if (s_.size() > kMaxSongBytes) {
      fail(kMaxSongBytes, "at most %u bytes", static_cast<unsigned>(kMaxSongBytes));
    } else {
      Span line;
      for (std::size_t at = 0; !failed_ && statement(at, line);)
        if (!line.empty() && !isTrackLine(line)) setting(line);
      for (std::size_t at = 0; !failed_ && statement(at, line);)
        if (isTrackLine(line)) trackLine(line);
      if (!failed_) finish();
    }
    if (failed_) {
      result.error = error_;
      result.line = 1;
      std::size_t lineStart = 0;
      for (std::size_t i = 0; i < errorAt_ && i < s_.size(); ++i)
        if (s_[i] == '\n') {
          ++result.line;
          lineStart = i + 1;
        }
      result.column = errorAt_ - lineStart + 1;
      return result;
    }
    result.song = std::make_shared<const Song>(std::move(song_));
    return result;
  }

 private:
  struct TrackState {
    std::size_t index = 0;
    uint32_t tick = 0;
    int octave = kDefaultOctave;
    uint32_t length = kDefaultLength;
    uint8_t velocity = 100;
    int transpose = 0;
    int instrument = -1;
    // The notes the latest note, chord or step started: what '_' holds on.
    std::size_t lastBegin = 0;
    std::size_t lastEnd = 0;
  };

  // What a repeat restores before each pass, so every pass sounds the same.
  struct Sticky {
    int octave;
    uint32_t length;
    uint8_t velocity;
    int transpose;
    int instrument;
  };

  __attribute__((format(printf, 3, 4))) bool fail(std::size_t at, const char* format, ...) {
    if (failed_) return false;
    failed_ = true;
    errorAt_ = at;
    va_list args;
    va_start(args, format);
    va_list again;
    va_copy(again, args);
    const int size = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    error_.assign(size > 0 ? static_cast<std::size_t>(size) : 0, '\0');
    std::vsnprintf(&error_[0], error_.size() + 1, format, again);
    va_end(again);
    return false;
  }

  const char* at(Span span) const { return s_.data() + span.begin; }

  bool is(Span span, const char* word) const {
    const std::size_t size = std::strlen(word);
    return span.end - span.begin == size && std::memcmp(at(span), word, size) == 0;
  }

  bool is(Span span, const std::string& word) const {
    return span.end - span.begin == word.size() && std::memcmp(at(span), word.data(), word.size()) == 0;
  }

  // A statement ends at '\n' or ';'. A comment starts at a '#' that begins a statement or follows
  // a space, and runs to the end of the line - so c#4 stays a note.
  bool statement(std::size_t& from, Span& out) const {
    const std::size_t start = from;
    std::size_t cut = std::string::npos;
    for (std::size_t i = start; i <= s_.size(); ++i) {
      const char c = i < s_.size() ? s_[i] : '\n';
      if (cut != std::string::npos) {
        if (c != '\n') continue;
        out = trim({start, cut});
        from = i + 1;
        return true;
      }
      if (c == '#' && (i == start || isSpace(s_[i - 1]))) {
        cut = i;
      } else if (c == '\n' || c == ';') {
        out = trim({start, i});
        from = i + 1;
        return true;
      }
    }
    return false;
  }

  Span trim(Span span) const {
    while (span.begin < span.end && isSpace(s_[span.begin])) ++span.begin;
    while (span.end > span.begin && isSpace(s_[span.end - 1])) --span.end;
    return span;
  }

  bool isTrackLine(Span line) const {
    std::size_t i = line.begin;
    while (i < line.end && isNameChar(s_[i])) ++i;
    return i > line.begin && i < line.end && s_[i] == ':';
  }

  Span word(std::size_t& i, std::size_t end) const {
    while (i < end && isSpace(s_[i])) ++i;
    const std::size_t start = i;
    while (i < end && !isSpace(s_[i])) ++i;
    return {start, i};
  }

  bool validName(Span name, const char* what) {
    const std::size_t size = name.end - name.begin;
    if (size == 0 || size > kMaxNameLength) return fail(name.begin, "invalid %s name", what);
    for (std::size_t i = name.begin; i < name.end; ++i)
      if (!isNameChar(s_[i])) return fail(i, "invalid %s name", what);
    return true;
  }

  // An optional '-', digits and an optional fraction.
  bool number(Span span, double& out) {
    std::size_t i = span.begin;
    bool negative = false;
    if (i < span.end && s_[i] == '-') {
      negative = true;
      ++i;
    }
    double value = 0;
    std::size_t digits = 0;
    while (i < span.end && isDigit(s_[i])) {
      value = value * 10 + (s_[i] - '0');
      ++i;
      ++digits;
    }
    if (i < span.end && s_[i] == '.') {
      ++i;
      double scale = 0.1;
      while (i < span.end && isDigit(s_[i])) {
        value += (s_[i] - '0') * scale;
        scale /= 10;
        ++i;
        ++digits;
      }
    }
    if (!digits || i != span.end || digits > 12) return fail(span.begin, "expected a number");
    out = negative ? -value : value;
    return true;
  }

  bool ranged(Span span, int32_t low, int32_t high, const char* what, double& out) {
    if (!number(span, out)) return false;
    if (out < low / 100.0 || out > high / 100.0) {
      char from[16], to[16];
      decimal(from, sizeof from, low);
      decimal(to, sizeof to, high);
      return fail(span.begin, "%s must be between %s and %s", what, from, to);
    }
    return true;
  }

  bool whole(Span span, int32_t low, int32_t high, const char* what, double& out) {
    if (!ranged(span, low, high, what, out)) return false;
    if (out != static_cast<double>(static_cast<int32_t>(out)))
      return fail(span.begin, "%s must be a whole number", what);
    return true;
  }

  bool pitchName(Span span, int& out) {
    std::size_t i = span.begin;
    const int base = i < span.end ? semitone(s_[i]) : -1;
    if (base < 0) return fail(span.begin, "expected a note");
    ++i;
    int value = base;
    if (i < span.end && (s_[i] == '#' || s_[i] == 'b')) value += s_[i++] == '#' ? 1 : -1;
    if (i + 1 != span.end || !isDigit(s_[i])) return fail(span.begin, "expected a note");
    value += 12 * (s_[i] - '0' + 1);
    if (value < 0 || value > 127) return fail(span.begin, "note out of range");
    out = value;
    return true;
  }

  void setting(Span line) {
    std::size_t i = line.begin;
    const Span key = word(i, line.end);
    if (is(key, "inst")) {
      instrument(line, i);
      return;
    }
    if (is(key, "echo")) {
      echo(key, i, line.end);
      return;
    }
    std::size_t which = 0;
    while (which < kEcho && !is(key, kSettings[which].name)) ++which;
    if (which == kEcho) {
      fail(key.begin, "unknown statement '%.*s'", key.size(), at(key));
      return;
    }
    const Span value = word(i, line.end);
    const Span extra = word(i, line.end);
    if (value.empty() || !extra.empty()) {
      fail(value.empty() ? key.end : extra.begin, "'%.*s' takes one value", key.size(), at(key));
      return;
    }
    if (!once(which, key)) return;
    if (which == kLoop && is(value, "off")) {
      song_.loops = false;
      return;
    }
    const Setting& s = kSettings[which];
    double v = 0;
    const bool counted = which == kBeats || which == kLoop;
    if (!(counted ? whole(value, s.low, s.high, s.name, v) : ranged(value, s.low, s.high, s.name, v)))
      return;
    if (which == kBpm) {
      song_.bpm = static_cast<float>(v);
    } else if (which == kBeats) {
      song_.beatsPerBar = static_cast<uint8_t>(v);
    } else if (which == kLoop) {
      loopBar_ = static_cast<uint32_t>(v);
      loopAt_ = value.begin;
    } else {
      song_.volume = static_cast<float>(v / 100);
    }
  }

  bool once(std::size_t which, Span keyword) {
    const unsigned bit = 1u << which;
    if (seen_ & bit) return fail(keyword.begin, "'%.*s' is set twice", keyword.size(), at(keyword));
    seen_ |= bit;
    return true;
  }

  bool pair(Span word, Span& key, Span& value) {
    std::size_t eq = word.begin;
    while (eq < word.end && s_[eq] != '=') ++eq;
    if (eq == word.end || eq == word.begin || eq + 1 == word.end)
      return fail(word.begin, "expected key=value");
    key = {word.begin, eq};
    value = {eq + 1, word.end};
    return true;
  }

  void echo(Span keyword, std::size_t i, std::size_t end) {
    if (!once(kEcho, keyword)) return;
    bool timed = false;
    for (Span w = word(i, end); !w.empty(); w = word(i, end)) {
      Span key, value;
      if (!pair(w, key, value)) return;
      std::size_t which = 0;
      while (which < 3 && !is(key, kEchoSettings[which].name)) ++which;
      if (which == 3) {
        fail(key.begin, "unknown echo setting '%.*s'", key.size(), at(key));
        return;
      }
      const Setting& s = kEchoSettings[which];
      double v = 0;
      if (!ranged(value, s.low, s.high, s.name, v)) return;
      if (which == 0) {
        song_.echoSixteenths = static_cast<float>(v);
        echoAt_ = value.begin;
        timed = true;
      } else if (which == 1) {
        song_.echoFeedback = static_cast<float>(v / 100);
      } else {
        song_.echoDampHz = static_cast<float>(v);
      }
    }
    if (!timed) fail(keyword.begin, "echo needs time");
  }

  void instrument(Span line, std::size_t i) {
    const Span name = word(i, line.end);
    if (name.empty()) {
      fail(line.end, "inst needs a name");
      return;
    }
    if (!validName(name, "instrument")) return;
    for (const Instrument& other : song_.instruments)
      if (is(name, other.name)) {
        fail(name.begin, "instrument '%.*s' is defined twice", name.size(), at(name));
        return;
      }
    if (song_.instruments.size() >= kMaxInstruments) {
      fail(name.begin, "at most %u instruments", static_cast<unsigned>(kMaxInstruments));
      return;
    }
    Instrument inst;
    inst.name.assign(at(name), name.end - name.begin);
    bool filterSet = false;
    for (Span w = word(i, line.end); !w.empty(); w = word(i, line.end)) {
      Span key, value;
      if (!pair(w, key, value) || !instrumentKey(inst.patch, key, value, filterSet)) return;
    }
    if (!filterSet && inst.patch.cutoffHz < 20000.0f) inst.patch.filter = Filter::LowPass;
    song_.instruments.push_back(std::move(inst));
  }

  int choice(Span value, const char* const* names, std::size_t count) const {
    for (std::size_t n = 0; n < count; ++n)
      if (is(value, names[n])) return static_cast<int>(n);
    return -1;
  }

  // Up to three numbers joined by '/', the first one required.
  bool instrumentKey(Patch& patch, Span key, Span value, bool& filterSet) {
    Span parts[4];
    std::size_t count = 0;
    std::size_t start = value.begin;
    for (std::size_t i = value.begin; i <= value.end; ++i)
      if (i == value.end || s_[i] == '/') {
        if (count < 4) parts[count] = {start, i};
        ++count;
        start = i + 1;
      }
    const Field* field = nullptr;
    for (const Field& f : kFields)
      if (f.part == 0 && is(key, f.name)) field = &f;
    std::size_t allowed = 1;
    while (field && field + allowed < std::end(kFields) && field[allowed].part == allowed) ++allowed;
    if (count > allowed)
      return fail(parts[allowed].begin, "'%.*s' takes at most %u %s", key.size(), at(key),
                  static_cast<unsigned>(allowed), allowed == 1 ? "value" : "values joined by /");
    if (is(key, "wave")) {
      const int wave = choice(value, kWaves, 5);
      if (wave < 0) return fail(value.begin, "wave is pulse, saw, tri, sine or noise");
      patch.wave = static_cast<Wave>(wave);
      return true;
    }
    if (is(key, "filter")) {
      const int filter = choice(value, kFilters, 3);
      if (filter < 0) return fail(value.begin, "filter is lp, bp or hp");
      patch.filter = static_cast<Filter>(filter + 1);
      filterSet = true;
      return true;
    }
    if (is(key, "note")) {
      int pitch = 0;
      if (!pitchName(value, pitch)) return false;
      patch.stepPitch = static_cast<uint8_t>(pitch);
      return true;
    }
    if (!field) return fail(key.begin, "unknown instrument setting '%.*s'", key.size(), at(key));
    for (std::size_t n = 0; n < allowed; ++n) {
      const Field& f = field[n];
      if (n >= count) {
        if (f.fallback) patch.*f.member = f.fallback;
        continue;
      }
      double v = 0;
      if (!ranged(parts[n], f.low, f.high, f.name, v)) return false;
      patch.*f.member = static_cast<float>(f.percent ? v / 100 : v);
    }
    return true;
  }

  TrackState* track(Span name) {
    for (TrackState& state : states_)
      if (is(name, song_.tracks[state.index].name)) return &state;
    if (song_.tracks.size() >= kMaxTracks) {
      fail(name.begin, "at most %u tracks", static_cast<unsigned>(kMaxTracks));
      return nullptr;
    }
    if (!validName(name, "track")) return nullptr;
    Track t;
    t.name.assign(at(name), name.end - name.begin);
    song_.tracks.push_back(std::move(t));
    TrackState state;
    state.index = song_.tracks.size() - 1;
    state.instrument = instrumentIndex(name);
    states_.push_back(state);
    return &states_.back();
  }

  int instrumentIndex(Span name) const {
    for (std::size_t i = 0; i < song_.instruments.size(); ++i)
      if (is(name, song_.instruments[i].name)) return static_cast<int>(i);
    return -1;
  }

  void trackLine(Span line) {
    std::size_t colon = line.begin;
    while (s_[colon] != ':') ++colon;
    TrackState* state = track({line.begin, colon});
    if (!state) return;
    sequence(*state, colon + 1, line.end, 0);
  }

  bool delimiter(std::size_t i, std::size_t end) const {
    return i >= end || isSpace(s_[i]) || s_[i] == '|' || s_[i] == '(' || s_[i] == ')' ||
           s_[i] == '[';
  }

  bool separated(std::size_t i, std::size_t end, std::size_t itemAt) {
    if (delimiter(i, end)) return true;
    return fail(i, "expected a space after '%.*s'", static_cast<int>(i - itemAt), s_.data() + itemAt);
  }

  bool unsignedInt(std::size_t& i, std::size_t end, uint32_t& out) {
    const std::size_t start = i;
    uint32_t value = 0;
    while (i < end && isDigit(s_[i])) {
      if (value > 100000) return fail(start, "number too large");
      value = value * 10 + static_cast<uint32_t>(s_[i] - '0');
      ++i;
    }
    if (i == start) return fail(start, "expected a number");
    out = value;
    return true;
  }

  // ':' then sixteenths, optionally divided: :4 is a quarter, :4/3 an eighth-note triplet.
  bool length(std::size_t& i, std::size_t end, uint32_t& ticks) {
    if (i >= end || s_[i] != ':') return true;
    const std::size_t at = i++;
    uint32_t count = 0, divisor = 1;
    if (!unsignedInt(i, end, count)) return false;
    if (i < end && s_[i] == '/') {
      ++i;
      if (!unsignedInt(i, end, divisor)) return false;
    }
    if (count < 1 || count > kMaxLengthSixteenths) return fail(at, "length must be 1..256");
    if (divisor < 1 || (count * kTicksPerSixteenth) % divisor != 0)
      return fail(at, "length too fine");
    ticks = count * kTicksPerSixteenth / divisor;
    return true;
  }

  // Letter, optional # or b, optional octave digit; the octave carries on to later notes.
  bool pitch(TrackState& t, std::size_t& i, std::size_t end, int& out) {
    const std::size_t at = i;
    int value = semitone(s_[i++]);
    if (i < end && (s_[i] == '#' || s_[i] == 'b')) value += s_[i++] == '#' ? 1 : -1;
    if (i < end && isDigit(s_[i])) {
      t.octave = s_[i++] - '0';
      if (i < end && isDigit(s_[i])) return fail(at, "octave must be 0..9");
    }
    value += 12 * (t.octave + 1) + t.transpose;
    if (value < 0 || value > 127) return fail(at, "note out of range");
    out = value;
    return true;
  }

  bool work(std::size_t at) {
    if (++work_ <= kMaxWork) return true;
    return fail(at, "too many repeats");
  }

  bool advance(TrackState& t, uint32_t ticks, std::size_t at) {
    t.tick += ticks;
    if (t.tick <= kMaxBars * song_.ticksPerBar()) return true;
    return fail(at, "at most %u bars", static_cast<unsigned>(kMaxBars));
  }

  void forget(TrackState& t) { t.lastBegin = t.lastEnd = song_.tracks[t.index].notes.size(); }

  bool play(TrackState& t, std::size_t at, int pitch, uint32_t ticks, uint8_t velocity,
            bool slide) {
    if (t.instrument < 0)
      return fail(at, "track '%s' has no instrument", song_.tracks[t.index].name.c_str());
    if (notes_ >= kMaxNotes) return fail(at, "at most %u notes", static_cast<unsigned>(kMaxNotes));
    ++notes_;
    Note note;
    note.tick = t.tick;
    note.length = ticks;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = velocity;
    note.instrument = static_cast<uint8_t>(t.instrument);
    note.slide = slide;
    SongVector<Note>& notes = song_.tracks[t.index].notes;
    notes.push_back(note);
    t.lastEnd = notes.size();
    return true;
  }

  bool sequence(TrackState& t, std::size_t i, std::size_t end, std::size_t depth) {
    while (i < end) {
      const char c = s_[i];
      if (isSpace(c)) {
        ++i;
        continue;
      }
      const std::size_t at = i;
      if (!work(at)) return false;
      if (c == '|') {
        const uint32_t bar = song_.ticksPerBar();
        const uint32_t into = t.tick % bar;
        if (into) {
          char sixteenths[24];
          if (into % kTicksPerSixteenth == 0)
            std::snprintf(sixteenths, sizeof sixteenths, "%u",
                          static_cast<unsigned>(into / kTicksPerSixteenth));
          else
            std::snprintf(sixteenths, sizeof sixteenths, "%u.%02u",
                          static_cast<unsigned>(into / kTicksPerSixteenth),
                          static_cast<unsigned>((into % kTicksPerSixteenth * 100 + 6) /
                                                kTicksPerSixteenth));
          return fail(at, "this | falls %s sixteenths into bar %u", sixteenths,
                      static_cast<unsigned>(t.tick / bar + 1));
        }
        ++i;
      } else if (c == '(') {
        if (!group(t, i, end, depth)) return false;
      } else if (c == ')') {
        return fail(at, "this ) closes no group");
      } else if (c == '[') {
        if (!chord(t, i, end)) return false;
      } else if (c == '^' || semitone(c) >= 0) {
        const bool slide = c == '^';
        if (slide && (++i >= end || semitone(s_[i]) < 0)) return fail(at, "^ needs a note");
        int value = 0;
        if (!pitch(t, i, end, value) || !length(i, end, t.length) || !separated(i, end, at))
          return false;
        forget(t);
        if (!play(t, at, value, t.length, t.velocity, slide) || !advance(t, t.length, at))
          return false;
      } else if (c == 'r' || c == '_') {
        ++i;
        if (!length(i, end, t.length) || !separated(i, end, at)) return false;
        if (c == '_') {
          if (t.lastBegin == t.lastEnd) return fail(at, "_ without a note");
          SongVector<Note>& notes = song_.tracks[t.index].notes;
          for (std::size_t n = t.lastBegin; n < t.lastEnd; ++n) notes[n].length += t.length;
        } else {
          forget(t);
        }
        if (!advance(t, t.length, at)) return false;
      } else if (c == 'v') {
        uint32_t v = 0;
        ++i;
        if (!unsignedInt(i, end, v) || !separated(i, end, at)) return false;
        if (v < 1 || v > 100) return fail(at, "velocity must be v1..v100");
        t.velocity = static_cast<uint8_t>(v);
      } else if (c == 't') {
        ++i;
        const bool negative = i < end && s_[i] == '-';
        if (i < end && (s_[i] == '-' || s_[i] == '+')) ++i;
        uint32_t v = 0;
        if (!unsignedInt(i, end, v) || !separated(i, end, at)) return false;
        if (v > 48) return fail(at, "transpose must be t-48..t+48");
        t.transpose = negative ? -static_cast<int>(v) : static_cast<int>(v);
      } else if (c == '@') {
        ++i;
        const std::size_t start = i;
        while (i < end && isNameChar(s_[i])) ++i;
        if (!separated(i, end, at)) return false;
        const Span name{start, i};
        const int index = instrumentIndex(name);
        if (index < 0) return fail(at, "no instrument called '%.*s'", name.size(), this->at(name));
        t.instrument = index;
      } else if (c == '%') {
        if (!steps(t, i, end)) return false;
      } else {
        return fail(at, "unexpected '%c'", c);
      }
    }
    return true;
  }

  bool group(TrackState& t, std::size_t& i, std::size_t end, std::size_t depth) {
    const std::size_t open = i;
    if (depth + 1 > kMaxDepth) return fail(open, "groups nest 4 deep at most");
    std::size_t close = open + 1;
    for (int level = 1; close < end; ++close) {
      if (s_[close] == '(') ++level;
      if (s_[close] == ')' && --level == 0) break;
    }
    if (close >= end) return fail(open, "unclosed (");
    std::size_t after = close + 1;
    uint32_t count = 0;
    if (after >= end || !isDigit(s_[after])) return fail(after, "repeat count missing after )");
    if (!unsignedInt(after, end, count) || !separated(after, end, open)) return false;
    if (count < 1 || count > kMaxRepeat) return fail(close + 1, "repeat must be 1..256");
    const Sticky sticky{t.octave, t.length, t.velocity, t.transpose, t.instrument};
    for (uint32_t pass = 0; pass < count; ++pass) {
      t.octave = sticky.octave;
      t.length = sticky.length;
      t.velocity = sticky.velocity;
      t.transpose = sticky.transpose;
      t.instrument = sticky.instrument;
      if (!sequence(t, open + 1, close, depth + 1)) return false;
    }
    i = after;
    return true;
  }

  bool chord(TrackState& t, std::size_t& i, std::size_t end) {
    const std::size_t open = i++;
    int pitches[kMaxChord];
    std::size_t count = 0;
    while (true) {
      while (i < end && isSpace(s_[i])) ++i;
      if (i >= end) return fail(open, "unclosed [");
      if (s_[i] == ']') break;
      if (semitone(s_[i]) < 0) return fail(i, "chord holds notes only");
      if (count == kMaxChord) return fail(i, "at most 8 chord notes");
      const std::size_t at = i;
      if (!pitch(t, i, end, pitches[count])) return false;
      if (i < end && s_[i] == ':') return fail(i, "chord length goes after ]");
      if (i < end && !isSpace(s_[i]) && s_[i] != ']') return separated(i, end, at);
      ++count;
    }
    ++i;
    if (!count) return fail(open, "empty chord");
    if (!length(i, end, t.length) || !separated(i, end, open)) return false;
    forget(t);
    for (std::size_t n = 0; n < count; ++n)
      if (!play(t, open, pitches[n], t.length, t.velocity, false)) return false;
    return advance(t, t.length, open);
  }

  // A step is a 16th, or the length written right after the %, for this pattern only. X, x, o
  // and g strike at 100, 80, 55 and 30 % of the velocity; '.' rests.
  bool steps(TrackState& t, std::size_t& i, std::size_t end) {
    const std::size_t at = i++;
    uint32_t step = kTicksPerSixteenth;
    if (!length(i, end, step)) return false;
    if (delimiter(i, end)) return fail(at, "%% needs steps");
    for (; !delimiter(i, end); ++i) {
      if (!work(i)) return false;
      const char c = s_[i];
      forget(t);
      if (c != '.') {
        const int percent = c == 'X' ? 100 : c == 'x' ? 80 : c == 'o' ? 55 : c == 'g' ? 30 : 0;
        if (!percent) return fail(i, "step must be X, x, o, g or .");
        int value = t.transpose;
        if (t.instrument >= 0) value += song_.instruments[t.instrument].patch.stepPitch;
        if (value < 0 || value > 127) return fail(i, "note out of range");
        const uint8_t velocity =
            static_cast<uint8_t>(std::max(1, (t.velocity * percent + 50) / 100));
        if (!play(t, i, value, step, velocity, false)) return false;
      }
      if (!advance(t, step, i)) return false;
    }
    return true;
  }

  void finish() {
    uint32_t end = 0;
    for (const TrackState& state : states_) end = std::max(end, state.tick);
    if (!notes_) {
      fail(s_.size(), "no notes");
      return;
    }
    const uint32_t bar = song_.ticksPerBar();
    song_.endTick = end;
    song_.lengthTicks = (end + bar - 1) / bar * bar;
    if (song_.loops) {
      song_.loopTick = (loopBar_ - 1) * bar;
      if (song_.loopTick >= song_.lengthTicks) {
        fail(loopAt_, "loop %u is past the last bar (%u)", static_cast<unsigned>(loopBar_),
             static_cast<unsigned>(song_.lengthTicks / bar));
        return;
      }
    }
    const double echoSeconds = song_.echoSixteenths * kTicksPerSixteenth * song_.secondsPerTick();
    if (echoSeconds > kMaxEchoSeconds) fail(echoAt_, "echo over 2 s at this bpm");
  }

  const std::string& s_;
  Song song_;
  std::vector<TrackState> states_;
  std::size_t notes_ = 0;
  uint32_t work_ = 0;
  uint32_t loopBar_ = 1;
  std::size_t loopAt_ = 0;
  std::size_t echoAt_ = 0;
  unsigned seen_ = 0;
  bool failed_ = false;
  std::size_t errorAt_ = 0;
  std::string error_;
};

}

std::size_t Song::bytes() const {
  std::size_t total = sizeof(Song) + instruments.size() * sizeof(Instrument);
  for (const Track& track : tracks) total += sizeof(Track) + track.notes.size() * sizeof(Note);
  return total;
}

std::string ParseResult::describe() const {
  if (ok()) return "";
  char where[48];
  std::snprintf(where, sizeof where, " (line %u, column %u)", static_cast<unsigned>(line),
                static_cast<unsigned>(column));
  return error + where;
}

ParseResult parse(const std::string& text) { return Parser(text).run(); }

}
}

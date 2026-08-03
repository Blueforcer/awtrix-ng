#include "core/sound/Rtttl.h"

namespace awtrix {
namespace rtttl {

namespace {

// Index 0 is the rest; the remaining 48 are 12 semitones per octave for octaves 4 to 7, C4 first.
const uint16_t kFreq[49] = {
    0,
    262,  277,  294,  311,  330,  349,  370,  392,  415,  440,  466,  494,
    523,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988,
    1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,
    2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951};

// The defaults an RTTTL string falls back to when d=, o= or b= is missing. 63 bpm looks odd but
// is what the format specifies.
constexpr uint16_t kDefaultDuration = 4;
constexpr uint16_t kDefaultOctave = 6;
constexpr uint16_t kDefaultBeat = 63;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

void skipSpace(const std::string& s, size_t& i, size_t end) {
  while (i < end && isSpace(s[i])) ++i;
}

bool readUInt(const std::string& s, size_t& i, size_t end, uint32_t& out) {
  if (i >= end || !isDigit(s[i])) return false;
  uint32_t v = 0;
  while (i < end && isDigit(s[i])) {
    if (v > 100000) return false;
    v = v * 10 + static_cast<uint32_t>(s[i] - '0');
    ++i;
  }
  out = v;
  return true;
}

bool validDuration(uint32_t d) {
  return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32;
}
bool validOctave(uint32_t o) { return o >= 4 && o <= 7; }
bool validBeat(uint32_t b) { return b >= 10 && b <= 300; }

Parse fail(const std::string& why, size_t at) {
  Parse p;
  p.ok = false;
  p.error = why;
  p.index = at;
  return p;
}

uint16_t semitoneOf(char c) {
  switch (lower(c)) {
    case 'c': return 1;
    case 'd': return 3;
    case 'e': return 5;
    case 'f': return 6;
    case 'g': return 8;
    case 'a': return 10;
    case 'b': return 12;
    default: return 0;
  }
}

struct Defaults {
  uint16_t duration = kDefaultDuration;
  uint16_t octave = kDefaultOctave;
  uint16_t beat = kDefaultBeat;
};

bool parseDefaults(const std::string& s, size_t begin, size_t end, Defaults& out,
                   Parse& err) {
  bool seenD = false, seenO = false, seenB = false;
  size_t i = begin;
  skipSpace(s, i, end);
  while (i < end) {
    const size_t keyAt = i;
    const char key = lower(s[i]);
    if (key != 'd' && key != 'o' && key != 'b') {
      err = fail("unknown default; expected 'd', 'o' or 'b'", keyAt);
      return false;
    }
    ++i;
    skipSpace(s, i, end);
    if (i >= end || s[i] != '=') {
      err = fail("expected '=' after the default key", i);
      return false;
    }
    ++i;
    skipSpace(s, i, end);
    const size_t valAt = i;
    uint32_t v = 0;
    if (!readUInt(s, i, end, v)) {
      err = fail("expected a number after '='", valAt);
      return false;
    }
    switch (key) {
      case 'd':
        if (seenD) { err = fail("'d' is set twice", keyAt); return false; }
        if (!validDuration(v)) {
          err = fail("'d' must be 1, 2, 4, 8, 16 or 32", valAt);
          return false;
        }
        out.duration = static_cast<uint16_t>(v);
        seenD = true;
        break;
      case 'o':
        if (seenO) { err = fail("'o' is set twice", keyAt); return false; }
        if (!validOctave(v)) { err = fail("'o' must be 4, 5, 6 or 7", valAt); return false; }
        out.octave = static_cast<uint16_t>(v);
        seenO = true;
        break;
      default:
        if (seenB) { err = fail("'b' is set twice", keyAt); return false; }
        if (!validBeat(v)) { err = fail("'b' must be between 10 and 300", valAt); return false; }
        out.beat = static_cast<uint16_t>(v);
        seenB = true;
        break;
    }
    skipSpace(s, i, end);
    if (i >= end) break;
    if (s[i] != ',') {
      err = fail("expected ',' between defaults", i);
      return false;
    }
    ++i;
    skipSpace(s, i, end);
  }
  return true;
}

// One comma-separated note: [duration] letter [#] [.] [octave] [.]. The dot is accepted on either
// side of the octave because both spellings turn up in melodies found in the wild.
bool parseNote(const std::string& s, size_t begin, size_t end, const Defaults& def,
               Note& out, Parse& err) {
  size_t i = begin;
  skipSpace(s, i, end);
  while (end > i && isSpace(s[end - 1])) --end;
  if (i >= end) {
    err = fail("empty note", begin);
    return false;
  }

  uint16_t duration = def.duration;
  if (isDigit(s[i])) {
    const size_t at = i;
    uint32_t v = 0;
    readUInt(s, i, end, v);
    if (!validDuration(v)) {
      err = fail("note length must be 1, 2, 4, 8, 16 or 32", at);
      return false;
    }
    duration = static_cast<uint16_t>(v);
  }

  if (i >= end) {
    err = fail("note length without a note", begin);
    return false;
  }
  const size_t letterAt = i;
  const char letter = lower(s[i]);
  const bool rest = letter == 'p';
  uint16_t semitone = semitoneOf(letter);
  if (!rest && semitone == 0) {
    err = fail(std::string("'") + s[i] + "' is not a note", letterAt);
    return false;
  }
  ++i;

  if (i < end && s[i] == '#') {
    if (rest) {
      err = fail("a rest cannot be sharp", i);
      return false;
    }
    if (letter == 'b' || letter == 'e') {
      err = fail(std::string("'") + letter + "#' is not a note; use the next letter", i);
      return false;
    }
    ++semitone;
    ++i;
  }

  bool dotted = false;
  if (i < end && s[i] == '.') {
    dotted = true;
    ++i;
  }

  uint16_t octave = def.octave;
  if (i < end && isDigit(s[i])) {
    const size_t at = i;
    uint32_t v = 0;
    readUInt(s, i, end, v);
    if (!validOctave(v)) {
      err = fail("octave must be 4, 5, 6 or 7", at);
      return false;
    }
    octave = static_cast<uint16_t>(v);
  }

  if (i < end && s[i] == '.') {
    if (dotted) {
      err = fail("note is dotted twice", i);
      return false;
    }
    dotted = true;
    ++i;
  }

  skipSpace(s, i, end);
  if (i != end) {
    err = fail(std::string("unexpected '") + s[i] + "' in note", i);
    return false;
  }

  // Length is stored in 64th-note units, so a dotted note (1.5x) still comes out as an integer.
  uint16_t units = static_cast<uint16_t>(64 / duration);
  if (dotted) units = static_cast<uint16_t>(units + units / 2);

  out.duration = units;
  out.frequency = rest ? 0 : kFreq[(octave - 4) * 12 + semitone];
  return true;
}

}

uint32_t Parse::durationMs() const {
  uint32_t total = 0;
  for (const Note& n : notes) total += noteMs(n.duration, timeUnit);
  return total;
}

std::string Parse::describe() const {
  if (ok) return "";
  return error + " (at offset " + std::to_string(index) + ")";
}

Parse parse(const std::string& in) {
  if (in.size() > kMaxLength)
    return fail("melody is longer than " + std::to_string(kMaxLength) + " characters",
                kMaxLength);

  const size_t c1 = in.find(':');
  if (c1 == std::string::npos)
    return fail("missing ':' after the melody name", in.size());
  const size_t c2 = in.find(':', c1 + 1);
  if (c2 == std::string::npos)
    return fail("missing ':' before the notes", in.size());

  size_t ts = 0, te = c1;
  skipSpace(in, ts, te);
  while (te > ts && isSpace(in[te - 1])) --te;
  if (ts == te) return fail("the melody name is empty", 0);
  if (te - ts > kMaxTitle)
    return fail("the melody name is longer than " + std::to_string(kMaxTitle) + " characters",
                ts + kMaxTitle);

  Parse p;
  p.title = in.substr(ts, te - ts);

  Defaults def;
  if (!parseDefaults(in, c1 + 1, c2, def, p)) return p;

  // Milliseconds per 32nd note: a whole note is 4 beats, so 60000 * 4 / bpm, divided into 32.
  p.timeUnit = static_cast<uint16_t>(60 * 1000 * 4 / def.beat / 32);

  size_t i = c2 + 1;
  while (i <= in.size()) {
    size_t comma = in.find(',', i);
    const bool last = comma == std::string::npos;
    const size_t end = last ? in.size() : comma;
    Note n;
    if (!parseNote(in, i, end, def, n, p)) return p;
    p.notes.push_back(n);
    if (last) break;
    i = comma + 1;
  }

  if (p.notes.empty()) return fail("the melody has no notes", c2 + 1);

  p.ok = true;
  p.error.clear();
  p.index = 0;
  return p;
}

// Melodies are stored as files named after the title, so the charset is restricted to what is
// safe in a path rather than to what RTTTL itself allows.
bool validName(const std::string& s) {
  if (s.empty() || s.size() > kMaxTitle) return false;
  for (const char c : s) {
    const bool okChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!okChar) return false;
  }
  return true;
}

bool retitle(const std::string& in, const std::string& name, std::string& out) {
  const size_t c1 = in.find(':');
  if (c1 == std::string::npos) return false;
  const size_t c2 = in.find(':', c1 + 1);
  out = name + ':' + (c2 == std::string::npos ? in : in.substr(c1 + 1));
  return true;
}

}
}

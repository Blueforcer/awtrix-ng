#include "core/api/JsonReader.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace awtrix {
namespace api {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

constexpr double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                            1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                            1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
constexpr int kMaxPow10 = 22;

double scalePow10(double v, int exponent) {
  if (exponent > 0) {
    for (; exponent > kMaxPow10; exponent -= kMaxPow10) {
      v *= kPow10[kMaxPow10];
      if (std::isinf(v)) return v;
    }
    return v * kPow10[exponent];
  }
  if (exponent < 0) {
    for (exponent = -exponent; exponent > kMaxPow10; exponent -= kMaxPow10) {
      v /= kPow10[kMaxPow10];
      if (v == 0.0) return v;
    }
    return v / kPow10[exponent];
  }
  return v;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}

void JsonReader::skipSpace() {
  while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
}

JsonReader::Type JsonReader::type() const {
  if (!ok_ || p_ >= end_) return Type::Invalid;
  switch (*p_) {
    case '{': return Type::Object;
    case '[': return Type::Array;
    case '"': return Type::String;
    case 't': case 'f': return Type::Bool;
    case 'n': return Type::Null;
    default:
      return (*p_ == '-' || isDigit(*p_)) ? Type::Number : Type::Invalid;
  }
}

bool JsonReader::isInteger() const {
  if (type() != Type::Number) return false;
  for (const char* q = p_; q < end_; ++q) {
    if (*q == '.' || *q == 'e' || *q == 'E') return false;
    if (*q != '-' && *q != '+' && !isDigit(*q)) break;
  }
  return true;
}

bool JsonReader::asBool(bool& out) const {
  if (type() != Type::Bool) return false;
  out = (*p_ == 't');
  return true;
}

// A number with a fraction or an exponent is read as a double and truncated, not rejected.
bool JsonReader::asLong(long long& out) const {
  if (type() != Type::Number) return false;
  char* stop = nullptr;
  char buf[40];
  const char* e = valueEnd();
  const std::size_t n = static_cast<std::size_t>(e - p_);
  if (n == 0 || n >= sizeof(buf)) return false;
  std::memcpy(buf, p_, n);
  buf[n] = '\0';
  const long long v = std::strtoll(buf, &stop, 10);
  if (stop == buf) return false;
  if (*stop == '.' || *stop == 'e' || *stop == 'E') {
    double d = 0.0;
    if (!asDouble(d)) return false;
    out = static_cast<long long>(d);
    return true;
  }
  if (*stop != '\0') return false;
  out = v;
  return true;
}

bool JsonReader::asDouble(double& out) const {
  if (type() != Type::Number) return false;
  return parseDouble(p_, valueEnd(), out);
}

bool parseDouble(const char* begin, const char* end, double& out) {
  const char* p = begin;
  bool negative = false;
  if (p < end && (*p == '-' || *p == '+')) {
    negative = (*p == '-');
    ++p;
  }

  uint64_t mantissa = 0;
  int exponent = 0;
  bool anyDigit = false;
  // Once the mantissa is full, further integer digits only bump the exponent instead of
  // overflowing it.
  constexpr uint64_t kMantissaLimit = (UINT64_MAX - 9) / 10;
  for (; p < end && isDigit(*p); ++p) {
    anyDigit = true;
    if (mantissa <= kMantissaLimit) mantissa = mantissa * 10 + static_cast<uint64_t>(*p - '0');
    else ++exponent;
  }
  if (p < end && *p == '.') {
    ++p;
    for (; p < end && isDigit(*p); ++p) {
      anyDigit = true;
      if (mantissa > kMantissaLimit) continue;
      mantissa = mantissa * 10 + static_cast<uint64_t>(*p - '0');
      --exponent;
    }
  }
  if (!anyDigit) return false;

  if (p < end && (*p == 'e' || *p == 'E')) {
    ++p;
    bool expNegative = false;
    if (p < end && (*p == '-' || *p == '+')) {
      expNegative = (*p == '-');
      ++p;
    }
    if (p >= end || !isDigit(*p)) return false;
    int written = 0;
    for (; p < end && isDigit(*p); ++p) {
      if (written < 10000) written = written * 10 + (*p - '0');
    }
    exponent += expNegative ? -written : written;
  }
  if (p != end) return false;

  out = scalePow10(static_cast<double>(mantissa), exponent);
  if (negative) out = -out;
  return true;
}

const char* JsonReader::valueEnd() const {
  const char* q = p_;
  while (q < end_ && (isDigit(*q) || *q == '-' || *q == '+' || *q == '.' || *q == 'e' ||
                      *q == 'E')) {
    ++q;
  }
  return q;
}

std::string_view JsonReader::rawString() const {
  if (type() != Type::String) return {};
  const char* q = p_ + 1;
  while (q < end_ && *q != '"') {
    if (*q == '\\') return {};
    ++q;
  }
  if (q >= end_) return {};
  return std::string_view(p_ + 1, static_cast<std::size_t>(q - (p_ + 1)));
}

bool JsonReader::stringView(std::string_view& out) const {
  if (type() != Type::String) return false;
  const char* q = p_ + 1;
  while (q < end_ && *q != '"') {
    if (*q == '\\') return false;
    ++q;
  }
  if (q >= end_) return false;
  out = std::string_view(p_ + 1, static_cast<std::size_t>(q - (p_ + 1)));
  return true;
}

bool JsonReader::appendString(std::string& out) const {
  if (type() != Type::String) return false;
  const char* q = p_ + 1;
  while (q < end_ && *q != '"') {
    if (*q != '\\') {
      out += *q++;
      continue;
    }
    if (++q >= end_) return false;
    switch (*q) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        if (q + 4 >= end_) return false;
        uint32_t cp = 0;
        for (int i = 1; i <= 4; ++i) {
          const int h = hexVal(q[i]);
          if (h < 0) return false;
          cp = (cp << 4) | static_cast<uint32_t>(h);
        }
        q += 4;
        // A high surrogate followed by a valid low one folds into a single code point; a lone
        // surrogate is emitted as-is rather than failing the string.
        if (cp >= 0xD800 && cp <= 0xDBFF && q + 6 < end_ && q[1] == '\\' && q[2] == 'u') {
          uint32_t lo = 0;
          bool okLow = true;
          for (int i = 3; i <= 6; ++i) {
            const int h = hexVal(q[i]);
            if (h < 0) { okLow = false; break; }
            lo = (lo << 4) | static_cast<uint32_t>(h);
          }
          if (okLow && lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            q += 6;
          }
        }
        appendUtf8(out, cp);
        break;
      }
      default: return false;
    }
    ++q;
  }
  return q < end_;
}

bool JsonReader::keyEquals(const char* name) const {
  if (!name) return false;
  return key_ == std::string_view(name);
}

bool JsonReader::skipString() {
  if (p_ >= end_ || *p_ != '"') return false;
  ++p_;
  while (p_ < end_) {
    if (*p_ == '\\') {
      p_ += 2;
      continue;
    }
    if (*p_ == '"') {
      ++p_;
      return true;
    }
    ++p_;
  }
  return false;
}

bool JsonReader::skipNumber() {
  const char* e = valueEnd();
  if (e == p_) return false;
  p_ = e;
  return true;
}

bool JsonReader::literal(const char* word) {
  const std::size_t n = std::strlen(word);
  if (static_cast<std::size_t>(end_ - p_) < n || std::memcmp(p_, word, n) != 0) return false;
  p_ += n;
  return true;
}

bool JsonReader::skipValue() {
  if (!ok_) return false;
  if (depth_ >= kMaxDepth) return ok_ = false;
  skipSpace();
  if (p_ >= end_) return ok_ = false;
  switch (*p_) {
    case '"':
      if (!skipString()) return ok_ = false;
      break;
    case 't':
      if (!literal("true")) return ok_ = false;
      break;
    case 'f':
      if (!literal("false")) return ok_ = false;
      break;
    case 'n':
      if (!literal("null")) return ok_ = false;
      break;
    // Save and restore the member cursor so skipping a nested value leaves the caller's own
    // iteration state alone.
    case '{': {
      const std::string_view savedKey = key_;
      const bool savedFirst = first_;
      if (!enterObject()) return false;
      while (nextMember())
        if (!skipValue()) return false;
      key_ = savedKey;
      first_ = savedFirst;
      break;
    }
    case '[': {
      const bool savedFirst = first_;
      if (!enterArray()) return false;
      while (nextElement())
        if (!skipValue()) return false;
      first_ = savedFirst;
      break;
    }
    default:
      if (!skipNumber()) return ok_ = false;
  }
  skipSpace();
  return ok_;
}

bool JsonReader::enterObject() {
  if (!ok_) return false;
  skipSpace();
  if (p_ >= end_ || *p_ != '{') return ok_ = false;
  ++p_;
  ++depth_;
  first_ = true;
  skipSpace();
  return true;
}

bool JsonReader::nextMember() {
  if (!ok_) return false;
  skipSpace();
  if (p_ < end_ && *p_ == '}') {
    ++p_;
    if (depth_) --depth_;
    skipSpace();
    return false;
  }
  if (!first_) {
    if (p_ >= end_ || *p_ != ',') { ok_ = false; return false; }
    ++p_;
    skipSpace();
  }
  first_ = false;
  if (p_ >= end_ || *p_ != '"') { ok_ = false; return false; }
  const char* nameStart = p_ + 1;
  if (!skipString()) { ok_ = false; return false; }
  key_ = std::string_view(nameStart, static_cast<std::size_t>(p_ - 1 - nameStart));
  skipSpace();
  if (p_ >= end_ || *p_ != ':') { ok_ = false; return false; }
  ++p_;
  skipSpace();
  return true;
}

bool JsonReader::enterArray() {
  if (!ok_) return false;
  skipSpace();
  if (p_ >= end_ || *p_ != '[') return ok_ = false;
  ++p_;
  ++depth_;
  first_ = true;
  skipSpace();
  return true;
}

bool JsonReader::nextElement() {
  if (!ok_) return false;
  skipSpace();
  if (p_ < end_ && *p_ == ']') {
    ++p_;
    if (depth_) --depth_;
    skipSpace();
    return false;
  }
  if (!first_) {
    if (p_ >= end_ || *p_ != ',') { ok_ = false; return false; }
    ++p_;
    skipSpace();
  }
  first_ = false;
  return p_ < end_;
}

std::string_view JsonReader::valueText() const {
  JsonReader r = *this;
  if (!r.skipValue()) return {};
  const char* stop = r.p_;
  while (stop > p_ && (stop[-1] == ' ' || stop[-1] == '\t' || stop[-1] == '\n' ||
                       stop[-1] == '\r')) {
    --stop;
  }
  return std::string_view(p_, static_cast<std::size_t>(stop - p_));
}

// Returns an invalid reader when the key is absent, which present() reports as missing.
JsonReader memberValue(JsonReader obj, const char* name) {
  if (!obj.isObject() || !obj.enterObject()) return {};
  while (obj.nextMember()) {
    if (obj.keyEquals(name)) return obj;
    if (!obj.skipValue()) break;
  }
  return {};
}

bool isWellFormed(std::string_view text) {
  JsonReader probe{text};
  return probe.skipValue() && probe.atEnd();
}

bool readMembers(std::string_view text, std::initializer_list<Member> members) {
  if (!isWellFormed(text)) return false;
  JsonReader r{text};
  if (!r.isObject() || !r.enterObject()) return true;
  while (r.nextMember()) {
    for (const Member& m : members) {
      if (!r.keyEquals(m.key)) continue;
      *m.value = r;
      break;
    }
    if (!r.skipValue()) return false;
  }
  return r.ok();
}

}
}

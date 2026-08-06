#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "core/api/JsonText.h"

namespace awtrix {
namespace api {

class JsonWriter {
 public:
  explicit JsonWriter(std::string& out) : out_(out) {}

  JsonWriter& beginObject() { open('{'); return *this; }
  JsonWriter& endObject() { close('}'); return *this; }
  JsonWriter& beginArray() { open('['); return *this; }
  JsonWriter& endArray() { close(']'); return *this; }

  JsonWriter& key(const char* k) {
    separate();
    appendJsonString(out_, k ? k : "");
    out_ += ':';
    pending_ = true;
    return *this;
  }

  JsonWriter& value(bool v) { prime(); out_ += v ? "true" : "false"; return *this; }
  JsonWriter& value(int v) { return value(static_cast<long long>(v)); }
  JsonWriter& value(unsigned v) { return value(static_cast<unsigned long long>(v)); }
  JsonWriter& value(long v) { return value(static_cast<long long>(v)); }
  JsonWriter& value(unsigned long v) { return value(static_cast<unsigned long long>(v)); }

  JsonWriter& value(long long v) {
    prime();
    appendInt(out_, v);
    return *this;
  }

  JsonWriter& value(unsigned long long v) {
    prime();
    appendUnsigned(out_, v);
    return *this;
  }

  JsonWriter& value(float v) { return printFloat(static_cast<double>(v)); }
  JsonWriter& value(double v) { return printFloat(v); }

  JsonWriter& value(const char* v) {
    prime();
    if (v) appendJsonString(out_, v); else out_ += "null";
    return *this;
  }

  JsonWriter& value(const std::string& v) { prime(); appendJsonString(out_, v); return *this; }

  JsonWriter& value(std::string_view v) {
    prime();
    appendJsonEscaped(out_, v.data(), v.size());
    return *this;
  }

  JsonWriter& raw(std::string_view v) {
    prime();
    out_.append(v.data(), v.size());
    return *this;
  }

  JsonWriter& null() { prime(); out_ += "null"; return *this; }

  JsonWriter& number(double v, int decimals) {
    prime();
    if (std::isnan(v) || std::isinf(v)) { out_ += "null"; return *this; }
    char buf[40];
    int n = snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    if (n < 0) return *this;
    if (n > static_cast<int>(sizeof(buf)) - 1) n = sizeof(buf) - 1;
    if (memchr(buf, '.', static_cast<std::size_t>(n)) != nullptr) {
      while (n > 0 && buf[n - 1] == '0') --n;
      if (n > 0 && buf[n - 1] == '.') --n;
    }
    if (n == 2 && buf[0] == '-' && buf[1] == '0') {
      out_ += '0';
    } else {
      out_.append(buf, static_cast<std::size_t>(n));
    }
    return *this;
  }

  template <typename T>
  JsonWriter& member(const char* k, const T& v) { key(k); return value(v); }
  JsonWriter& member(const char* k, double v, int decimals) {
    key(k);
    return number(v, decimals);
  }
  JsonWriter& memberNull(const char* k) { key(k); return null(); }

 private:
  void open(char c) { separate(); out_ += c; needComma_ = false; pending_ = false; ++depth_; }
  void close(char c) {
    if (depth_) --depth_;
    out_ += c;
    needComma_ = true;
    pending_ = false;
  }
  // A comma goes before every value except the first in a container and the one after a key.
  void separate() {
    if (pending_) return;
    if (needComma_) out_ += ',';
  }
  void prime() { separate(); needComma_ = true; pending_ = false; }

  // Hand-rolled float printing to keep a full dtoa out of the firmware: up to 9 decimals,
  // trailing zeros trimmed, and an exponent only outside roughly 1e-5..1e7.
  JsonWriter& printFloat(double v) {
    prime();
    if (std::isnan(v)) { out_ += "null"; return *this; }
    if (std::isinf(v)) { out_ += "null"; return *this; }
    if (v < 0.0) { out_ += '-'; v = -v; }

    int exponent = normalizeExponent(v);
    uint32_t maxDecimalPart = 1000000000u;
    int decimalPlaces = 9;
    uint32_t integral = static_cast<uint32_t>(v);
    for (uint32_t t = integral; t >= 10; t /= 10) {
      maxDecimalPart /= 10;
      --decimalPlaces;
    }
    double remainder = (v - static_cast<double>(integral)) * static_cast<double>(maxDecimalPart);
    uint32_t decimal = static_cast<uint32_t>(remainder);
    remainder -= static_cast<double>(decimal);
    decimal += static_cast<uint32_t>(remainder * 2);
    if (decimal >= maxDecimalPart) {
      decimal = 0;
      ++integral;
      if (exponent && integral >= 10) { ++exponent; integral = 1; }
    }
    while (decimal % 10 == 0 && decimalPlaces > 0) {
      decimal /= 10;
      --decimalPlaces;
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(integral));
    out_ += buf;
    if (decimalPlaces) {
      out_ += '.';
      snprintf(buf, sizeof(buf), "%0*lu", decimalPlaces, static_cast<unsigned long>(decimal));
      out_ += buf;
    }
    if (exponent) {
      out_ += 'e';
      snprintf(buf, sizeof(buf), "%d", exponent);
      out_ += buf;
    }
    return *this;
  }

  // Scales v into printable range by walking the powers of ten bit by bit, and returns the
  // exponent it took out.
  static int normalizeExponent(double& v) {
    static const double kPos[] = {1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256};
    static const double kNeg[] = {1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32, 1e-64, 1e-128, 1e-256};
    int powersOf10 = 0;
    int index = 8;
    int bit = 1 << index;
    if (v >= 1e7) {
      for (; index >= 0; --index) {
        if (v >= kPos[index]) {
          v *= kNeg[index];
          powersOf10 += bit;
        }
        bit >>= 1;
      }
    }
    if (v > 0 && v <= 1e-5) {
      for (; index >= 0; --index) {
        if (v < kNeg[index] * 10) {
          v *= kPos[index];
          powersOf10 -= bit;
        }
        bit >>= 1;
      }
    }
    return powersOf10;
  }

  std::string& out_;
  int depth_ = 0;
  bool needComma_ = false;
  bool pending_ = false;
};

}
}

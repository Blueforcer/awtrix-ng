#pragma once

#include <cstdio>
#include <string>

namespace awtrix {
namespace api {

inline void appendUnsigned(std::string& out, unsigned long long v) {
  char buf[20];
  char* q = buf + sizeof(buf);
  do {
    *--q = static_cast<char>('0' + (v % 10));
    v /= 10;
  } while (v);
  out.append(q, static_cast<std::size_t>(buf + sizeof(buf) - q));
}

// Negation happens in unsigned space so the most negative long long does not overflow.
inline void appendInt(std::string& out, long long v) {
  const unsigned long long magnitude =
      v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
  if (v < 0) out += '-';
  appendUnsigned(out, magnitude);
}

inline void appendJsonEscaped(std::string& out, const char* p, std::size_t n);

inline void appendJsonString(std::string& out, const char* s) {
  appendJsonEscaped(out, s ? s : "", s ? std::char_traits<char>::length(s) : 0);
}

inline void appendJsonString(std::string& out, const std::string& s) {
  appendJsonEscaped(out, s.data(), s.size());
}

inline void appendJsonEscaped(std::string& out, const char* p, std::size_t n) {
  out += '"';
  for (std::size_t i = 0; i < n; ++i) {
    const char c = p[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char esc[7];
          snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned char>(c));
          out += esc;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

}
}

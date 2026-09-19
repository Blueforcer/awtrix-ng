#pragma once

#include <cstddef>

namespace awtrix {
namespace api {

class JsonStream {
 public:
  using Sink = void (*)(void* ctx, const char* data, std::size_t len);
  enum class Escape { Json, LogLine };

  static constexpr std::size_t kCapacity = 256;

  JsonStream(Sink sink, void* ctx) : sink_(sink), ctx_(ctx) {}
  JsonStream(const JsonStream&) = delete;
  JsonStream& operator=(const JsonStream&) = delete;

  void put(char c) {
    if (len_ == kCapacity) flush();
    buf_[len_++] = c;
  }

  void put(const char* s) {
    for (; *s; ++s) put(*s);
  }

  void putUnsigned(unsigned long v) {
    char digits[3 * sizeof(v)];
    std::size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + (v % 10));
      v /= 10;
    } while (v);
    while (n) put(digits[--n]);
  }

  void putInt(long v) {
    if (v < 0) put('-');
    putUnsigned(v < 0 ? 0ul - static_cast<unsigned long>(v) : static_cast<unsigned long>(v));
  }

  void putString(const char* s, Escape style = Escape::Json) {
    static const char kHex[] = "0123456789abcdef";
    const bool json = style == Escape::Json;
    put('"');
    for (; *s; ++s) {
      const unsigned char c = static_cast<unsigned char>(*s);
      switch (c) {
        case '"': put("\\\""); break;
        case '\\': put("\\\\"); break;
        case '\n': put("\\n"); break;
        case '\t': put("\\t"); break;
        case '\r': if (json) put("\\r"); break;
        default:
          if (json && c == '\b') {
            put("\\b");
          } else if (json && c == '\f') {
            put("\\f");
          } else if (c < 0x20) {
            put("\\u00");
            put(kHex[c >> 4]);
            put(kHex[c & 0x0f]);
          } else {
            put(static_cast<char>(c));
          }
      }
    }
    put('"');
  }

  void flush() {
    if (!len_) return;
    sink_(ctx_, buf_, len_);
    len_ = 0;
  }

 private:
  Sink sink_;
  void* ctx_;
  std::size_t len_ = 0;
  char buf_[kCapacity];
};

}
}

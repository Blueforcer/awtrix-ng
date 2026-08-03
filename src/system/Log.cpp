#include "system/Log.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace awtrix {

namespace {

// The ring is statically sized and never allocates: logging has to keep working when the heap is
// exhausted, which is exactly when the log matters. 34 x 120 bytes is the budget it gets.
constexpr size_t kLineMax = 120;
constexpr size_t kLineSlots = 34;

struct Slot {
  uint32_t seq;
  char text[kLineMax];
};

Slot g_slots[kLineSlots];
size_t g_head = 0;
size_t g_count = 0;
uint32_t g_seq = 0;
bool g_verbose = false;

void writeLine(const char* msg) {
  Slot& slot = g_slots[(g_head + g_count) % kLineSlots];
  if (g_count < kLineSlots)
    ++g_count;
  else
    g_head = (g_head + 1) % kLineSlots;
  char* line = slot.text;
  time_t now = time(nullptr);
  // Before NTP lands the wall clock is meaningless, so stamp early lines with uptime instead.
  if (now > 1600000000) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    snprintf(line, kLineMax, "%02d:%02d:%02d %s", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msg);
  } else {
    snprintf(line, kLineMax, "[%6lus] %s", static_cast<unsigned long>(millis() / 1000), msg);
  }
  slot.seq = ++g_seq;
  Serial.println(line);
}

void appendJsonEscaped(std::string& out, const char* s) {
  for (; *s; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
}

}

void logf(const char* fmt, ...) {
  char msg[kLineMax];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  writeLine(msg);
}

void logdbg(const char* fmt, ...) {
  if (!g_verbose) return;
  char msg[kLineMax];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  writeLine(msg);
}

namespace logbuf {

void setVerbose(bool on) { g_verbose = on; }
bool verbose() { return g_verbose; }

// Returns the lines newer than the caller's last sequence number plus the next one to ask for, so
// the web UI can poll for a tail without re-reading the whole ring.
std::string jsonAfter(uint32_t after) {
  std::string out = "{\"next\":";
  out += std::to_string(g_seq);
  out += ",\"lines\":[";
  bool first = true;
  for (size_t i = 0; i < g_count; ++i) {
    const Slot& e = g_slots[(g_head + i) % kLineSlots];
    if (e.seq <= after) continue;
    if (!first) out += ',';
    first = false;
    out += '"';
    appendJsonEscaped(out, e.text);
    out += '"';
  }
  out += "]}";
  return out;
}

}
}

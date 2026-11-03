#include "system/Log.h"

#include "system/MonotonicClock.h"
#include "platform_log/Output.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#include "core/api/JsonStream.h"

namespace awtrix {

namespace {

// The ring is statically sized and never allocates: logging has to keep working when the heap is
// exhausted, which is exactly when the log matters. Lines are stored at their real length, one
// length byte and the text. That is 34 lines of 80 characters, a little over the average measured
// on a clock, and 22 at the longest. A line is cut at kLineMax including its terminator.
constexpr size_t kLineMax = 120;
constexpr size_t kRingBytes = 34 * (80 + 1);

uint8_t g_ring[kRingBytes];
size_t g_oldest = 0;      // Where the oldest line's length byte is.
size_t g_used = 0;
uint32_t g_oldestSeq = 1;
uint32_t g_seq = 0;       // The newest line's number. Lines between the two are consecutive.
// Any task may log, and one torn length byte would garble the whole ring. On the ESP32 the first
// lock allocates the mutex; the boot log gets there long before the heap can run out.
std::mutex g_ringLock;
bool g_verbose = false;

size_t wrapped(size_t offset) { return offset < kRingBytes ? offset : offset - kRingBytes; }

size_t afterLine(size_t lengthAt) { return wrapped(lengthAt + 1 + g_ring[lengthAt]); }

void store(const char* line, size_t length) {
  std::lock_guard<std::mutex> lock(g_ringLock);
  while (kRingBytes - g_used < length + 1) {
    g_used -= 1 + g_ring[g_oldest];
    g_oldest = afterLine(g_oldest);
    ++g_oldestSeq;
  }
  const size_t lengthAt = wrapped(g_oldest + g_used);
  g_ring[lengthAt] = static_cast<uint8_t>(length);
  const size_t textAt = wrapped(lengthAt + 1);
  const size_t untilEnd = std::min(length, kRingBytes - textAt);
  std::memcpy(g_ring + textAt, line, untilEnd);
  std::memcpy(g_ring, line + untilEnd, length - untilEnd);
  g_used += 1 + length;
  ++g_seq;
}

void load(size_t lengthAt, char* line) {
  const size_t length = g_ring[lengthAt];
  const size_t textAt = wrapped(lengthAt + 1);
  const size_t untilEnd = std::min(length, kRingBytes - textAt);
  std::memcpy(line, g_ring + textAt, untilEnd);
  std::memcpy(line + untilEnd, g_ring, length - untilEnd);
  line[length] = '\0';
}

void writeLine(const char* fmt, va_list ap) {
  char line[kLineMax];
  int stamp;
  time_t now = time(nullptr);
  // Before NTP lands the wall clock is meaningless, so stamp early lines with uptime instead.
  if (now > 1600000000) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    stamp = snprintf(line, kLineMax, "%02d:%02d:%02d ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  } else {
    stamp = snprintf(line, kLineMax, "[%6lus] ", static_cast<unsigned long>(monotonicMs() / 1000));
  }
  vsnprintf(line + stamp, kLineMax - stamp, fmt, ap);
  store(line, std::strlen(line));
  writeLogLine(line);
}

}

void logf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  writeLine(fmt, ap);
  va_end(ap);
}

void logdbg(const char* fmt, ...) {
  if (!g_verbose) return;
  va_list ap;
  va_start(ap, fmt);
  writeLine(fmt, ap);
  va_end(ap);
}

namespace logbuf {

void setVerbose(bool on) { g_verbose = on; }
bool verbose() { return g_verbose; }

// Writes the lines newer than the caller's last sequence number plus the next one to ask for, so
// the web UI can poll for a tail without re-reading the whole ring. The reply can be slower than the
// logging, so the lock is only held to copy a line out, and lines overwritten meanwhile are skipped.
void streamJsonAfter(uint32_t after, api::JsonStream& out) {
  uint32_t seq;
  uint32_t newest;
  size_t at;
  {
    std::lock_guard<std::mutex> lock(g_ringLock);
    newest = g_seq;
    seq = g_oldestSeq;
    at = g_oldest;
    for (; seq <= after && seq <= newest; ++seq) at = afterLine(at);
  }
  out.put("{\"next\":");
  out.putUnsigned(newest);
  out.put(",\"lines\":[");
  bool first = true;
  char line[kLineMax];
  while (seq <= newest) {
    {
      std::lock_guard<std::mutex> lock(g_ringLock);
      if (seq < g_oldestSeq) {
        seq = g_oldestSeq;
        at = g_oldest;
        if (seq > newest) break;
      }
      load(at, line);
      at = afterLine(at);
    }
    ++seq;
    if (!first) out.put(',');
    first = false;
    out.putString(line, api::JsonStream::Escape::LogLine);
  }
  out.put("]}");
  out.flush();
}

}
}

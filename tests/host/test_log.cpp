#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "platform/posix/Time.h"
#include "core/api/JsonStream.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../support.h"
#include "../LogLines.h"

namespace {
int realStderr = -1;

// Failures have to reach the real stderr, which is silenced while the ring tests log.
void check(bool condition, std::string_view message) {
  if (!condition && realStderr >= 0) ::dup2(realStderr, STDERR_FILENO);
  awtrix::test::require(condition, message);
}

using awtrix::logf;
using awtrix::test::logAfter;
using awtrix::test::logMark;

int64_t systemUs() {
  timespec value{};
  check(::clock_gettime(CLOCK_MONOTONIC, &value) == 0, "read system monotonic clock");
  return static_cast<int64_t>(value.tv_sec) * 1000000 + value.tv_nsec / 1000;
}

std::atomic<long> allocations{0};
std::atomic<bool> heapExhausted{false};

// The lines of one reply, without their stamp. These tests log nothing that needs escaping.
std::vector<std::string> linesAfter(uint32_t after) {
  const std::string json = logAfter(after);
  std::vector<std::string> lines;
  std::size_t at = json.find("\"lines\":[");
  check(at != std::string::npos, "reply has lines");
  at += 9;
  while (json[at] == '"') {
    const std::size_t end = json.find('"', at + 1);
    check(end != std::string::npos, "line is closed");
    const std::size_t text = json.find("] ", at);
    check(text != std::string::npos && text < end, "line is stamped");
    lines.push_back(json.substr(text + 2, end - text - 2));
    at = end + 1;
    if (json[at] == ',') ++at;
  }
  check(json.compare(at, 2, "]}") == 0, "reply is complete");
  return lines;
}

void sequenceNumbersSelectTheTail() {
  const uint32_t mark = logMark();
  logf("seq %d", 1);
  logf("seq %d", 2);
  logf("seq %d", 3);
  const std::vector<std::string> all = linesAfter(mark);
  check(all == std::vector<std::string>({"seq 1", "seq 2", "seq 3"}), "lines come oldest first");
  check(logMark() == mark + 3, "next names the newest line");
  check(linesAfter(mark + 2) == std::vector<std::string>({"seq 3"}), "after skips what was seen");
  check(linesAfter(mark + 3).empty(), "nothing is new after the newest");
  check(linesAfter(mark + 1000).empty(), "a number from before a reboot asks for nothing");
  check(logAfter(mark + 1000).rfind("{\"next\":" + std::to_string(mark + 3) + ",", 0) == 0,
        "a number from before a reboot still learns the current one");
}

void theRingHoldsAtLeastTheOldLineCountAtAverageLength() {
  const std::string text(60, 'x');
  const uint32_t mark = logMark();
  for (int i = 0; i < 200; ++i) logf("%03d %s", i, text.c_str());
  const std::vector<std::string> lines = linesAfter(mark);
  check(lines.size() >= 34 && lines.size() < 40, "about 34 lines of the average length");
  for (std::size_t i = 0; i < lines.size(); ++i) {
    char expected[8];
    std::snprintf(expected, sizeof expected, "%03d ", static_cast<int>(200 - lines.size() + i));
    check(lines[i] == expected + text, "the newest lines survive, complete and in order");
  }
}

void aLineIsCutAtTheOldLimit() {
  const std::string text(300, 'y');
  const uint32_t mark = logMark();
  for (int i = 0; i < 100; ++i) logf("%s", text.c_str());
  const std::string json = logAfter(mark);
  const std::vector<std::string> lines = linesAfter(mark);
  check(lines.size() == 22, "22 lines of the longest kind fit");
  for (const std::string& line : lines) check(line == std::string(line.size(), 'y'), "cut lines stay intact");
  const std::size_t start = json.find("\"lines\":[\"") + 10;
  check(json.find('"', start) - start == 119, "119 characters and the terminator, as before");
}

void aCutKeepsWholeCharacters() {
  for (const char* character : {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"}) {
    const std::string unit = character;
    for (std::size_t pad = 0; pad < unit.size(); ++pad) {
      std::string text(pad, 'p');
      while (text.size() < 200) text += unit;
      const uint32_t mark = logMark();
      logf("%s", text.c_str());
      const std::string kept = linesAfter(mark).at(0);
      check(text.compare(0, kept.size(), kept) == 0, "the kept part is unchanged");
      check((kept.size() - pad) % unit.size() == 0, "the cut falls between two characters");
      const std::string json = logAfter(mark);
      const std::size_t start = json.find("\"lines\":[\"") + 10;
      const std::size_t whole = json.find('"', start) - start;
      check(whole <= 119 && whole + unit.size() > 119, "only the character that did not fit goes");
    }
  }
}

void brokenBytesBecomeQuestionMarks() {
  const struct {
    const char* in;
    const char* out;
  } broken[] = {
      {"a\x80z", "a?z"},             // No lead byte.
      {"\xC3(", "?("},               // No continuation.
      {"\xE2\x82z", "??z"},          // Too few continuations.
      {"\xF5\xFF", "??"},            // Never part of UTF-8.
      {"\xC0\xAF", "??"},            // Overlong, three lengths.
      {"\xE0\x80\xAF", "???"},
      {"\xF0\x80\x80\xAF", "????"},
      {"\xED\xA0\x80", "???"},       // Surrogate.
      {"\xF4\x90\x80\x80", "????"},  // Past U+10FFFF.
      {"z\xE2\x82", "z"},            // Ends inside a character, as a cut does.
  };
  for (const auto& line : broken) {
    const uint32_t mark = logMark();
    logf("%s", line.in);
    check(linesAfter(mark) == std::vector<std::string>({line.out}), "broken bytes do not survive");
  }
  // The first and last character on each side of the rules above.
  for (const char* whole : {"\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xEE\x80\x80", "\xEF\xBF\xBF",
                            "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF"}) {
    const uint32_t mark = logMark();
    logf("%s", whole);
    check(linesAfter(mark) == std::vector<std::string>({whole}), "whole characters stay as they are");
  }
}

// As strict as Python's decoder. It judges the decoded code point where the firmware judges byte
// ranges, so the two do not share a mistake.
bool validUtf8(const std::string& text) {
  static const uint32_t kLeast[] = {0, 0x80, 0x800, 0x10000};
  for (std::size_t at = 0; at < text.size();) {
    const auto lead = static_cast<unsigned char>(text[at]);
    const int extra = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : (lead & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0 || at + static_cast<std::size_t>(extra) >= text.size()) return false;
    uint32_t code = extra ? lead & (0x3Fu >> extra) : lead;
    for (int i = 1; i <= extra; ++i) {
      const auto next = static_cast<unsigned char>(text[at + static_cast<std::size_t>(i)]);
      if ((next & 0xC0) != 0x80) return false;
      code = code << 6 | (next & 0x3F);
    }
    if (code < kLeast[extra] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    at += static_cast<std::size_t>(extra) + 1;
  }
  return true;
}

void anyBytesComeOutValid() {
  // Mostly the bytes where the rules change, so whole characters turn up between the broken ones.
  static const unsigned char kBytes[] = {'z',  0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF,
                                         0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF};
  std::mt19937 random(1);
  for (int i = 0; i < 5000; ++i) {
    std::string text(random() % 160, 'z');
    for (char& byte : text) byte = static_cast<char>(kBytes[random() % sizeof kBytes]);
    const uint32_t mark = logMark();
    logf("%s", text.c_str());
    check(validUtf8(logAfter(mark)), "any input comes out as valid UTF-8");
  }
}

// Every length, so a line starts, and its length byte sits, on every offset around the wrap.
void everyLengthSurvivesTheWrap() {
  uint32_t mark = logMark();
  for (int i = 0; i < 3000; ++i) {
    const std::string text = std::to_string(i) + " " + std::string(static_cast<std::size_t>(i % 101), 'z');
    logf("%s", text.c_str());
    if (i % 7) continue;
    const std::vector<std::string> lines = linesAfter(mark + static_cast<uint32_t>(i));
    check(lines.size() == 1 && lines[0] == text, "the newest line reads back exactly");
  }
  mark = logMark();
  logf("%s", "");
  check(linesAfter(mark) == std::vector<std::string>({""}), "an empty message is still a line");
}

void loggingNeedsNoHeap() {
  long seen = allocations;
  logf("%s", std::string(300, 'h').c_str());
  allocations = seen;
  heapExhausted = true;
  logf("heap gone %d", 1);
  awtrix::api::JsonStream out([](void*, const char*, std::size_t) {}, nullptr);
  awtrix::logbuf::streamJsonAfter(0, out);
  heapExhausted = false;
  check(allocations == seen, "neither a line nor a reply allocates");
}

void concurrentWritersNeverGarbleTheRing() {
  constexpr int kWriters = 4;
  constexpr int kLinesEach = 4000;
  std::atomic<bool> writing{true};
  std::vector<std::thread> writers;
  for (int writer = 0; writer < kWriters; ++writer) {
    writers.emplace_back([writer] {
      for (int line = 0; line < kLinesEach; ++line)
        logf("w%d %d %s", writer, line, std::string(static_cast<std::size_t>(line % 90), 'c').c_str());
    });
  }
  const uint32_t firstMark = logMark();
  std::thread reader([&writing, firstMark] {
    int next[kWriters] = {};
    uint32_t after = firstMark;
    do {
      const std::string json = logAfter(after);
      char* end = nullptr;
      const uint32_t announced = static_cast<uint32_t>(std::strtoul(json.c_str() + 8, &end, 10));
      check(announced >= after, "next never runs backwards");
      after = announced;
      std::size_t at = json.find("\"lines\":[") + 9;
      while (json[at] == '"') {
        const std::size_t close = json.find('"', at + 1);
        check(close != std::string::npos, "line is closed");
        const std::string whole = json.substr(at + 1, close - at - 1);
        const std::size_t text = whole.find("] w");
        check(text != std::string::npos, "line kept its stamp and its start");
        int writer = -1, number = -1, consumed = 0;
        check(std::sscanf(whole.c_str() + text + 2, "w%d %d %n", &writer, &number, &consumed) == 2 &&
              writer >= 0 && writer < kWriters, "line kept its header");
        const std::string tail = whole.substr(text + 2 + static_cast<std::size_t>(consumed));
        check(tail == std::string(static_cast<std::size_t>(number % 90), 'c'), "line kept its text");
        check(number >= next[writer], "one writer's lines stay in order");
        next[writer] = number + 1;
        at = close + 1;
        if (json[at] == ',') ++at;
      }
    } while (writing);
  });
  for (std::thread& writer : writers) writer.join();
  writing = false;
  reader.join();
}

}

extern "C" time_t __wrap_time(time_t* out) {
  if (out) *out = 0;
  return 0;
}

void* operator new(std::size_t bytes) {
  ++allocations;
  if (heapExhausted) throw std::bad_alloc();
  void* pointer = std::malloc(bytes ? bytes : 1);
  if (!pointer) throw std::bad_alloc();
  return pointer;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
  const int64_t before = systemUs();
  const int64_t application = awtrix::monotonicUs();
  const int64_t fileClock = awtrix::posix::monotonicMs();
  const int64_t after = systemUs();
  check(before <= application && application <= after, "application clock uses the system monotonic epoch");
  check(before / 1000 <= fileClock && fileClock <= after / 1000, "POSIX and application timestamps are comparable");

  FILE* capture = std::tmpfile();
  check(capture != nullptr, "create stderr capture");
  const int saved = ::dup(STDERR_FILENO);
  realStderr = saved;
  check(saved >= 0 && ::dup2(::fileno(capture), STDERR_FILENO) >= 0, "capture stderr");
  const int64_t firstSecond = systemUs() / 1000000;
  awtrix::logf("clock before wall time synchronization");
  const int64_t lastSecond = systemUs() / 1000000;
  std::fflush(stderr);
  check(::dup2(saved, STDERR_FILENO) >= 0, "restore stderr");
  std::rewind(capture);
  char line[256]{};
  check(std::fgets(line, sizeof line, capture) != nullptr, "platform output reaches stderr");
  std::fclose(capture);
  unsigned long seconds = 0;
  check(std::sscanf(line, "[%lus]", &seconds) == 1 && seconds >= static_cast<unsigned long>(firstSecond) &&
        seconds <= static_cast<unsigned long>(lastSecond), "early log lines use the shared uptime clock");

  std::string json;
  awtrix::api::JsonStream out([](void* context, const char* text, std::size_t size) {
    static_cast<std::string*>(context)->append(text, size);
  }, &json);
  awtrix::logbuf::streamJsonAfter(0, out);
  check(json.find("clock before wall time synchronization") != std::string::npos,
        "the API ring keeps the emitted log line");

  // The rest logs thousands of lines; none of them belongs in the test's output.
  FILE* nowhere = std::fopen("/dev/null", "w");
  check(nowhere != nullptr && ::dup2(::fileno(nowhere), STDERR_FILENO) >= 0, "silence stderr");
  sequenceNumbersSelectTheTail();
  theRingHoldsAtLeastTheOldLineCountAtAverageLength();
  aLineIsCutAtTheOldLimit();
  aCutKeepsWholeCharacters();
  brokenBytesBecomeQuestionMarks();
  anyBytesComeOutValid();
  everyLengthSurvivesTheWrap();
  loggingNeedsNoHeap();
  concurrentWritersNeverGarbleTheRing();
  check(::dup2(saved, STDERR_FILENO) >= 0, "restore stderr");
  ::close(saved);
  std::fclose(nowhere);
  std::puts("host log: shared clock epoch, uptime stamp, stderr output, byte ring, valid UTF-8 and concurrency passed");
}

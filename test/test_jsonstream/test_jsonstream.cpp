#include <unity.h>

#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "core/api/JsonStream.h"
#include "core/api/JsonText.h"

namespace {
bool s_heapExhausted = false;
int s_allocAttempts = 0;
}

void* operator new(std::size_t n) {
  if (s_heapExhausted) {
    ++s_allocAttempts;
    throw std::bad_alloc();
  }
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (s_heapExhausted) {
    ++s_allocAttempts;
    return nullptr;
  }
  return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  return operator new(n, std::nothrow);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

using namespace awtrix;
using api::JsonStream;

namespace {

struct Capture {
  std::string data;
  int calls = 0;
  std::size_t largest = 0;
  bool sawEmpty = false;
};

void captureSink(void* ctx, const char* data, std::size_t len) {
  auto* c = static_cast<Capture*>(ctx);
  ++c->calls;
  if (len == 0) c->sawEmpty = true;
  if (len > c->largest) c->largest = len;
  c->data.append(data, len);
}

struct FixedCapture {
  char data[32768];
  std::size_t len = 0;
  bool overflow = false;
};

void fixedSink(void* ctx, const char* data, std::size_t len) {
  auto* c = static_cast<FixedCapture*>(ctx);
  if (c->len + len > sizeof(c->data)) {
    c->overflow = true;
    return;
  }
  std::memcpy(c->data + c->len, data, len);
  c->len += len;
}

void referenceLogEscape(std::string& out, const char* s) {
  out += '"';
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
  out += '"';
}

std::string everyByte() {
  std::string s;
  for (int c = 1; c < 256; ++c) s += static_cast<char>(c);
  return s;
}

std::string streamed(const char* s, JsonStream::Escape style) {
  Capture cap;
  JsonStream js(captureSink, &cap);
  js.putString(s, style);
  js.flush();
  return cap.data;
}

}

void setUp() {
  s_heapExhausted = false;
  s_allocAttempts = 0;
}
void tearDown() { s_heapExhausted = false; }

static void test_log_line_escaping_is_spelled_out() {
  TEST_ASSERT_EQUAL_STRING(
      "\"12:00:01 say \\\"hi\\\" C:\\\\x\\ttab\\nline end \\u0008\\u000c\\u001b\\u0001 \xc3\xa4\"",
      streamed("12:00:01 say \"hi\" C:\\x\ttab\nline\r end \b\f\x1b\x01 \xc3\xa4",
               JsonStream::Escape::LogLine)
          .c_str());
  TEST_ASSERT_EQUAL_STRING("\"\"", streamed("", JsonStream::Escape::LogLine).c_str());
}

static void test_log_line_escaping_matches_the_reference_for_every_byte() {
  const std::string all = everyByte();
  std::string expect;
  referenceLogEscape(expect, all.c_str());
  const std::string got = streamed(all.c_str(), JsonStream::Escape::LogLine);
  TEST_ASSERT_EQUAL_size_t(expect.size(), got.size());
  TEST_ASSERT_EQUAL_MEMORY(expect.data(), got.data(), expect.size());
}

static void test_json_escaping_matches_the_json_writer_for_every_byte() {
  const std::string all = everyByte();
  std::string expect;
  api::appendJsonString(expect, all);
  const std::string got = streamed(all.c_str(), JsonStream::Escape::Json);
  TEST_ASSERT_EQUAL_size_t(expect.size(), got.size());
  TEST_ASSERT_EQUAL_MEMORY(expect.data(), got.data(), expect.size());
  TEST_ASSERT_EQUAL_STRING("\"a\\rb\\bc\\fd\"",
                           streamed("a\rb\bc\fd", JsonStream::Escape::Json).c_str());
}

static void test_numbers_are_written_in_decimal() {
  Capture cap;
  JsonStream js(captureSink, &cap);
  js.putUnsigned(0);
  js.put(' ');
  js.putUnsigned(4294967295ul);
  js.put(' ');
  js.putInt(-52);
  js.put(' ');
  js.putInt(7);
  js.put(' ');
  js.putInt(LONG_MIN);
  js.flush();
  const std::string expect = "0 4294967295 -52 7 " + std::to_string(LONG_MIN);
  TEST_ASSERT_EQUAL_STRING(expect.c_str(), cap.data.c_str());
}

static void test_nothing_reaches_the_sink_before_the_buffer_fills() {
  Capture cap;
  JsonStream js(captureSink, &cap);
  js.put("{\"next\":");
  js.putUnsigned(12);
  js.put('}');
  TEST_ASSERT_EQUAL_INT(0, cap.calls);
  js.flush();
  TEST_ASSERT_EQUAL_INT(1, cap.calls);
  TEST_ASSERT_EQUAL_STRING("{\"next\":12}", cap.data.c_str());
}

static void test_the_sink_never_sees_an_empty_write() {
  Capture cap;
  JsonStream js(captureSink, &cap);
  js.flush();
  js.put("");
  js.flush();
  TEST_ASSERT_EQUAL_INT(0, cap.calls);

  for (std::size_t i = 0; i < JsonStream::kCapacity; ++i) js.put('x');
  js.flush();
  js.flush();
  TEST_ASSERT_EQUAL_INT(1, cap.calls);
  TEST_ASSERT_FALSE(cap.sawEmpty);
}

static void test_output_survives_every_split_position() {
  const char* tail = "\x01\"\\\n\tz";
  std::string expectTail;
  referenceLogEscape(expectTail, tail);

  for (std::size_t pad = 0; pad <= JsonStream::kCapacity + 8; ++pad) {
    Capture cap;
    JsonStream js(captureSink, &cap);
    for (std::size_t i = 0; i < pad; ++i) js.put('x');
    js.putString(tail, JsonStream::Escape::LogLine);
    js.put("]}");
    js.flush();

    const std::string expect = std::string(pad, 'x') + expectTail + "]}";
    TEST_ASSERT_EQUAL_size_t(expect.size(), cap.data.size());
    TEST_ASSERT_EQUAL_MEMORY(expect.data(), cap.data.data(), expect.size());
    TEST_ASSERT_FALSE(cap.sawEmpty);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(JsonStream::kCapacity, cap.largest);
  }
}

static void test_a_full_log_reply_is_written_with_the_heap_exhausted() {
  char line[120];
  std::memset(line, 0x01, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  static FixedCapture cap;
  cap.len = 0;
  cap.overflow = false;

  bool threw = false;
  s_heapExhausted = true;
  try {
    JsonStream js(fixedSink, &cap);
    js.put("{\"next\":");
    js.putUnsigned(4294967295ul);
    js.put(",\"lines\":[");
    for (int i = 0; i < 34; ++i) {
      if (i) js.put(',');
      js.putString(line, JsonStream::Escape::LogLine);
    }
    js.put("]}");
    js.flush();
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  s_heapExhausted = false;

  TEST_ASSERT_FALSE(threw);
  TEST_ASSERT_EQUAL_INT(0, s_allocAttempts);
  TEST_ASSERT_FALSE(cap.overflow);

  std::string expect = "{\"next\":4294967295,\"lines\":[";
  for (int i = 0; i < 34; ++i) {
    if (i) expect += ',';
    referenceLogEscape(expect, line);
  }
  expect += "]}";
  TEST_ASSERT_EQUAL_size_t(expect.size(), cap.len);
  TEST_ASSERT_EQUAL_MEMORY(expect.data(), cap.data, expect.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_log_line_escaping_is_spelled_out);
  RUN_TEST(test_log_line_escaping_matches_the_reference_for_every_byte);
  RUN_TEST(test_json_escaping_matches_the_json_writer_for_every_byte);
  RUN_TEST(test_numbers_are_written_in_decimal);
  RUN_TEST(test_nothing_reaches_the_sink_before_the_buffer_fills);
  RUN_TEST(test_the_sink_never_sees_an_empty_write);
  RUN_TEST(test_output_survives_every_split_position);
  RUN_TEST(test_a_full_log_reply_is_written_with_the_heap_exhausted);
  return UNITY_END();
}

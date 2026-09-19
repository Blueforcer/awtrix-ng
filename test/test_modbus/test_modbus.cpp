#include <unity.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "core/script/ModbusTcp.h"
#include "core/script/ScriptHeapTesting.h"

using namespace awtrix::script;
void setUp() {}
void tearDown() { heap::testing::resetGrowthBudget(); }

static void test_endpoints_and_limits() {
  modbus::Read r;
  TEST_ASSERT_TRUE(modbus::parse("modbus://meter.local/1/3/0/125", r));
  TEST_ASSERT_EQUAL_STRING("meter.local", r.host.c_str());
  TEST_ASSERT_EQUAL(502, r.port);
  TEST_ASSERT_TRUE(modbus::parse("modbus://192.168.1.2:1502/255/4/65535/1", r));
  TEST_ASSERT_EQUAL(1502, r.port);
  TEST_ASSERT_EQUAL(255, r.unit);
  TEST_ASSERT_TRUE(modbus::parse("modbus://x/0/1/0/2000", r));
  for (const char* url : {"http://x/1/3/0/1", "modbus:///1/3/0/1", "modbus://x:0/1/3/0/1",
       "modbus://x:65536/1/3/0/1", "modbus://x/256/3/0/1", "modbus://x/1/0/0/1",
       "modbus://x/1/6/0/1", "modbus://x/1/3/-1/1", "modbus://x/1/3/65535/2",
       "modbus://x/1/3/0/126", "modbus://x/1/3/0/0", "modbus://x/1/2/0/2001",
       "modbus://x/1/3/0/1/", "modbus://x/1/3/0/1?x=2", "modbus://x\r/1/3/0/1",
       "modbus://x/1/3/0/99999999999999999999", "modbus://x/1/3//1"}) {
    TEST_ASSERT_FALSE_MESSAGE(modbus::parse(url, r), url);
  }
}

static void test_request_wire_format() {
  modbus::Read r;
  modbus::parse("modbus://x/17/4/300/2", r);
  uint8_t bytes[12];
  modbus::encode(r, 0x1234, bytes);
  const uint8_t expected[] = {0x12, 0x34, 0, 0, 0, 6, 17, 4, 1, 44, 0, 2};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, bytes, 12);
}

static void test_registers_and_exceptions() {
  modbus::Read r;
  r.count = 2;
  const uint8_t f[] = {0, 42, 0, 0, 0, 7, 1, 3, 4, 0x12, 0x34, 0xff, 0xff};
  auto result = modbus::decode(r, 42, f, sizeof(f));
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_EQUAL_STRING("[4660,65535]", result.body.c_str());
  TEST_ASSERT_EQUAL(200, result.status);
  const uint8_t exception[] = {0, 42, 0, 0, 0, 3, 1, 0x83, 2};
  result = modbus::decode(r, 42, exception, sizeof(exception));
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_EQUAL(2, result.status);
}

static void test_bad_frames() {
  modbus::Read r;
  const std::vector<uint8_t> good = {0, 42, 0, 0, 0, 5, 1, 3, 2, 0, 1};
  for (unsigned i : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
    auto bad = good;
    bad[i] ^= 1;
    TEST_ASSERT_FALSE(modbus::decode(r, 42, bad.data(), bad.size()).ok);
  }
  for (std::size_t n = 0; n < good.size(); ++n)
    TEST_ASSERT_FALSE(modbus::decode(r, 42, good.data(), n).ok);
}

static void test_low_memory_does_not_collect_a_response() {
  modbus::Read r;
  const uint8_t frame[] = {0, 1, 0, 0, 0, 5, 1, 3, 2, 0, 42};
  heap::testing::setGrowthBudget(0);
  const auto result = modbus::decode(r, 1, frame, sizeof(frame));
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_TRUE(result.body.empty());
}

static void test_bits_and_maximum_register_response() {
  modbus::Read r;
  r.function = 2;
  r.count = 9;
  const uint8_t f[] = {0, 1, 0, 0, 0, 5, 1, 2, 2, 0x85, 0xff};
  TEST_ASSERT_EQUAL_STRING("[1,0,1,0,0,0,0,1,1]", modbus::decode(r, 1, f, sizeof(f)).body.c_str());
  r.function = 3;
  r.count = 125;
  std::vector<uint8_t> full(259, 0xff);
  const uint8_t header[] = {0, 1, 0, 0, 0, 253, 1, 3, 250};
  std::copy(header, header + 9, full.begin());
  TEST_ASSERT_TRUE(modbus::decode(r, 1, full.data(), full.size()).ok);
}

struct Client {
  std::vector<uint8_t> data;
  std::size_t offset = 0;
  bool open = true;
  bool writable = true;
  int available() { return offset < data.size() ? 1 : 0; }
  int read(uint8_t* out, std::size_t n) {
    const auto count = std::min(n, data.size() - offset);
    std::memcpy(out, data.data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  }
  std::size_t write(const uint8_t*, std::size_t n) { return writable ? n : 0; }
  bool connected() { return open; }
};

static void test_fragmentation_timeout_disconnect_and_oversize() {
  modbus::Read r;
  int64_t now = 0;
  auto clock = [&] { return now; };
  auto pause = [&] { now += 10; };
  Client c{{0, 1, 0, 0, 0, 5, 1, 3, 2, 0, 42}};
  TEST_ASSERT_EQUAL_STRING("[42]", modbus::exchange(c, r, 1, clock, pause).body.c_str());
  c = Client{{0, 1, 0, 0, 0, 5, 1, 3, 2, 0}};
  TEST_ASSERT_FALSE(modbus::exchange(c, r, 1, clock, pause).ok);
  TEST_ASSERT_EQUAL(2000, now);
  c = Client{};
  c.open = false;
  TEST_ASSERT_FALSE(modbus::exchange(c, r, 1, clock, pause).ok);
  c = Client{{0, 1, 0, 0, 255, 255, 1}};
  TEST_ASSERT_FALSE(modbus::exchange(c, r, 1, clock, pause).ok);
  c.writable = false;
  TEST_ASSERT_FALSE(modbus::exchange(c, r, 1, clock, pause).ok);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_endpoints_and_limits);
  RUN_TEST(test_request_wire_format);
  RUN_TEST(test_registers_and_exceptions);
  RUN_TEST(test_bad_frames);
  RUN_TEST(test_low_memory_does_not_collect_a_response);
  RUN_TEST(test_bits_and_maximum_register_response);
  RUN_TEST(test_fragmentation_timeout_disconnect_and_oversize);
  return UNITY_END();
}

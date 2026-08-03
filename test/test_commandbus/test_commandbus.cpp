#include <unity.h>

#include "core/CommandBus.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static int ti(CommandType t) { return static_cast<int>(t); }

static void test_empty_pop_fails() {
  CommandBus b(4);
  Command c;
  TEST_ASSERT_TRUE(b.empty());
  TEST_ASSERT_FALSE(b.pop(c));
}

static void test_push_pop_fifo() {
  CommandBus b(4);
  TEST_ASSERT_TRUE(b.push(Command(CommandType::NextApp)));
  TEST_ASSERT_TRUE(b.push(Command(CommandType::PreviousApp)));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)b.size());
  Command out;
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_EQUAL_INT(ti(CommandType::NextApp), ti(out.type));
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_EQUAL_INT(ti(CommandType::PreviousApp), ti(out.type));
  TEST_ASSERT_TRUE(b.empty());
}

static void test_full_push_is_dropped() {
  CommandBus b(2);
  TEST_ASSERT_TRUE(b.push(Command(CommandType::NextApp)));
  TEST_ASSERT_TRUE(b.push(Command(CommandType::NextApp)));
  TEST_ASSERT_TRUE(b.full());
  TEST_ASSERT_FALSE(b.push(Command(CommandType::NextApp)));
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)b.size());
}

static void test_ring_wraparound() {
  CommandBus b(2);
  Command out;
  TEST_ASSERT_TRUE(b.push(Command(CommandType::NextApp)));
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_TRUE(b.push(Command(CommandType::PreviousApp)));
  TEST_ASSERT_TRUE(b.push(Command(CommandType::Reboot)));
  TEST_ASSERT_TRUE(b.full());
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_EQUAL_INT(ti(CommandType::PreviousApp), ti(out.type));
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_EQUAL_INT(ti(CommandType::Reboot), ti(out.type));
}

static void test_payload_preserved() {
  CommandBus b(2);
  Command c(CommandType::Notify);
  c.payload = "{\"text\":\"hi\"}";
  c.source = Source::Http;
  TEST_ASSERT_TRUE(b.push(c));
  Command out;
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_EQUAL_STRING("{\"text\":\"hi\"}", out.payload.c_str());
  TEST_ASSERT_EQUAL_INT((int)Source::Http, (int)out.source);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_pop_fails);
  RUN_TEST(test_push_pop_fifo);
  RUN_TEST(test_full_push_is_dropped);
  RUN_TEST(test_ring_wraparound);
  RUN_TEST(test_payload_preserved);
  return UNITY_END();
}

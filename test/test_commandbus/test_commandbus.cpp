#include <unity.h>

#include <cstdlib>
#include <new>
#include <utility>

#include "core/CommandBus.h"

using namespace awtrix;

// Live heap blocks: what the queue still holds shows up here without inspecting its slots.
namespace {
long liveBlocks = 0;
}

void* operator new(std::size_t bytes) {
  void* pointer = std::malloc(bytes ? bytes : 1);
  if (!pointer) throw std::bad_alloc();
  ++liveBlocks;
  return pointer;
}
void* operator new[](std::size_t bytes) { return operator new(bytes); }
void operator delete(void* pointer) noexcept {
  if (pointer) --liveBlocks;
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept { operator delete(pointer); }
void operator delete[](void* pointer) noexcept { operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { operator delete(pointer); }

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

static void test_consumed_payloads_are_released_while_the_queue_is_alive() {
  CommandBus b(2);
  Command source(CommandType::Notify);
  source.name.assign(128, 'n');
  source.payload.assign(8192, 'p');
  const long before = liveBlocks;
  bool succeeded = true;
  {
    Command out;
    for (int i = 0; i < 8; ++i) {
      succeeded = succeeded && b.push(source) && b.pop(out);
      // Alternate long and short commands while reusing the output and wrapping the ring.
      source.payload.assign(i % 2 ? 8192 : 3, 'p');
    }
  }
  const long retained = liveBlocks - before;
  TEST_ASSERT_TRUE(succeeded);
  TEST_ASSERT_EQUAL_INT(0, retained);
}

static void test_moved_commands_transfer_their_payload() {
  CommandBus b(1);
  Command source(CommandType::PlayAudio);
  source.name.assign(128, 'n');
  source.payload.assign(8192, 'p');
  source.arg = 42;
  const char* name = source.name.data();
  const char* payload = source.payload.data();
  Command out;
  TEST_ASSERT_TRUE(b.push(std::move(source)));
  TEST_ASSERT_TRUE(b.pop(out));
  TEST_ASSERT_EQUAL_PTR(name, out.name.data());
  TEST_ASSERT_EQUAL_PTR(payload, out.payload.data());
  TEST_ASSERT_EQUAL_INT(ti(CommandType::PlayAudio), ti(out.type));
  TEST_ASSERT_EQUAL_INT(42, out.arg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_pop_fails);
  RUN_TEST(test_push_pop_fifo);
  RUN_TEST(test_full_push_is_dropped);
  RUN_TEST(test_ring_wraparound);
  RUN_TEST(test_payload_preserved);
  RUN_TEST(test_consumed_payloads_are_released_while_the_queue_is_alive);
  RUN_TEST(test_moved_commands_transfer_their_payload);
  return UNITY_END();
}

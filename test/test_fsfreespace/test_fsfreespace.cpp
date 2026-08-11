
#include <unity.h>

#include "persistence/FsFreeSpace.h"

using namespace awtrix;

static int g_reads;
static std::size_t g_free;

static std::size_t readFree() {
  ++g_reads;
  return g_free;
}

void setUp() {
  g_reads = 0;
  g_free = 1024 * 1024;
}

void tearDown() {}

// A script is allowed to update its store from draw(), so this question can be asked at frame
// rate. Answering it means adding up every block in use, which does not fit in a frame.
static void test_a_burst_of_questions_costs_one_reading() {
  FsFreeSpace f;
  for (int i = 0; i < 200; ++i)
    TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, static_cast<uint32_t>(f.bytes(readFree)));
  TEST_ASSERT_EQUAL_INT(1, g_reads);
}

// Once something has actually been written the remembered figure is worth nothing, and the next
// question pays for a fresh one.
static void test_a_write_makes_the_next_question_pay_again() {
  FsFreeSpace f;
  TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, static_cast<uint32_t>(f.bytes(readFree)));

  g_free = 4096;
  TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, static_cast<uint32_t>(f.bytes(readFree)));
  TEST_ASSERT_EQUAL_INT(1, g_reads);

  f.stale();
  TEST_ASSERT_EQUAL_UINT32(4096u, static_cast<uint32_t>(f.bytes(readFree)));
  TEST_ASSERT_EQUAL_INT(2, g_reads);
}

// Marking it stale reads nothing by itself: a device where no script writes never pays for a
// reading at all.
static void test_going_stale_reads_nothing_on_its_own() {
  FsFreeSpace f;
  for (int i = 0; i < 50; ++i) f.stale();
  TEST_ASSERT_EQUAL_INT(0, g_reads);
}

// Settings and icons share the filesystem with the scripts, so a store is only taken while it
// still leaves them room.
static void test_the_margin_is_kept_clear() {
  TEST_ASSERT_TRUE(fitsWithMargin(kFsMarginBytes + 100, 100));
  TEST_ASSERT_FALSE(fitsWithMargin(kFsMarginBytes + 100, 101));
  TEST_ASSERT_FALSE(fitsWithMargin(kFsMarginBytes, 1));
  TEST_ASSERT_FALSE(fitsWithMargin(0, 1));
}

// A full filesystem refuses even an empty write rather than wrapping round to a huge figure.
static void test_a_full_filesystem_refuses_everything() {
  TEST_ASSERT_FALSE(fitsWithMargin(0, 0));
  TEST_ASSERT_FALSE(fitsWithMargin(kFsMarginBytes - 1, 0));
  TEST_ASSERT_TRUE(fitsWithMargin(kFsMarginBytes + 1, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_burst_of_questions_costs_one_reading);
  RUN_TEST(test_a_write_makes_the_next_question_pay_again);
  RUN_TEST(test_going_stale_reads_nothing_on_its_own);
  RUN_TEST(test_the_margin_is_kept_clear);
  RUN_TEST(test_a_full_filesystem_refuses_everything);
  return UNITY_END();
}

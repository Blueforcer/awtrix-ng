#include <unity.h>

#include "core/remote/OfferList.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {
wire::Offer offer(const char* name, uint16_t port = 8300) {
  wire::Offer o;
  o.name = name;
  o.port = port;
  return o;
}
}

void setUp() {}
void tearDown() {}

static void test_an_offer_is_listed() {
  OfferList list;
  list.offer(offer("raspberrypi"), 0xC0A80105u, 1000);
  const auto now = list.current(1000);
  TEST_ASSERT_EQUAL(1, now.size());
  TEST_ASSERT_EQUAL_STRING("raspberrypi", now[0].name.c_str());
  TEST_ASSERT_EQUAL_HEX32(0xC0A80105u, now[0].address);
  TEST_ASSERT_EQUAL(8300, now[0].port);
}

static void test_the_same_host_is_only_refreshed() {
  OfferList list;
  list.offer(offer("raspberrypi"), 0xC0A80105u, 0);
  list.offer(offer("raspberrypi", 8400), 0xC0A80105u, 80000);
  const auto now = list.current(100000);
  TEST_ASSERT_EQUAL(1, now.size());
  TEST_ASSERT_EQUAL_MESSAGE(8400, now[0].port, "the newest port wins");
}

static void test_a_fifth_host_replaces_the_oldest() {
  OfferList list;
  list.offer(offer("a"), 1, 0);
  list.offer(offer("b"), 2, 10);
  list.offer(offer("c"), 3, 20);
  list.offer(offer("d"), 4, 30);
  list.offer(offer("e"), 5, 40);
  const auto now = list.current(40);
  TEST_ASSERT_EQUAL(OfferList::kMax, now.size());
  for (const auto& entry : now) TEST_ASSERT_NOT_EQUAL(1u, entry.address);
}

static void test_offers_expire() {
  OfferList list;
  list.offer(offer("raspberrypi"), 7, 0);
  TEST_ASSERT_EQUAL(1, list.current(OfferList::kTtlMs).size());
  TEST_ASSERT_EQUAL(0, list.current(OfferList::kTtlMs + 1).size());
}

static void test_the_same_name_at_another_address_is_another_offer() {
  OfferList list;
  list.offer(offer("raspberrypi"), 7, 0);
  list.offer(offer("raspberrypi"), 8, 0);
  TEST_ASSERT_EQUAL(2, list.current(0).size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_an_offer_is_listed);
  RUN_TEST(test_the_same_host_is_only_refreshed);
  RUN_TEST(test_a_fifth_host_replaces_the_oldest);
  RUN_TEST(test_offers_expire);
  RUN_TEST(test_the_same_name_at_another_address_is_another_offer);
  return UNITY_END();
}

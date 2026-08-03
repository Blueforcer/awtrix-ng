
#include <unity.h>

#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/script/AsyncQueue.h"
#include "core/script/ScriptServices.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_fifo_and_bound() {
  script::AsyncQueue<int, 4> q;
  for (int i = 0; i < 6; ++i) q.push(int(i));
  int v;
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(2, v);
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(3, v);
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(4, v);
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(5, v);
  TEST_ASSERT_FALSE(q.pop(v));
}

static void test_drain_then_refill() {
  script::AsyncQueue<int, 2> q;
  q.push(1);
  q.push(2);
  q.push(3);
  int v;
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(2, v);
  q.push(4);
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(3, v);
  TEST_ASSERT_TRUE(q.pop(v));
  TEST_ASSERT_EQUAL_INT(4, v);
  TEST_ASSERT_FALSE(q.pop(v));
}

static void test_pop_on_empty_leaves_out_untouched() {
  script::AsyncQueue<std::string, 4> q;
  std::string out = "sentinel";
  TEST_ASSERT_FALSE(q.pop(out));
  TEST_ASSERT_EQUAL_STRING("sentinel", out.c_str());
}

static void test_carries_service_payloads() {
  script::AsyncQueue<script::HttpResult, 4> q;
  script::HttpResult in;
  in.id = 7;
  in.ok = true;
  in.body = "{\"temperature\":21.5}";
  q.push(std::move(in));

  script::HttpResult out;
  TEST_ASSERT_TRUE(q.pop(out));
  TEST_ASSERT_EQUAL_UINT32(7u, out.id);
  TEST_ASSERT_TRUE(out.ok);
  TEST_ASSERT_EQUAL_STRING("{\"temperature\":21.5}", out.body.c_str());

  script::AsyncQueue<script::MqttMessage, 4> mq;
  mq.push(script::MqttMessage{"awtrix/script/foo", "hello"});
  script::MqttMessage m;
  TEST_ASSERT_TRUE(mq.pop(m));
  TEST_ASSERT_EQUAL_STRING("awtrix/script/foo", m.topic.c_str());
  TEST_ASSERT_EQUAL_STRING("hello", m.payload.c_str());
}

static void test_concurrent_producers_do_not_corrupt_the_queue() {
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 2000;
  script::AsyncQueue<script::HttpResult, 8> q;

  std::atomic<bool> done{false};
  std::vector<script::HttpResult> got;
  got.reserve(kProducers * kPerProducer);

  std::thread consumer([&] {
    script::HttpResult out;
    while (!done.load(std::memory_order_acquire)) {
      while (q.pop(out)) got.push_back(out);
    }
    while (q.pop(out)) got.push_back(out);
  });

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&q, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        script::HttpResult r;
        r.id = static_cast<uint32_t>(p * kPerProducer + i);
        r.ok = true;
        r.body = std::string("producer-") + std::to_string(p) + "-item-" +
                 std::to_string(i) + std::string(64, 'x');
        q.push(std::move(r));
      }
    });
  }
  for (auto& t : producers) t.join();
  done.store(true, std::memory_order_release);
  consumer.join();

  std::set<uint32_t> seen;
  for (const auto& r : got) {
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.id < static_cast<uint32_t>(kProducers * kPerProducer));
    const int p = static_cast<int>(r.id) / kPerProducer;
    const int i = static_cast<int>(r.id) % kPerProducer;
    const std::string expect = std::string("producer-") + std::to_string(p) + "-item-" +
                               std::to_string(i) + std::string(64, 'x');
    TEST_ASSERT_EQUAL_STRING(expect.c_str(), r.body.c_str());
    TEST_ASSERT_TRUE_MESSAGE(seen.insert(r.id).second, "queue yielded a duplicate");
  }
  TEST_ASSERT_TRUE(got.size() > 8);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fifo_and_bound);
  RUN_TEST(test_drain_then_refill);
  RUN_TEST(test_pop_on_empty_leaves_out_untouched);
  RUN_TEST(test_carries_service_payloads);
  RUN_TEST(test_concurrent_producers_do_not_corrupt_the_queue);
  return UNITY_END();
}

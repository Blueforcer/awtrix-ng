#include <unity.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <Arduino.h>

#include "system/Log.h"

namespace awtrix {
void logf(const char*, ...) {}
void logdbg(const char*, ...) {}
}

#include "transport/net/HostResolverEsp32.cpp"

using namespace awtrix;
using net::ResolveState;

namespace {

constexpr uint32_t kFirstAddress = 0x0100000au;   // 10.0.0.1, in network byte order on a little-endian host
constexpr uint32_t kSecondAddress = 0x0200000au;  // 10.0.0.2

// Lives until the test has seen its task exit; the firmware's resolvers are never destroyed.
std::unique_ptr<net::IHostResolver> resolver;

std::atomic<int> lookups{0};
std::atomic<int> lookupsRunning{0};
std::atomic<int> mostRunning{0};

uint32_t addressOf(const char* host) {
  return std::strcmp(host, "second.example") == 0 ? kSecondAddress : kFirstAddress;
}

// Answers at once unless a test closes the gate.
std::mutex gateLock;
std::condition_variable gateChanged;
bool gateClosed = false;

void closeGate() {
  std::lock_guard<std::mutex> lock(gateLock);
  gateClosed = true;
}

void openGate() {
  {
    std::lock_guard<std::mutex> lock(gateLock);
    gateClosed = false;
  }
  gateChanged.notify_all();
}

void passGate() {
  std::unique_lock<std::mutex> lock(gateLock);
  gateChanged.wait(lock, [] { return !gateClosed; });
}

int answerDns(const char* name, IPAddress& out) {
  const int running = ++lookupsRunning;
  int seen = mostRunning.load();
  while (running > seen && !mostRunning.compare_exchange_weak(seen, running)) {}
  ++lookups;
  passGate();
  out = addressOf(name);
  --lookupsRunning;
  return 1;
}

bool waitForLookupStart(int count) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (lookups.load() < count) {
    if (std::chrono::steady_clock::now() > until) return false;
    std::this_thread::yield();
  }
  return true;
}

ResolveState waitForAnswer(net::IHostResolver& resolver, const std::string& host) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    const ResolveState state = resolver.resolve(host);
    if (state != ResolveState::Pending) return state;
    if (std::chrono::steady_clock::now() > until) return ResolveState::Pending;
    std::this_thread::yield();
  }
}

}

void setUp() {
  fake::resetTasks();
  fake::wifiUp = true;
  fake::dnsLookup = answerDns;
  fake::mdnsLookup = nullptr;
  lookups = 0;
  lookupsRunning = 0;
  mostRunning = 0;
  openGate();
  resolver = net::makeHostResolver();
}

void tearDown() {
  openGate();
  TEST_ASSERT_TRUE(fake::waitForNoTasks());
  resolver.reset();
  TEST_ASSERT_EQUAL_INT(0, fake::tasksReturned.load());
}

static void test_a_new_resolver_and_a_literal_address_start_no_task() {
  TEST_ASSERT_TRUE(resolver->resolve("192.168.1.9") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_UINT32(0xC0A80109u, resolver->address());
  TEST_ASSERT_EQUAL_INT(0, fake::tasksCreated.load());
}

static void test_the_task_exits_after_answering() {
  TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Pending);
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_UINT32(0x0A000001u, resolver->address());
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
  TEST_ASSERT_TRUE(fake::waitForNoTasks());
}

static void test_a_cached_answer_needs_no_task_and_forget_starts_a_new_one() {
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_TRUE(fake::waitForNoTasks());

  TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());

  resolver->forget();
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_INT(2, fake::tasksCreated.load());
}

static void test_no_memory_for_the_task_fails_at_once_and_the_next_call_retries() {
  fake::failCreates = 1;
  TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Failed);
  TEST_ASSERT_TRUE(resolver->error() == net::LinkError::Timeout);
  TEST_ASSERT_EQUAL_INT(0, lookups.load());

  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_UINT32(0x0A000001u, resolver->address());
  TEST_ASSERT_TRUE(resolver->error() == net::LinkError::None);
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
}

static void test_a_failed_lookup_is_reported_and_retried() {
  fake::dnsLookup = [](const char*, IPAddress&) { return 0; };
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Failed);
  TEST_ASSERT_TRUE(resolver->error() == net::LinkError::HostNotFound);
  TEST_ASSERT_TRUE(fake::waitForNoTasks());

  fake::dnsLookup = answerDns;
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_INT(2, fake::tasksCreated.load());
}

// The stale lookup cannot be interrupted, so the new one waits for the same task to get to it.
static void test_a_new_host_during_a_lookup_reuses_the_task_and_gets_its_own_answer() {
  closeGate();
  TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Pending);
  TEST_ASSERT_TRUE(waitForLookupStart(1));

  TEST_ASSERT_TRUE(resolver->resolve("second.example") == ResolveState::Pending);
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
  openGate();

  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "second.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_UINT32(0x0A000002u, resolver->address());
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
  TEST_ASSERT_EQUAL_INT(2, lookups.load());
}

static void test_a_cancelled_lookup_answers_nobody_and_its_task_still_exits() {
  closeGate();
  TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Pending);
  TEST_ASSERT_TRUE(waitForLookupStart(1));
  resolver->forget();
  openGate();
  TEST_ASSERT_TRUE(fake::waitForNoTasks());

  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "second.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_UINT32(0x0A000002u, resolver->address());
  TEST_ASSERT_EQUAL_INT(2, fake::tasksCreated.load());
}

// A task that is on its way out must not swallow a request made in that moment, and two tasks must
// never look anything up side by side. Timing here is whatever the scheduler makes of it, so this
// goes through the exit window many times rather than aiming at it.
static void test_lookups_racing_the_task_exit_are_never_lost() {
  for (int round = 0; round < 3000; ++round) {
    const std::string host = round % 2 ? "second.example" : "first.example";
    resolver->forget();
    TEST_ASSERT_TRUE_MESSAGE(waitForAnswer(*resolver, host) == ResolveState::Ready, "lookup lost");
    TEST_ASSERT_EQUAL_UINT32(round % 2 ? 0x0A000002u : 0x0A000001u, resolver->address());
    if (round % 3 == 0) std::this_thread::yield();
  }
  TEST_ASSERT_EQUAL_INT(1, mostRunning.load());
}

static void test_requests_replacing_each_other_during_the_exit_still_get_an_answer() {
  for (int round = 0; round < 1000; ++round) {
    resolver->forget();
    TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Pending);
    TEST_ASSERT_TRUE(waitForAnswer(*resolver, "second.example") == ResolveState::Ready);
    TEST_ASSERT_EQUAL_UINT32(0x0A000002u, resolver->address());
  }
  TEST_ASSERT_EQUAL_INT(1, mostRunning.load());
}

// lwIP holds on to the answer's address until a reply comes, which can be after the task is gone.
static void test_the_dns_answer_does_not_live_on_the_task_stack() {
  static std::atomic<IPAddress*> firstAnswer{nullptr};
  static std::atomic<IPAddress*> secondAnswer{nullptr};
  firstAnswer = nullptr;
  secondAnswer = nullptr;
  fake::dnsLookup = [](const char* name, IPAddress& out) {
    IPAddress* expected = nullptr;
    if (!firstAnswer.compare_exchange_strong(expected, &out)) secondAnswer = &out;
    out = addressOf(name);
    return 1;
  };
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_TRUE(fake::waitForNoTasks());
  resolver->forget();
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "first.example") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_INT(2, fake::tasksCreated.load());
  TEST_ASSERT_NOT_NULL(secondAnswer.load());
  TEST_ASSERT_EQUAL_PTR(firstAnswer.load(), secondAnswer.load());
}

static void test_mdns_comes_first_for_a_local_name() {
  fake::mdnsLookup = [](const char*) { return IPAddress(kFirstAddress); };
  TEST_ASSERT_TRUE(waitForAnswer(*resolver, "clock.local") == ResolveState::Ready);
  TEST_ASSERT_EQUAL_UINT32(0x0A000001u, resolver->address());
  TEST_ASSERT_EQUAL_INT(0, lookups.load());
}

static void test_without_wifi_nothing_starts() {
  fake::wifiUp = false;
  TEST_ASSERT_TRUE(resolver->resolve("first.example") == ResolveState::Failed);
  TEST_ASSERT_TRUE(resolver->error() == net::LinkError::NoWifi);
  TEST_ASSERT_EQUAL_INT(0, fake::tasksCreated.load());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_resolver_and_a_literal_address_start_no_task);
  RUN_TEST(test_the_task_exits_after_answering);
  RUN_TEST(test_a_cached_answer_needs_no_task_and_forget_starts_a_new_one);
  RUN_TEST(test_no_memory_for_the_task_fails_at_once_and_the_next_call_retries);
  RUN_TEST(test_a_failed_lookup_is_reported_and_retried);
  RUN_TEST(test_a_new_host_during_a_lookup_reuses_the_task_and_gets_its_own_answer);
  RUN_TEST(test_a_cancelled_lookup_answers_nobody_and_its_task_still_exits);
  RUN_TEST(test_lookups_racing_the_task_exit_are_never_lost);
  RUN_TEST(test_requests_replacing_each_other_during_the_exit_still_get_an_answer);
  RUN_TEST(test_the_dns_answer_does_not_live_on_the_task_stack);
  RUN_TEST(test_mdns_comes_first_for_a_local_name);
  RUN_TEST(test_without_wifi_nothing_starts);
  return UNITY_END();
}

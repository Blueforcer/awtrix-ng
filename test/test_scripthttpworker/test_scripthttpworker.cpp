#include <unity.h>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "system/ScriptHttpWorker.cpp"

using namespace awtrix;

namespace awtrix {
void logf(const char*, ...) {}
void logdbg(const char*, ...) {}
}

void ScriptHttpWorker::fetchModbus(const script::HttpRequest& req) {
  script::HttpResult result;
  result.id = req.id;
  onResult_(std::move(result));
}

namespace {

script::HttpRequest get(uint32_t id, const std::string& url = "http://host/path") {
  script::HttpRequest request;
  request.id = id;
  request.method = "GET";
  request.url = url;
  return request;
}

std::mutex resultsLock;
std::vector<script::HttpResult> results;

ScriptHttpWorker::ResultFn collect() {
  return [](script::HttpResult result) {
    std::lock_guard<std::mutex> lock(resultsLock);
    results.push_back(std::move(result));
  };
}

std::size_t resultCount() {
  std::lock_guard<std::mutex> lock(resultsLock);
  return results.size();
}

bool waitForResults(std::size_t count) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (resultCount() < count) {
    if (std::chrono::steady_clock::now() > until) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

void stopWorker() {
  fake::stopTasks = true;
  TEST_ASSERT_TRUE(fake::waitForNoTasks());
}

}

void setUp() {
  fake::resetTasks();
  fake::wifiUp = true;
  fake::httpFetches = 0;
  fake::httpStatus = 200;
  fake::httpBody.clear();
  results.clear();
}

void tearDown() {}

static void test_begin_starts_no_task() {
  ScriptHttpWorker worker;
  worker.begin(collect());
  TEST_ASSERT_EQUAL_INT(0, fake::tasksCreated.load());
}

static void test_without_begin_nothing_is_accepted() {
  ScriptHttpWorker worker;
  TEST_ASSERT_FALSE(worker.request(get(1)));
  TEST_ASSERT_EQUAL_INT(0, fake::tasksCreated.load());
}

static void test_a_refused_request_starts_no_task() {
  fake::runTasks = false;
  ScriptHttpWorker worker;
  worker.begin(collect());

  TEST_ASSERT_FALSE(worker.request(get(1, "ftp://host/")));
  TEST_ASSERT_FALSE(worker.request(get(2, "modbus://")));
  script::HttpRequest noMethod = get(3);
  noMethod.method.clear();
  TEST_ASSERT_FALSE(worker.request(noMethod));
  fake::wifiUp = false;
  TEST_ASSERT_FALSE(worker.request(get(4)));

  TEST_ASSERT_EQUAL_INT(0, fake::tasksCreated.load());
}

static void test_the_first_accepted_request_starts_the_only_task() {
  fake::runTasks = false;
  ScriptHttpWorker worker;
  worker.begin(collect());
  TEST_ASSERT_TRUE(worker.request(get(1)));
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
  TEST_ASSERT_TRUE(worker.request(get(2)));
  TEST_ASSERT_TRUE(worker.request(get(3, "https://host/")));
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
}

// False is what the script's prelude turns into cb(nil, 0).
static void test_no_memory_for_the_task_refuses_and_a_later_request_retries() {
  ScriptHttpWorker worker;
  worker.begin(collect());
  fake::failCreates = 2;
  TEST_ASSERT_FALSE(worker.request(get(1)));
  TEST_ASSERT_FALSE(worker.request(get(2)));
  TEST_ASSERT_EQUAL_INT(0, fake::tasksCreated.load());
  TEST_ASSERT_EQUAL_size_t(0, resultCount());

  fake::httpBody = "answer";
  TEST_ASSERT_TRUE(worker.request(get(3)));
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
  TEST_ASSERT_TRUE(waitForResults(1));
  stopWorker();

  // The refused requests did not stay in the queue: only the third one reached the network.
  TEST_ASSERT_EQUAL_INT(1, fake::httpFetches.load());
  TEST_ASSERT_EQUAL_size_t(1, resultCount());
  TEST_ASSERT_EQUAL_UINT32(3u, results[0].id);
}

static void test_the_started_task_answers_the_request() {
  fake::httpBody = "answer";
  ScriptHttpWorker worker;
  worker.begin(collect());
  TEST_ASSERT_TRUE(worker.request(get(1)));
  TEST_ASSERT_TRUE(waitForResults(1));
  stopWorker();
  TEST_ASSERT_TRUE(results[0].ok);
  TEST_ASSERT_EQUAL_INT(200, results[0].status);
  TEST_ASSERT_EQUAL_STRING("answer", results[0].body.c_str());
  TEST_ASSERT_EQUAL_INT(0, fake::tasksReturned.load());
}

static void test_the_queue_bound_holds_from_the_first_request() {
  fake::runTasks = false;
  ScriptHttpWorker worker;
  worker.begin(collect());
  for (uint32_t id = 1; id <= 16; ++id) TEST_ASSERT_TRUE(worker.request(get(id)));
  TEST_ASSERT_FALSE(worker.request(get(17)));
  TEST_ASSERT_EQUAL_INT(1, fake::tasksCreated.load());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_starts_no_task);
  RUN_TEST(test_without_begin_nothing_is_accepted);
  RUN_TEST(test_a_refused_request_starts_no_task);
  RUN_TEST(test_the_first_accepted_request_starts_the_only_task);
  RUN_TEST(test_no_memory_for_the_task_refuses_and_a_later_request_retries);
  RUN_TEST(test_the_started_task_answers_the_request);
  RUN_TEST(test_the_queue_bound_holds_from_the_first_request);
  return UNITY_END();
}

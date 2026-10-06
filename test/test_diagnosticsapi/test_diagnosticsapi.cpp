#include <unity.h>

#include <string>

#include "core/api/DiagnosticsApi.h"
#include "core/api/JsonReader.h"

using namespace awtrix::api;
void setUp() {}
void tearDown() {}

namespace {
struct Response final : Reply {
  HttpResult result;
  bool streamed = false, finished = false;
  void send(const HttpResult& value, bool streaming = false) override { result = value; streamed = streaming; }
  void header(const char*, const std::string&) override {}
  void chunk(const char* data, std::size_t size) override { result.body.append(data, size); finished = size == 0; }
  bool sendFile(const std::string&, const char*) override { return false; }
};

void append(void* context, const char* data, std::size_t size) {
  static_cast<std::string*>(context)->append(data, size);
}

void test_scan_writer_escapes_names_and_preserves_signal_and_security() {
  std::string body;
  JsonStream output(append, &body);
  WifiScanWriter writer(output);
  writer.network("Cafe \"A\"\\guest\n", -72, true);
  writer.network("", -10, false);
  writer.end();
  TEST_ASSERT_TRUE(isWellFormed(body));
  JsonReader array(body);
  TEST_ASSERT_TRUE(array.enterArray() && array.nextElement());
  std::string ssid;
  TEST_ASSERT_TRUE(memberValue(array, "ssid").appendString(ssid));
  TEST_ASSERT_EQUAL_STRING("Cafe \"A\"\\guest\n", ssid.c_str());
  long long rssi = 0;
  TEST_ASSERT_TRUE(memberValue(array, "rssi").asLong(rssi));
  TEST_ASSERT_EQUAL_INT(-72, rssi);
  bool secure = false;
  TEST_ASSERT_TRUE(memberValue(array, "enc").asBool(secure) && secure);
  TEST_ASSERT_TRUE(array.skipValue() && array.nextElement());
  TEST_ASSERT_TRUE(memberValue(array, "enc").asBool(secure) && !secure);
  TEST_ASSERT_TRUE(array.skipValue());
  TEST_ASSERT_FALSE(array.nextElement());
}

void test_scan_writer_empty_and_pending_replies_are_distinct() {
  std::string body;
  JsonStream output(append, &body);
  WifiScanWriter writer(output);
  writer.end();
  TEST_ASSERT_EQUAL_STRING("[]", body.c_str());
  const auto pending = wifiScanPending();
  TEST_ASSERT_EQUAL_INT(202, pending.status);
  bool scanning = false;
  TEST_ASSERT_TRUE(memberValue(JsonReader(pending.body), "scanning").asBool(scanning) && scanning);
}

uint32_t seenCursor;
void logs(uint32_t after, JsonStream& out) {
  seenCursor = after;
  out.put("{\"lines\":[");
  out.putString(std::string(600, 'x').c_str());
  out.put("]}");
}

void test_logs_route_passes_cursor_and_finishes_chunked_reply() {
  const std::string method = "GET", path = "/api/v1/logs", body;
  const Request request{method, path, body, [](const char* name, std::string& value) {
    if (std::string(name) != "after") return false;
    value = "4294967295"; return true;
  }, {}, {}};
  Response response;
  TEST_ASSERT_TRUE(routeDiagnostics(request, response, {}, logs));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, seenCursor);
  TEST_ASSERT_TRUE(response.streamed && response.finished);
  TEST_ASSERT_TRUE(isWellFormed(response.result.body));
  TEST_ASSERT_TRUE(response.result.body.size() > JsonStream::kCapacity);
}

void test_scan_route_uses_platform_result_and_leaves_other_methods_unmatched() {
  const std::string get = "GET", put = "PUT", path = "/api/v1/system/wifi-scan", body;
  Response response;
  TEST_ASSERT_TRUE(routeDiagnostics({get, path, body, {}, {}, {}}, response,
      [](Reply& reply) { reply.send(wifiScanPending()); }, logs));
  TEST_ASSERT_EQUAL_INT(202, response.result.status);
  TEST_ASSERT_FALSE(response.streamed);
  TEST_ASSERT_TRUE(routeDiagnostics({get, path, body, {}, {}, {}}, response, {}, logs));
  TEST_ASSERT_EQUAL_INT(200, response.result.status);
  TEST_ASSERT_TRUE(isWellFormed(response.result.body));
  TEST_ASSERT_FALSE(routeDiagnostics({put, path, body, {}, {}, {}}, response, {}, logs));
}
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scan_writer_escapes_names_and_preserves_signal_and_security);
  RUN_TEST(test_scan_writer_empty_and_pending_replies_are_distinct);
  RUN_TEST(test_logs_route_passes_cursor_and_finishes_chunked_reply);
  RUN_TEST(test_scan_route_uses_platform_result_and_leaves_other_methods_unmatched);
  return UNITY_END();
}

#include <unity.h>

#include <map>
#include <string>

#include "core/AssetPaths.h"
#include "core/api/ApiRouter.h"
#include "core/api/HttpProtocol.h"

using namespace awtrix;
void setUp() {}
void tearDown() {}

void test_etag_describes_content_independently_of_read_boundaries() {
  const std::string content("GIF8\0\xff", 6);
  api::Etag whole;
  whole.append(content.data(), content.size());
  for (std::size_t split = 0; split <= content.size(); ++split) {
    api::Etag chunks;
    chunks.append(content.data(), split);
    chunks.append(content.data() + split, content.size() - split);
    TEST_ASSERT_EQUAL_STRING(whole.value().c_str(), chunks.value().c_str());
  }
  api::Etag changed;
  changed.append("GIF8xx", 6);
  TEST_ASSERT_TRUE(whole.value() != changed.value());
  TEST_ASSERT_TRUE(whole.value() != api::Etag().value());
  TEST_ASSERT_EQUAL('"', whole.value().front());
  TEST_ASSERT_EQUAL('"', whole.value().back());
}

void test_mime_types_keep_binary_files_downloadable() {
  TEST_ASSERT_EQUAL_STRING("image/jpeg", assets::mimeType("/ICONS/a.jpeg"));
  TEST_ASSERT_EQUAL_STRING("image/gif", assets::mimeType("/ICONS/a.gif"));
  TEST_ASSERT_EQUAL_STRING("audio/mpeg", assets::mimeType("/SCRIPTS/game/sound.mp3"));
  TEST_ASSERT_EQUAL_STRING("text/plain", assets::mimeType("/PALETTES/a.txt"));
  TEST_ASSERT_EQUAL_STRING("application/octet-stream", assets::mimeType("/SCRIPTS/game.be"));
}

void test_upload_route_recognition_rejects_siblings_and_items() {
  for (const char* path : {"/api/v1/files", "/api/v1/audio/mp3", "/api/v1/restore", "/update",
                          "/api/v1/apps/script/Game/sounds"})
    TEST_ASSERT_TRUE_MESSAGE(api::isUploadRoute(path), path);
  for (const char* path : {"/api/v1/filesx", "/api/v1/audio/mp3/beep", "/update/x",
                          "/api/v1/apps/script/Game/sounds/beep", "/api/v1/apps/script/Game"})
    TEST_ASSERT_FALSE_MESSAGE(api::isUploadRoute(path), path);
}

void test_preflight_allows_the_same_headers_on_both_transports() {
  std::map<std::string, std::string> headers;
  const auto emit = [&](const char* name, const char* value) { headers[name] = value; };
  api::corsHeaders(emit, false);
  TEST_ASSERT_EQUAL_UINT(1, headers.size());
  TEST_ASSERT_EQUAL_STRING("*", headers["Access-Control-Allow-Origin"].c_str());
  api::corsHeaders(emit, true);
  TEST_ASSERT_TRUE(headers["Access-Control-Allow-Headers"].find("X-HTTP-Method-Override") != std::string::npos);
  TEST_ASSERT_TRUE(headers["Access-Control-Allow-Methods"].find("PATCH") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("true", headers["Access-Control-Allow-Private-Network"].c_str());
}

void test_credentials_and_firmware_routes_are_for_the_own_page_only() {
  TEST_ASSERT_TRUE(api::ownPageOnly("GET", "/api/v1/system", true));
  TEST_ASSERT_TRUE(api::ownPageOnly("PUT", "/api/v1/system", false));
  TEST_ASSERT_TRUE(api::ownPageOnly("POST", "/api/v1/restore", false));
  TEST_ASSERT_TRUE(api::ownPageOnly("POST", "/update", false));
  TEST_ASSERT_TRUE(api::ownPageOnly("POST", "/api/v1/device/factory-reset", false));
  TEST_ASSERT_FALSE(api::ownPageOnly("GET", "/api/v1/system", false));
  TEST_ASSERT_FALSE(api::ownPageOnly("OPTIONS", "/api/v1/system", true));
  TEST_ASSERT_FALSE(api::ownPageOnly("GET", "/api/v1/settings", true));
  TEST_ASSERT_FALSE(api::ownPageOnly("POST", "/api/v1/notifications", false));
  TEST_ASSERT_FALSE(api::ownPageOnly("POST", "/api/v1/files", false));
  TEST_ASSERT_FALSE(api::ownPageOnly("PUT", "/api/v1/apps/script/Game", false));
  TEST_ASSERT_FALSE(api::ownPageOnly("POST", "/api/v1/device/reboot", false));
}

void test_a_page_of_another_site_is_told_apart_from_the_own_page() {
  using api::BrowserOrigin;
  TEST_ASSERT_FALSE(api::fromOtherSite(BrowserOrigin{"", 0, "clock.local", 1, "", 0}));
  TEST_ASSERT_FALSE(api::fromOtherSite(BrowserOrigin{"http://clock.local", 1, "clock.local", 1, "same-origin", 1}));
  TEST_ASSERT_FALSE(api::fromOtherSite(BrowserOrigin{"", 0, "clock.local", 1, "none", 1}));
  TEST_ASSERT_FALSE(api::fromOtherSite(BrowserOrigin{"http://192.168.1.5:8080", 1, "192.168.1.5:8080", 1, "", 0}));
  TEST_ASSERT_TRUE(api::fromOtherSite(BrowserOrigin{"http://evil.example", 1, "clock.local", 1, "", 0}));
  TEST_ASSERT_TRUE(api::fromOtherSite(BrowserOrigin{"null", 1, "clock.local", 1, "", 0}));
  TEST_ASSERT_TRUE(api::fromOtherSite(BrowserOrigin{"", 0, "clock.local", 1, "cross-site", 1}));
  TEST_ASSERT_TRUE(api::fromOtherSite(BrowserOrigin{"", 0, "clock.local", 1, "same-site", 1}));
  TEST_ASSERT_TRUE(api::fromOtherSite(BrowserOrigin{"", 0, "clock.local", 1, "same-origin", 2}));
  TEST_ASSERT_TRUE(api::fromOtherSite(BrowserOrigin{"http://clock.local", 2, "clock.local", 1, "", 0}));
  const api::HttpResult refusal = api::forbiddenOrigin();
  TEST_ASSERT_EQUAL(403, refusal.status);
  TEST_ASSERT_TRUE(refusal.body.find("\"forbiddenOrigin\"") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_etag_describes_content_independently_of_read_boundaries);
  RUN_TEST(test_mime_types_keep_binary_files_downloadable);
  RUN_TEST(test_upload_route_recognition_rejects_siblings_and_items);
  RUN_TEST(test_preflight_allows_the_same_headers_on_both_transports);
  RUN_TEST(test_credentials_and_firmware_routes_are_for_the_own_page_only);
  RUN_TEST(test_a_page_of_another_site_is_told_apart_from_the_own_page);
  return UNITY_END();
}

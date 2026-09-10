#include <unity.h>

#include <set>
#include <string>

#include "core/icons/IconOrigins.h"

using namespace awtrix::iconorigins;

namespace {
struct MemoryStorage : Backend {
  std::string persisted;
  std::set<std::string> files;
  bool failRead = false, failWrite = false;
  int writes = 0;
  bool read(std::string& out) override { out = persisted; return !failRead; }
  bool writeAtomic(const std::string& json) override {
    if (failWrite) return false;
    persisted = json; ++writes; return true;
  }
  bool iconExists(const std::string& name) override { return files.count(name) != 0; }
};
Record record(const std::string& name = "mail.gif") {
  return {name, "https://hub.example/icons/", "mail", std::string(64, 'a')};
}
std::string single(const Record& r) {
  const auto all = serialize({r});
  return all.substr(10, all.size() - 12);
}
void test_empty_and_offline_read() {
  MemoryStorage s;
  const auto result = handle(s, "GET");
  TEST_ASSERT_EQUAL_INT(200, result.status);
  TEST_ASSERT_EQUAL_STRING("{\"icons\":[]}", result.body.c_str());
  TEST_ASSERT_EQUAL_INT(0, s.writes);
}
void test_link_survives_reopen_and_local_edit() {
  MemoryStorage s;
  s.files.insert("mail.gif");
  TEST_ASSERT_EQUAL_INT(200, handle(s, "PUT", single(record())).status);
  MemoryStorage reopened;
  reopened.persisted = s.persisted;
  reopened.files = s.files; // The icon bytes may have changed; its linkage SHA must not.
  const auto result = handle(reopened, "GET");
  TEST_ASSERT_EQUAL_STRING(serialize({record()}).c_str(), result.body.c_str());
  auto update = record(); update.slug = "mail-2";
  TEST_ASSERT_EQUAL_INT(200, handle(reopened, "PUT", single(update)).status);
  TEST_ASSERT_EQUAL_STRING(serialize({update}).c_str(), handle(reopened, "GET").body.c_str());
}
void test_missing_icon_cannot_be_linked_or_listed() {
  MemoryStorage s;
  TEST_ASSERT_EQUAL_INT(404, handle(s, "PUT", single(record())).status);
  s.persisted = serialize({record()});
  TEST_ASSERT_EQUAL_STRING("{\"icons\":[]}", handle(s, "GET").body.c_str());
  TEST_ASSERT_EQUAL_INT(0, s.writes);
}
void test_delete_removes_link_even_if_file_already_missing() {
  MemoryStorage s;
  s.persisted = serialize({record()});
  TEST_ASSERT_EQUAL_INT(200, handle(s, "DELETE", {}, "mail.gif").status);
  s.files.insert("mail.gif"); // A later unrelated upload must not inherit the old link.
  TEST_ASSERT_EQUAL_STRING("{\"icons\":[]}", handle(s, "GET").body.c_str());
  TEST_ASSERT_EQUAL_INT(200, handle(s, "DELETE", {}, "mail.gif").status);
  TEST_ASSERT_EQUAL_INT(1, s.writes);
}
void test_path_and_filename_validation() {
  MemoryStorage s;
  for (const auto* name : {"../mail.gif", "a/b.gif", "a\\b.gif", "/mail.gif", "mail.png",
                           "mail.GIF", "mail..gif", ".gif", "mail.gif?x", "a b.gif"}) {
    s.files.insert(name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, handle(s, "PUT", single(record(name))).status, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, handle(s, "DELETE", {}, name).status, name);
  }
  TEST_ASSERT_TRUE(validName("My_icon-12.jpg"));
  TEST_ASSERT_TRUE(validName(std::string(32, 'a') + ".gif"));
  TEST_ASSERT_FALSE(validName(std::string(33, 'a') + ".gif"));
  TEST_ASSERT_EQUAL_INT(0, s.writes);
}
void test_rejects_unsafe_hubs_and_accepts_configured_https_base() {
  MemoryStorage s; s.files.insert("mail.gif");
  for (const auto* hub : {"http://hub.example/icons/", "https:///icons/", "https://u@host/icons/",
                          "https://host/icons/?x", "https://host/icons/#x", "https://host\\evil/icons/",
                          "https://host%2f.evil/icons/", "https://host/a/../icons/",
                          "https://host:99999/icons/", "https://host:/icons/", "https://host/icons"}) {
    auto r = record(); r.hub = hub;
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, handle(s, "PUT", single(r)).status, hub);
  }
  auto r = record(); r.hub = "https://custom.example:8443/awtrix/icons/";
  TEST_ASSERT_EQUAL_INT(200, handle(s, "PUT", single(r)).status);
}
void test_requires_complete_valid_record() {
  MemoryStorage s; s.files.insert("mail.gif");
  for (const auto* body : {"{}", "[]", "null", "{", "{\"name\":1}"})
    TEST_ASSERT_EQUAL_INT(400, handle(s, "PUT", body).status);
  auto r = record(); r.slug = "Mail";
  TEST_ASSERT_EQUAL_INT(400, handle(s, "PUT", single(r)).status);
  r = record(); r.sha256[0] = 'A';
  TEST_ASSERT_EQUAL_INT(400, handle(s, "PUT", single(r)).status);
  r = record(); r.sha256.pop_back();
  TEST_ASSERT_EQUAL_INT(400, handle(s, "PUT", single(r)).status);
  auto duplicate = single(record()); duplicate.insert(1, "\"name\":\"other.gif\",");
  TEST_ASSERT_EQUAL_INT(400, handle(s, "PUT", duplicate).status);
  TEST_ASSERT_EQUAL_INT(413, handle(s, "PUT", std::string(1025, ' ')).status);
  TEST_ASSERT_EQUAL_INT(405, handle(s, "POST", single(record())).status);
}
void test_failed_storage_never_reports_success_or_destroys_existing_data() {
  MemoryStorage s; s.files.insert("mail.gif"); s.persisted = serialize({record()});
  const auto before = s.persisted;
  s.failWrite = true;
  TEST_ASSERT_EQUAL_INT(500, handle(s, "DELETE", {}, "mail.gif").status);
  TEST_ASSERT_EQUAL_STRING(before.c_str(), s.persisted.c_str());
  s.failRead = true;
  TEST_ASSERT_EQUAL_INT(500, handle(s, "GET").status);
  TEST_ASSERT_EQUAL_INT(500, handle(s, "PUT", single(record())).status);
}
void test_corrupt_store_is_not_silently_overwritten() {
  MemoryStorage s; s.files.insert("mail.gif"); s.persisted = "{bad";
  TEST_ASSERT_EQUAL_INT(500, handle(s, "PUT", single(record())).status);
  TEST_ASSERT_EQUAL_STRING("{bad", s.persisted.c_str());
}
void test_record_capacity_allows_replacement_but_refuses_new_link() {
  MemoryStorage s;
  for (unsigned i = 0; i < kMaxRecords; ++i) {
    const auto name = "icon-" + std::to_string(i) + ".gif";
    s.files.insert(name);
    TEST_ASSERT_EQUAL_INT(200, handle(s, "PUT", single(record(name))).status);
  }
  s.files.insert("extra.gif");
  TEST_ASSERT_EQUAL_INT(507, handle(s, "PUT", single(record("extra.gif"))).status);
  TEST_ASSERT_EQUAL_INT(200, handle(s, "PUT", single(record("icon-0.gif"))).status);
}
void test_restore_validates_atomically_and_prunes_missing_icons() {
  MemoryStorage s; s.files.insert("mail.gif");
  std::string err;
  TEST_ASSERT_TRUE(restore(s, serialize({record(), record("gone.gif")}), err));
  TEST_ASSERT_EQUAL_STRING(serialize({record()}).c_str(), s.persisted.c_str());
  const auto before = s.persisted;
  TEST_ASSERT_FALSE(restore(s, serialize({record(), record()}), err));
  TEST_ASSERT_FALSE(restore(s, std::string(kMaxBytes + 1, ' '), err));
  TEST_ASSERT_EQUAL_STRING(before.c_str(), s.persisted.c_str());
  TEST_ASSERT_TRUE(restore(s, "{\"icons\":[]}", err));
}
}

void setUp() {}
void tearDown() {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_and_offline_read);
  RUN_TEST(test_link_survives_reopen_and_local_edit);
  RUN_TEST(test_missing_icon_cannot_be_linked_or_listed);
  RUN_TEST(test_delete_removes_link_even_if_file_already_missing);
  RUN_TEST(test_path_and_filename_validation);
  RUN_TEST(test_rejects_unsafe_hubs_and_accepts_configured_https_base);
  RUN_TEST(test_requires_complete_valid_record);
  RUN_TEST(test_failed_storage_never_reports_success_or_destroys_existing_data);
  RUN_TEST(test_corrupt_store_is_not_silently_overwritten);
  RUN_TEST(test_record_capacity_allows_replacement_but_refuses_new_link);
  RUN_TEST(test_restore_validates_atomically_and_prunes_missing_icons);
  return UNITY_END();
}

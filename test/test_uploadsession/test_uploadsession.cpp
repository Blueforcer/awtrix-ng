#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "core/api/AssetUploadSession.h"

using namespace awtrix::api;

void setUp() {}
void tearDown() {}

namespace {
struct MemoryStorage {
  std::map<std::string, std::string> files;
  std::vector<std::string> calls;
  std::string pending;
  bool beginOk = true;
  bool writeOk = true;
  bool publishOk = true;
  unsigned changed = 0;

  asset::StreamStorage adapter() {
    asset::StreamStorage storage;
    storage.exists = [this](const std::string& path) { return files.count(path) != 0; };
    storage.begin = [this](const std::string& path) {
      calls.push_back("begin:" + path);
      pending.clear();
      return beginOk;
    };
    storage.write = [this](const uint8_t* data, std::size_t size) {
      calls.push_back("write");
      if (!writeOk) return false;
      pending.append(reinterpret_cast<const char*>(data), size);
      return true;
    };
    storage.publish = [this](const std::string& path) {
      calls.push_back("publish:" + path);
      if (!publishOk) return false;
      files[path] = pending;
      pending.clear();
      return true;
    };
    storage.discard = [this] { calls.push_back("discard"); pending.clear(); };
    storage.changed = [this] { ++changed; };
    return storage;
  }
};

void write(asset::UploadSession& session, const std::string& bytes) {
  session.append(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

void request(asset::UploadSession& session, MemoryStorage& storage) {
  session.reset(storage.adapter());
  session.authorize(HttpResult{});
}

void upload(asset::UploadSession& session, const char* filename = "x.gif") {
  session.start("/ICONS", filename);
  write(session, "GIF8new");
  session.end();
}

void test_staging_does_not_change_old_asset_until_valid_end() {
  MemoryStorage storage;
  storage.files["/MELODIES/bell.txt"] = "old";
  asset::UploadSession session;
  request(session, storage);
  session.start("/MELODIES", "bell.txt");
  write(session, "bell:d=4:");
  TEST_ASSERT_EQUAL_STRING("old", storage.files["/MELODIES/bell.txt"].c_str());
  TEST_ASSERT_EQUAL_UINT(0, storage.changed);
  write(session, "c,e,g");
  session.end();
  TEST_ASSERT_EQUAL_STRING("bell:d=4:c,e,g", storage.files["/MELODIES/bell.txt"].c_str());
  TEST_ASSERT_EQUAL_UINT(1, storage.changed);
  TEST_ASSERT_EQUAL_INT(200, session.complete().status);
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
}

void test_authorization_is_required_before_storage_callbacks() {
  MemoryStorage storage;
  asset::UploadSession session;
  session.reset(storage.adapter());
  upload(session);
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
  TEST_ASSERT_TRUE(storage.calls.empty());
  for (int status : {401, 403, 405}) {
    session.reset(storage.adapter());
    session.authorize(HttpResult{status, "application/json", "rejected"});
    upload(session);
    TEST_ASSERT_EQUAL_INT(status, session.complete().status);
    TEST_ASSERT_TRUE(storage.calls.empty());
  }
}

void test_empty_new_request_cannot_reuse_previous_success_or_authorization() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  upload(session);
  TEST_ASSERT_EQUAL_INT(200, session.complete().status);
  session.reset(storage.adapter());
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
  session.reset(storage.adapter());
  session.authorize(HttpResult{401, "application/json", "rejected"});
  TEST_ASSERT_EQUAL_INT(401, session.complete().status);
  TEST_ASSERT_EQUAL_UINT(1, storage.changed);
}

void test_validation_failure_is_sticky_across_later_valid_parts() {
  MemoryStorage storage;
  storage.files["/ICONS/x.gif"] = "old";
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  write(session, "wrong");
  session.end();
  const auto count = storage.calls.size();
  session.authorize(HttpResult{});
  upload(session, "later.gif");
  TEST_ASSERT_EQUAL_INT(415, session.complete().status);
  TEST_ASSERT_EQUAL_UINT(count, storage.calls.size());
  TEST_ASSERT_EQUAL_STRING("old", storage.files["/ICONS/x.gif"].c_str());
  TEST_ASSERT_EQUAL_UINT(0, storage.changed);
}

void test_empty_file_is_rejected_and_old_target_is_preserved() {
  MemoryStorage storage;
  storage.files["/ICONS/x.gif"] = "old";
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  session.end();
  TEST_ASSERT_EQUAL_INT(415, session.complete().status);
  TEST_ASSERT_EQUAL_STRING("old", storage.files["/ICONS/x.gif"].c_str());
  TEST_ASSERT_EQUAL_UINT(0, storage.changed);
  TEST_ASSERT_TRUE(storage.pending.empty());
}

void test_storage_failures_preserve_old_target_and_discard_staging() {
  for (int failure = 0; failure != 3; ++failure) {
    MemoryStorage storage;
    storage.files["/ICONS/x.gif"] = "old";
    storage.beginOk = failure != 0;
    storage.writeOk = failure != 1;
    storage.publishOk = failure != 2;
    asset::UploadSession session;
    request(session, storage);
    upload(session);
    TEST_ASSERT_EQUAL_INT(507, session.complete().status);
    TEST_ASSERT_EQUAL_STRING("old", storage.files["/ICONS/x.gif"].c_str());
    TEST_ASSERT_EQUAL_STRING("discard", storage.calls.back().c_str());
    TEST_ASSERT_TRUE(storage.pending.empty());
    TEST_ASSERT_EQUAL_UINT(0, storage.changed);
  }
}

void test_parser_abort_cleans_staging_without_publishing() {
  MemoryStorage storage;
  storage.files["/ICONS/x.gif"] = "old";
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  write(session, "GIF8");
  session.abort();
  session.end();
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
  TEST_ASSERT_EQUAL_STRING("old", storage.files["/ICONS/x.gif"].c_str());
  TEST_ASSERT_TRUE(storage.pending.empty());
  TEST_ASSERT_EQUAL_UINT(0, storage.changed);
}

void test_new_request_discards_abandoned_staging() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  write(session, "GIF8");
  session.reset(storage.adapter());
  TEST_ASSERT_EQUAL_STRING("discard", storage.calls.back().c_str());
  TEST_ASSERT_TRUE(storage.pending.empty());
  TEST_ASSERT_TRUE(storage.files.empty());
  session.authorize(HttpResult{});
  upload(session, "next.gif");
  TEST_ASSERT_EQUAL_INT(200, session.complete().status);
  TEST_ASSERT_EQUAL_UINT(1, storage.files.size());
}

void test_incomplete_file_fails_completion_and_cleans_up() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  write(session, "GIF8");
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
  TEST_ASSERT_TRUE(storage.pending.empty());
  TEST_ASSERT_TRUE(storage.files.empty());
}

void test_second_start_before_end_is_rejected() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  write(session, "GIF8");
  session.start("/ICONS", "other.gif");
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
  TEST_ASSERT_TRUE(storage.pending.empty());
  TEST_ASSERT_TRUE(storage.files.empty());
}

void test_path_rejection_precedes_storage_and_is_sticky() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  session.start("/MP3", "nested/unplayable.mp3");
  upload(session);
  TEST_ASSERT_EQUAL_INT(400, session.complete().status);
  TEST_ASSERT_TRUE(storage.calls.empty());
}

void test_name_of_the_other_kind_is_refused_before_storage() {
  MemoryStorage storage;
  storage.files["/MELODIES/ding.txt"] = "ding:d=4:c";
  asset::UploadSession session;
  request(session, storage);
  session.start("/MP3", "ding.mp3");
  write(session, "ID3");
  session.end();
  const HttpResult result = session.complete();
  TEST_ASSERT_EQUAL_INT(409, result.status);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, result.body.find("nameTaken"));
  TEST_ASSERT_TRUE(storage.calls.empty());
  TEST_ASSERT_EQUAL_UINT(0, storage.files.count("/MP3/ding.mp3"));
}

void test_script_sound_upload_ignores_melody_names() {
  MemoryStorage storage;
  storage.files["/MELODIES/boost.txt"] = "boost:d=4:c";
  asset::UploadSession session;
  request(session, storage);
  asset::UploadTarget target;
  target.path = "/SCRIPTS/Racer/boost.mp3";
  target.result.status = 200;
  session.start(target);
  write(session, "ID3");
  session.end();
  TEST_ASSERT_EQUAL_INT(200, session.complete().status);
  TEST_ASSERT_EQUAL_STRING("ID3", storage.files["/SCRIPTS/Racer/boost.mp3"].c_str());
}

void test_multiple_files_publish_individually_and_late_failure_stays_visible() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  upload(session, "one.gif");
  upload(session, "two.gif");
  session.start("/ICONS", "empty.gif");
  session.end();
  TEST_ASSERT_EQUAL_INT(415, session.complete().status);
  TEST_ASSERT_EQUAL_UINT(2, storage.files.size());
  TEST_ASSERT_EQUAL_UINT(2, storage.changed);
}

void test_policy_rejection_cancels_active_staging() {
  MemoryStorage storage;
  asset::UploadSession session;
  request(session, storage);
  session.start("/ICONS", "x.gif");
  write(session, "GIF8");
  session.authorize(HttpResult{401, "application/json", "rejected"});
  session.end();
  TEST_ASSERT_EQUAL_INT(401, session.complete().status);
  TEST_ASSERT_TRUE(storage.pending.empty());
  TEST_ASSERT_TRUE(storage.files.empty());
}
}

void test_created_directory_is_removed_on_failure_but_retained_after_publish() {
  for (bool existed : {false, true}) {
    for (bool succeeds : {false, true}) {
      MemoryStorage storage;
      bool directory = existed;
      auto backend = storage.adapter();
      backend.mkdir = [&](const std::string&, bool& created) {
        created = !directory;
        directory = true;
        return true;
      };
      backend.prune = [&](const std::string&) { directory = false; };
      storage.publishOk = succeeds;
      asset::UploadSession session;
      session.reset(std::move(backend));
      session.authorize({});
      session.start(asset::UploadTarget{"/SCRIPTS/Game/start.mp3", {}});
      write(session, "ID3music");
      session.end();
      TEST_ASSERT_EQUAL_INT(succeeds ? 200 : 507, session.complete().status);
      TEST_ASSERT_EQUAL(existed || succeeds, directory);
      TEST_ASSERT_EQUAL(succeeds, storage.files.count("/SCRIPTS/Game/start.mp3") != 0);
    }
  }
}

void test_failed_folder_creation_does_not_start_or_publish_a_file() {
  MemoryStorage storage;
  auto backend = storage.adapter();
  backend.mkdir = [](const std::string&, bool&) { return false; };
  asset::UploadSession session;
  session.reset(std::move(backend));
  session.authorize({});
  upload(session);
  TEST_ASSERT_EQUAL_INT(507, session.complete().status);
  TEST_ASSERT_TRUE(storage.files.empty());
  TEST_ASSERT_TRUE(storage.pending.empty());
  TEST_ASSERT_EQUAL_UINT(0, storage.changed);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_created_directory_is_removed_on_failure_but_retained_after_publish);
  RUN_TEST(test_failed_folder_creation_does_not_start_or_publish_a_file);
  RUN_TEST(test_staging_does_not_change_old_asset_until_valid_end);
  RUN_TEST(test_authorization_is_required_before_storage_callbacks);
  RUN_TEST(test_empty_new_request_cannot_reuse_previous_success_or_authorization);
  RUN_TEST(test_validation_failure_is_sticky_across_later_valid_parts);
  RUN_TEST(test_empty_file_is_rejected_and_old_target_is_preserved);
  RUN_TEST(test_storage_failures_preserve_old_target_and_discard_staging);
  RUN_TEST(test_parser_abort_cleans_staging_without_publishing);
  RUN_TEST(test_new_request_discards_abandoned_staging);
  RUN_TEST(test_incomplete_file_fails_completion_and_cleans_up);
  RUN_TEST(test_second_start_before_end_is_rejected);
  RUN_TEST(test_path_rejection_precedes_storage_and_is_sticky);
  RUN_TEST(test_name_of_the_other_kind_is_refused_before_storage);
  RUN_TEST(test_script_sound_upload_ignores_melody_names);
  RUN_TEST(test_multiple_files_publish_individually_and_late_failure_stays_visible);
  RUN_TEST(test_policy_rejection_cancels_active_staging);
  return UNITY_END();
}

#include <unity.h>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/api/FilesApi.h"
#include "core/api/JsonReader.h"

using namespace awtrix;
using namespace awtrix::api;
void setUp() {}
void tearDown() {}

namespace {
struct Origins final : iconorigins::Backend {
  std::string json;
  bool writable = true;
  bool read(std::string& out) override { out = json; return true; }
  bool writeAtomic(const std::string& value) override { if (!writable) return false; json = value; return true; }
  bool iconExists(const std::string&) override { return true; }
};

struct Storage final : Files {
  std::map<std::string, std::string> contents;
  std::set<std::string> unreadable;
  std::vector<std::string> calls;
  bool writable = true;
  unsigned notifications = 0;
  Storage(const scriptsounds::InstalledScripts& scripts, Origins& origins) : Files(scripts, origins) {}
  void list(const std::string& directory, const Visit& visit) override {
    calls.push_back("list:" + directory);
    const std::string prefix = directory == "/" ? directory : directory + "/";
    std::set<std::string> directories;
    for (const auto& file : contents) {
      if (file.first.rfind(prefix, 0) != 0) continue;
      const auto tail = file.first.substr(prefix.size());
      const auto slash = tail.find('/');
      if (slash == std::string::npos) visit({tail.c_str(), file.second.size(), false});
      else if (directories.insert(tail.substr(0, slash)).second) {
        const auto name = tail.substr(0, slash);
        visit({name.c_str(), 0, true});
      }
    }
  }
  bool exists(const std::string& path) override { return contents.count(path); }
  bool read(const std::string& path, std::string& value) override {
    if (!exists(path) || unreadable.count(path)) return false;
    value = contents[path]; return true;
  }
  bool write(const std::string& path, const std::string& value) override {
    if (!writable) return false;
    contents[path] = value; return true;
  }
  bool remove(const std::string& path) override {
    calls.push_back("remove:" + path); return contents.erase(path);
  }
  bool rename(const std::string& from, const std::string& to) override {
    if (!exists(from)) return false;
    contents[to] = contents[from]; contents.erase(from); return true;
  }
  bool mkdir(const std::string&, bool& created) override { created = false; return true; }
  void prune(const std::string& directory) override { calls.push_back("prune:" + directory); }
  void usage(uint64_t& used, uint64_t& total) override { used = 12; total = 1000; }
  bool etag(const std::string& path, std::string& value) override {
    calls.push_back("etag:" + path);
    if (!exists(path)) return false;
    value = "\"file-tag\""; return true;
  }
  bool sha256(const std::string& path, std::string& value, uint64_t& size) override {
    calls.push_back("hash:" + path);
    if (!exists(path) || unreadable.count(path)) return false;
    value = "digest"; size = 42; return true;  // Actual read size, independent of directory metadata.
  }
  void release(const std::string& path) override { calls.push_back("release:" + path); }
  void changed() override { ++notifications; }
};

struct Response final : Reply {
  HttpResult result;
  std::map<std::string, std::string> headers;
  std::string served;
  bool streaming = false, finished = false;
  void send(const HttpResult& response, bool streamed = false) override { result = response; streaming = streamed; }
  void header(const char* name, const std::string& value) override { headers[name] = value; }
  void chunk(const char* bytes, std::size_t size) override { result.body.append(bytes, size); finished = size == 0; }
  bool sendFile(const std::string& path, const char* type) override { served = path; result.contentType = type; return true; }
};

struct Fixture {
  script::ConfigTextFn source = [](const std::string& name, std::string& text) {
    if (name != "game") return false;
    text = "# @name My Game\n"; return true;
  };
  scriptsounds::InstalledScripts scripts{nullptr, source};
  Origins origins;
  Storage files{scripts, origins};
  Response response;
  std::map<std::string, std::string> query;
  std::string etag, uploaded;
  bool route(const std::string& method, const std::string& path, const std::string& body = {}) {
    response = Response{};
    const Request request{method, path, body,
        [&](const char* name, std::string& value) {
          const auto found = query.find(name);
          if (found == query.end()) return false;
          value = found->second; return true;
        }, [&](const UploadTargetFn& target) {
          const auto result = target("tone.mp3");
          uploaded = result.path; return result.result;
        }, etag};
    return routeFiles(request, response, files);
  }
};

void test_listing_defaults_and_invalid_directory_before_io() {
  Fixture f;
  f.files.contents["/ICONS/sun.gif"] = "GIF";
  TEST_ASSERT_TRUE(f.route("GET", "/api/v1/files"));
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  TEST_ASSERT_TRUE(f.response.streaming && f.response.finished);
  TEST_ASSERT_TRUE(isWellFormed(f.response.result.body));
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "sun.gif"));
  TEST_ASSERT_EQUAL_STRING("list:/ICONS", f.files.calls.front().c_str());
  f.query["dir"] = ""; f.files.calls.clear();
  f.route("GET", "/api/v1/files");
  TEST_ASSERT_EQUAL_STRING("list:/", f.files.calls.front().c_str());
  for (const char* path : {"/../secret", "a\\b", "a:b", "a\nb"}) {
    f.query["dir"] = path; f.files.calls.clear();
    f.route("GET", "/api/v1/files");
    TEST_ASSERT_EQUAL_INT(400, f.response.result.status);
    TEST_ASSERT_TRUE(f.files.calls.empty());
  }
}

void test_asset_get_cache_and_backup_paths() {
  Fixture f;
  f.files.contents["/ICONS/sun.gif"] = "GIF";
  f.route("GET", "/ICONS/sun.gif");
  TEST_ASSERT_EQUAL_STRING("/ICONS/sun.gif", f.response.served.c_str());
  TEST_ASSERT_EQUAL_STRING("image/gif", f.response.result.contentType);
  TEST_ASSERT_EQUAL_STRING("no-cache", f.response.headers["Cache-Control"].c_str());
  f.etag = f.response.headers["ETag"];
  f.route("GET", "/ICONS/sun.gif");
  TEST_ASSERT_EQUAL_INT(304, f.response.result.status);
  TEST_ASSERT_TRUE(f.response.served.empty());
  f.route("GET", "/ICONS/missing.gif");
  TEST_ASSERT_EQUAL_INT(404, f.response.result.status);
  f.files.contents["/SCRIPTS/game.be"] = "source"; f.etag.clear();
  f.route("GET", "/SCRIPTS/game.be");
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/game.be", f.response.served.c_str());
  TEST_ASSERT_FALSE(f.route("GET", "/config/system.json"));
}

void test_script_listing_filters_entries_and_uses_hash_read_size() {
  Fixture f;
  f.files.contents["/SCRIPTS/game/lap.mp3"] = "bytes";
  f.files.contents["/SCRIPTS/game/broken.mp3"] = "unreadable";
  f.files.contents["/SCRIPTS/game/readme.txt"] = "text";
  f.files.contents["/SCRIPTS/game/nested/no.mp3"] = "bytes";
  f.files.unreadable.insert("/SCRIPTS/game/broken.mp3");
  f.route("GET", "/api/v1/apps/script/game/sounds");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "\"size\":42"));
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "\"sha256\":\"digest\""));
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "lap.mp3"));
  TEST_ASSERT_NULL(strstr(f.response.result.body.c_str(), "broken.mp3"));
  TEST_ASSERT_NULL(strstr(f.response.result.body.c_str(), "readme.txt"));
  TEST_ASSERT_NULL(strstr(f.response.result.body.c_str(), "no.mp3"));
  TEST_ASSERT_TRUE(isWellFormed(f.response.result.body));
}

void test_orphan_sounds_remain_readable_but_cannot_be_uploaded() {
  Fixture f;
  f.files.contents["/SCRIPTS/gone/lap.mp3"] = "bytes";
  f.route("GET", "/api/v1/apps/script/gone/sounds");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  f.route("POST", "/api/v1/apps/script/gone/sounds");
  TEST_ASSERT_EQUAL_INT(404, f.response.result.status);
  f.route("GET", "/api/v1/apps/script/unknown/sounds");
  TEST_ASSERT_EQUAL_INT(404, f.response.result.status);
  f.files.calls.clear();
  f.route("GET", "/api/v1/apps/script/../sounds");
  TEST_ASSERT_EQUAL_INT(400, f.response.result.status);
  TEST_ASSERT_TRUE(f.files.calls.empty());
  f.route("GET", "/api/v1/audio/mp3");
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "\"orphan\":true"));
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "gone"));
}

void test_melody_create_replace_list_and_storage_failure() {
  Fixture f;
  const char* body = "{\"rtttl\":\"old:d=4,o=5,b=120:c\"}";
  f.route("PUT", "/api/v1/audio/melodies/bell", body);
  TEST_ASSERT_EQUAL_INT(201, f.response.result.status);
  f.route("PUT", "/api/v1/audio/melodies/bell", body);
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  f.route("GET", "/api/v1/audio/melodies");
  TEST_ASSERT_TRUE(isWellFormed(f.response.result.body));
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "bell:d=4,o=5,b=120:c"));
  f.files.writable = false;
  f.route("PUT", "/api/v1/audio/melodies/new", body);
  TEST_ASSERT_EQUAL_INT(507, f.response.result.status);
  TEST_ASSERT_EQUAL_UINT(2, f.files.notifications);
  f.route("DELETE", "/api/v1/audio/melodies/bell");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  TEST_ASSERT_FALSE(f.files.exists("/MELODIES/bell.txt"));
}

void test_deletion_releases_active_sound_before_removing_it() {
  Fixture f;
  f.files.contents["/MP3/tone.mp3"] = "bytes";
  f.route("DELETE", "/api/v1/audio/mp3/tone");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  TEST_ASSERT_EQUAL_STRING("release:/MP3/tone.mp3", f.files.calls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("remove:/MP3/tone.mp3", f.files.calls[1].c_str());
  f.files.contents["/SCRIPTS/game/tone.mp3"] = "bytes";
  f.route("DELETE", "/api/v1/apps/script/game/sounds");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  TEST_ASSERT_FALSE(f.files.exists("/SCRIPTS/game/tone.mp3"));
}

void test_icon_rename_and_metadata_failure_rollback() {
  Fixture f;
  f.files.contents["/ICONS/sun.gif"] = "GIF";
  f.origins.json = iconorigins::serialize({{"sun.gif", "https://hub.example/icons/", "sun", std::string(64, 'a')}});
  f.origins.writable = false;
  f.route("POST", "/api/v1/icons/rename", "{\"from\":\"sun.gif\",\"to\":\"day.gif\"}");
  TEST_ASSERT_EQUAL_INT(500, f.response.result.status);
  TEST_ASSERT_TRUE(f.files.exists("/ICONS/sun.gif"));
  TEST_ASSERT_FALSE(f.files.exists("/ICONS/day.gif"));
  f.origins.writable = true;
  f.route("POST", "/api/v1/icons/rename", "{\"from\":\"sun.gif\",\"to\":\"day.gif\"}");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  f.route("GET", "/api/v1/icons/origins");
  TEST_ASSERT_EQUAL_STRING("no-store", f.response.headers["Cache-Control"].c_str());
  TEST_ASSERT_NOT_NULL(strstr(f.response.result.body.c_str(), "day.gif"));
}

void test_mp3_rename_keeps_names_unique() {
  Fixture f;
  f.files.contents["/MP3/ding.mp3"] = "bytes";
  f.files.contents["/MP3/bell.mp3"] = "other";
  f.files.contents["/MELODIES/chime.txt"] = "chime:d=4,o=5,b=120:c";
  f.route("POST", "/api/v1/audio/mp3/rename", "{\"from\":\"ding\",\"to\":\"bell\"}");
  TEST_ASSERT_EQUAL_INT(409, f.response.result.status);
  f.route("POST", "/api/v1/audio/mp3/rename", "{\"from\":\"ding\",\"to\":\"chime\"}");
  TEST_ASSERT_EQUAL_INT(409, f.response.result.status);
  f.route("POST", "/api/v1/audio/mp3/rename", "{\"from\":\"ding\",\"to\":\"a b\"}");
  TEST_ASSERT_EQUAL_INT(400, f.response.result.status);
  f.route("POST", "/api/v1/audio/mp3/rename", "{\"from\":\"gone\",\"to\":\"door\"}");
  TEST_ASSERT_EQUAL_INT(404, f.response.result.status);
  TEST_ASSERT_EQUAL_UINT(0, f.files.notifications);
  f.files.calls.clear();
  f.route("POST", "/api/v1/audio/mp3/rename", "{\"from\":\"ding\",\"to\":\"door\"}");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
  TEST_ASSERT_EQUAL_STRING("release:/MP3/ding.mp3", f.files.calls[0].c_str());
  TEST_ASSERT_FALSE(f.files.exists("/MP3/ding.mp3"));
  TEST_ASSERT_EQUAL_STRING("bytes", f.files.contents["/MP3/door.mp3"].c_str());
  TEST_ASSERT_EQUAL_UINT(1, f.files.notifications);
  f.files.contents["/MP3/rename.mp3"] = "bytes";
  f.route("DELETE", "/api/v1/audio/mp3/rename");
  TEST_ASSERT_EQUAL_INT(200, f.response.result.status);
}

void test_upload_targets_and_method_errors() {
  Fixture f;
  f.route("POST", "/api/v1/audio/mp3");
  TEST_ASSERT_EQUAL_STRING("/MP3/tone.mp3", f.uploaded.c_str());
  f.route("POST", "/api/v1/apps/script/game/sounds");
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/game/tone.mp3", f.uploaded.c_str());
  f.query["dir"] = "/MP3";
  f.route("POST", "/api/v1/files");
  TEST_ASSERT_EQUAL_STRING("/MP3/tone.mp3", f.uploaded.c_str());
  for (const char* route : {"/api/v1/files", "/api/v1/audio/mp3", "/api/v1/audio/melodies",
                            "/api/v1/icons/rename", "/api/v1/apps/script/game/sounds"}) {
    TEST_ASSERT_TRUE(f.route("PATCH", route));
    TEST_ASSERT_EQUAL_INT(405, f.response.result.status);
  }
  f.route("GET", "/api/v1/audio/mp3/../secret");
  TEST_ASSERT_EQUAL_INT(400, f.response.result.status);
  TEST_ASSERT_FALSE(f.route("GET", "/api/v1/version"));
}
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_listing_defaults_and_invalid_directory_before_io);
  RUN_TEST(test_asset_get_cache_and_backup_paths);
  RUN_TEST(test_script_listing_filters_entries_and_uses_hash_read_size);
  RUN_TEST(test_orphan_sounds_remain_readable_but_cannot_be_uploaded);
  RUN_TEST(test_melody_create_replace_list_and_storage_failure);
  RUN_TEST(test_deletion_releases_active_sound_before_removing_it);
  RUN_TEST(test_icon_rename_and_metadata_failure_rollback);
  RUN_TEST(test_mp3_rename_keeps_names_unique);
  RUN_TEST(test_upload_targets_and_method_errors);
  return UNITY_END();
}

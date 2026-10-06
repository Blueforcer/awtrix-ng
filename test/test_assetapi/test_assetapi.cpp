#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "core/api/AssetApi.h"
#include "core/api/AssetUploadSession.h"
#include "core/api/JsonReader.h"
#include "core/icons/IconOrigins.h"

using namespace awtrix;
using namespace awtrix::api;

void setUp() {}
void tearDown() {}

namespace {

struct Storage : iconorigins::Backend, asset::Backend {
  std::map<std::string, std::string> files;
  std::vector<std::string> calls;
  bool writable = true;
  bool originsWritable = true;
  unsigned notifications = 0;
  asset::Backend& backend = *this;
  std::string origins = iconorigins::serialize({{"sun.gif", "https://hub.example/icons/", "sun", std::string(64, 'a')}});
  bool read(std::string& out) override { out = origins; return true; }
  bool writeAtomic(const std::string& json) override {
    calls.push_back("origins");
    if (!originsWritable) return false;
    origins = json;
    return true;
  }
  bool iconExists(const std::string& name) override { return files.count("/ICONS/" + name) != 0; }


  Storage() { iconOrigins = this; }
  bool exists(const std::string& path) override {
    calls.push_back("exists:" + path);
    return files.count(path) != 0;
  }
  bool write(const std::string& path, const std::string& content) override {
    calls.push_back("write:" + path);
    if (!writable) return false;
    files[path] = content;
    return true;
  }
  bool remove(const std::string& path) override {
    calls.push_back("remove:" + path);
    return files.erase(path) != 0;
  }
  bool rename(const std::string& from, const std::string& to) override {
    if (!files.count(from)) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
  void release(const std::string&) override {}
  void changed() override { ++notifications; }

};

HttpResult upload(const asset::UploadTarget& target, const std::string& content,
                  asset::Backend& backend) {
  asset::StreamStorage storage;
  storage.exists = [&](const std::string& path) { return backend.exists(path); };
  storage.begin = [](const std::string&) { return true; };
  storage.write = [](const uint8_t*, std::size_t) { return true; };
  storage.publish = [&](const std::string& path) { return backend.write(path, content); };
  storage.changed = [&] { backend.changed(); };
  asset::UploadSession session;
  session.reset(std::move(storage));
  session.authorize({});
  session.start(target);
  session.append(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  session.end();
  return session.complete();
}

HttpResult upload(const std::string& dir, const std::string& filename, const std::string& content,
                  asset::Backend& backend) {
  return upload(asset::prepareUpload(dir, filename), content, backend);
}

bool validChunks(const std::string& path, const std::string& value, std::size_t split) {
  asset::UploadValidator validator(path);
  const auto* data = reinterpret_cast<const uint8_t*>(value.data());
  if (!validator.append(data, split)) return false;
  if (!validator.append(data + split, value.size() - split)) return false;
  return validator.finish();
}

void assertEverySplit(const std::string& path, const std::string& value, bool expected) {
  for (std::size_t split = 0; split <= value.size(); ++split) {
    const std::string context = path + " split=" + std::to_string(split);
    TEST_ASSERT_EQUAL_MESSAGE(expected, validChunks(path, value, split), context.c_str());
  }
  asset::UploadValidator validator(path);
  bool accepted = true;
  for (unsigned char c : value) accepted = validator.append(&c, 1) && accepted;
  TEST_ASSERT_EQUAL(expected, accepted && validator.finish());
}

void test_melody_create_replace_and_retitle() {
  Storage storage;
  const std::string body = "{\"rtttl\":\"old:d=4,o=5,b=120:c,e\"}";
  TEST_ASSERT_EQUAL_INT(201, asset::writeMelody("bell", body, storage.backend).status);
  TEST_ASSERT_EQUAL_STRING("bell:d=4,o=5,b=120:c,e", storage.files["/MELODIES/bell.txt"].c_str());
  TEST_ASSERT_EQUAL_INT(200, asset::writeMelody("bell", body, storage.backend).status);
  TEST_ASSERT_EQUAL_UINT(2, storage.notifications);
}

void test_melody_validation_precedes_io() {
  Storage storage;
  TEST_ASSERT_EQUAL_INT(422, asset::writeMelody("../wrong", "{}", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(400, asset::writeMelody("bell", "invalid", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(422, asset::writeMelody("bell", "{\"rtttl\":\"invalid\"}", storage.backend).status);
  TEST_ASSERT_TRUE(storage.calls.empty());
  TEST_ASSERT_EQUAL_UINT(0, storage.notifications);
}

void test_melody_storage_failure_is_not_announced() {
  Storage storage;
  storage.writable = false;
  const HttpResult result = asset::writeMelody("bell", "{\"rtttl\":\"d=4:c\"}", storage.backend);
  TEST_ASSERT_EQUAL_INT(507, result.status);
  TEST_ASSERT_TRUE(storage.files.empty());
  TEST_ASSERT_EQUAL_UINT(0, storage.notifications);
}

void test_melody_delete_never_strips_attacker_path() {
  Storage storage;
  storage.files["/MELODIES/bell.txt"] = "bell:d=4:c";
  for (const char* name : {"../bell", "nested/bell", "/bell", "..", "", "bad name"})
    TEST_ASSERT_EQUAL_INT(404, asset::deleteMelody(name, storage.backend).status);
  TEST_ASSERT_TRUE(storage.calls.empty());
  TEST_ASSERT_EQUAL_INT(200, asset::deleteMelody("bell", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(404, asset::deleteMelody("bell", storage.backend).status);
  TEST_ASSERT_EQUAL_UINT(1, storage.notifications);
}

void test_file_delete_clears_origin_before_bytes() {
  Storage storage;
  storage.files["/ICONS/sun.gif"] = "GIF8";
  TEST_ASSERT_EQUAL_INT(200, asset::deleteFile("/ICONS/sun.gif", storage.backend).status);
  TEST_ASSERT_EQUAL_UINT(2, storage.calls.size());
  TEST_ASSERT_EQUAL_STRING("origins", storage.calls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("remove:/ICONS/sun.gif", storage.calls[1].c_str());
  TEST_ASSERT_EQUAL_UINT(1, storage.notifications);
}

void test_file_delete_propagates_origin_failure_without_deleting() {
  Storage storage;
  storage.files["/ICONS/sun.gif"] = "GIF8";
  storage.originsWritable = false;
  const HttpResult result = asset::deleteFile("/ICONS/sun.gif", storage.backend);
  TEST_ASSERT_EQUAL_INT(500, result.status);
  TEST_ASSERT_TRUE(api::isWellFormed(result.body));
  TEST_ASSERT_EQUAL_UINT(1, storage.calls.size());
  TEST_ASSERT_EQUAL_UINT(1, storage.files.size());
  TEST_ASSERT_EQUAL_UINT(0, storage.notifications);
}

void test_file_delete_rejects_ambiguous_paths_before_io() {
  Storage storage;
  const std::vector<std::string> paths = {
      "/config/system.json", "/ICONS/../config", "/ICONS/..\\config", "/ICONS/x:stream",
      std::string("/ICONS/x\0.gif", 13), "/ICONS/x\n.gif"};
  for (const auto& path : paths)
    TEST_ASSERT_EQUAL_INT(400, asset::deleteFile(path, storage.backend).status);
  TEST_ASSERT_TRUE(storage.calls.empty());
}

void test_mp3_delete_validates_names_and_existence() {
  Storage storage;
  storage.files["/MP3/beep.mp3"] = "ID3";
  TEST_ASSERT_EQUAL_INT(400, asset::deleteMp3("nested/beep", storage.backend).status);
  TEST_ASSERT_TRUE(storage.calls.empty());
  TEST_ASSERT_EQUAL_INT(200, asset::deleteMp3("beep", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(404, asset::deleteMp3("beep", storage.backend).status);
  TEST_ASSERT_EQUAL_UINT(1, storage.notifications);
}

void test_directory_normalization_rejects_traversal() {
  std::string normalized;
  TEST_ASSERT_EQUAL_INT(200, asset::listDirectory("ICONS", normalized).status);
  TEST_ASSERT_EQUAL_STRING("/ICONS", normalized.c_str());
  TEST_ASSERT_EQUAL_INT(200, asset::listDirectory("", normalized).status);
  TEST_ASSERT_EQUAL_STRING("/", normalized.c_str());
  for (const char* path : {"/ICONS/../config", "ICONS\\x", "ICONS:x"}) {
    TEST_ASSERT_EQUAL_INT(400, asset::listDirectory(path, normalized).status);
    TEST_ASSERT_TRUE(normalized.empty());
  }
}

void test_upload_target_validates_flat_mp3_names_before_io() {
  const auto target = asset::prepareUpload("ICONS", "sun.gif");
  TEST_ASSERT_TRUE(target.ok());
  TEST_ASSERT_EQUAL_STRING("/ICONS/sun.gif", target.path.c_str());
  TEST_ASSERT_EQUAL_STRING("/ICONS/sun.gif", asset::prepareUpload("/ICONS/", "sun.gif").path.c_str());
  TEST_ASSERT_TRUE(asset::prepareUpload("/ICONS", "/MP3/beep.mp3").ok());
  TEST_ASSERT_TRUE(asset::prepareUpload("/MP3", "a-b_1.mp3").ok());
  for (const char* filename : {"beep.txt", "bad name.mp3", "nested/beep.mp3", "../beep.mp3"})
    TEST_ASSERT_FALSE_MESSAGE(asset::prepareUpload("/MP3", filename).ok(), filename);
  TEST_ASSERT_FALSE(asset::prepareUpload("/ICONS", "").ok());
  TEST_ASSERT_FALSE(asset::prepareUpload("/ICONS", "../config").ok());
  TEST_ASSERT_FALSE(asset::prepareUpload("/ICONS", "x:stream").ok());
}

void test_upload_validator_accepts_every_chunk_boundary() {
  assertEverySplit("/ICONS/sun.gif", "GIF89a body", true);
  assertEverySplit("/ICONS/sun.jpg", std::string("\xff\xd8\xff", 3), true);
  assertEverySplit("/MP3/beep.mp3", "ID3tagbody", true);
  assertEverySplit("/MP3/beep.mp3", std::string("\xff\xe0", 2), true);
  assertEverySplit("/MELODIES/bell.txt", "bell:d=4,o=5,b=120:c,e,g", true);
  assertEverySplit("/PALETTES/fire.txt", "FF0000\n00FF00\r\n0000FF\t", true);
  assertEverySplit("/PALETTES/fire.txt", "#FF0000@0\n#0000FF@100", true);
}

void test_upload_validator_rejects_invalid_final_content_for_every_split() {
  assertEverySplit("/ICONS/sun.gif", "GIF", false);
  assertEverySplit("/ICONS/sun.gif", "ID3", false);
  assertEverySplit("/MP3/beep.mp3", "ID", false);
  assertEverySplit("/MELODIES/bell.txt", "bell:d=4:c,h", false);
  assertEverySplit("/PALETTES/fire.txt", std::string("FF0000\n00FF00\x01", 14), false);
  assertEverySplit("/PALETTES/fire.txt", "GIF89a", false);
  assertEverySplit("/PALETTES/fire.txt", std::string("\x89PNG", 4), false);
  assertEverySplit("/PALETTES/fire.txt", "hello world", false);
  assertEverySplit("/PALETTES/fire.txt", "FF0000@0\n0000FF\n", false);
  assertEverySplit("/unknown/x", "GIF8", false);
}

void test_upload_validator_rejects_empty_and_bounds_melodies() {
  asset::UploadValidator validator("/MELODIES/bell.txt");
  TEST_ASSERT_FALSE(validator.finish());
  TEST_ASSERT_TRUE(validator.append(nullptr, 0));
  TEST_ASSERT_FALSE(validator.finish());
  const std::string tooLong(rtttl::kMaxLength + 1, 'c');
  TEST_ASSERT_FALSE(validator.append(reinterpret_cast<const uint8_t*>(tooLong.data()), tooLong.size()));
  TEST_ASSERT_FALSE(validator.finish());
  validator.reset("/PALETTES/fire.txt");
  const std::string padded = "FF0000" + std::string(render::kMaxPaletteFileBytes - 6, '\n');
  TEST_ASSERT_TRUE(validator.append(reinterpret_cast<const uint8_t*>(padded.data()), padded.size()));
  TEST_ASSERT_TRUE(validator.finish());
  TEST_ASSERT_FALSE(validator.append(reinterpret_cast<const uint8_t*>("\n"), 1));
  TEST_ASSERT_FALSE(validator.finish());
  validator.reset("/ICONS/sun.gif");
  TEST_ASSERT_FALSE(validator.append(nullptr, 4));
  TEST_ASSERT_FALSE(validator.finish());
  validator.reset("/ICONS/sun.gif");
  TEST_ASSERT_TRUE(validator.append(reinterpret_cast<const uint8_t*>("GIF8"), 4));
  TEST_ASSERT_TRUE(validator.finish());
}

void test_upload_file_rejects_before_writing_and_notifies_only_success() {
  Storage storage;
  TEST_ASSERT_EQUAL_INT(400, upload("/MP3", "bad name.mp3", "ID3", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(415, upload("/ICONS", "x.gif", "ID3", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(415, upload("/ICONS", "x.gif", "", storage.backend).status);
  TEST_ASSERT_TRUE(storage.calls.empty());
  TEST_ASSERT_EQUAL_INT(200, upload("/ICONS", "x.gif", "GIF8", storage.backend).status);
  TEST_ASSERT_EQUAL_STRING("GIF8", storage.files["/ICONS/x.gif"].c_str());
  TEST_ASSERT_EQUAL_UINT(1, storage.notifications);
  storage.writable = false;
  TEST_ASSERT_EQUAL_INT(507, upload("/ICONS", "x.gif", "GIF8new", storage.backend).status);
  TEST_ASSERT_EQUAL_UINT(1, storage.notifications);
}

void test_mp3s_and_melodies_name_each_other() {
  TEST_ASSERT_EQUAL_STRING("/MELODIES/ding.txt", sound::namesakePath("/MP3/ding.mp3").c_str());
  TEST_ASSERT_EQUAL_STRING("/MP3/ding.mp3", sound::namesakePath("/MELODIES/ding.txt").c_str());
  for (const char* path : {"/SCRIPTS/Racer/boost.mp3", "/ICONS/ding.gif", "/MP3/bad name.mp3",
                           "/MP3/ding.txt", "/MELODIES/ding.mp3", "/MELODIES/.txt", "/MP3/a/b.mp3",
                           "/PALETTES/ding.txt"})
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", sound::namesakePath(path).c_str(), path);
}

void test_melody_cannot_take_an_mp3_name() {
  Storage storage;
  storage.files["/MP3/ding.mp3"] = "ID3";
  const HttpResult result =
      asset::writeMelody("ding", "{\"rtttl\":\"d=4:c\"}", storage.backend);
  TEST_ASSERT_EQUAL_INT(409, result.status);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, result.body.find("\"nameTaken\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, result.body.find("\"name taken\""));
  TEST_ASSERT_EQUAL_UINT(0, storage.files.count("/MELODIES/ding.txt"));
  TEST_ASSERT_EQUAL_UINT(0, storage.notifications);
  TEST_ASSERT_EQUAL_INT(201, asset::writeMelody("bell", "{\"rtttl\":\"d=4:c\"}", storage.backend).status);
}

void test_upload_cannot_take_the_other_kinds_name() {
  Storage storage;
  storage.files["/MELODIES/ding.txt"] = "ding:d=4:c";
  storage.files["/MP3/bell.mp3"] = "ID3";
  TEST_ASSERT_EQUAL_INT(409, upload("/MP3", "ding.mp3", "ID3", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(409, upload("/ICONS", "/MP3/ding.mp3", "ID3", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(409,
      upload("/MELODIES", "bell.txt", "bell:d=4:c", storage.backend).status);
  TEST_ASSERT_EQUAL_UINT(0, storage.files.count("/MP3/ding.mp3"));
  TEST_ASSERT_EQUAL_UINT(0, storage.notifications);
  TEST_ASSERT_EQUAL_INT(200, upload("/MP3", "bell.mp3", "ID3new", storage.backend).status);
  TEST_ASSERT_EQUAL_INT(200, upload("/MP3", "other.mp3", "ID3", storage.backend).status);
}

void test_script_sound_may_share_a_melody_name() {
  Storage storage;
  storage.files["/MELODIES/boost.txt"] = "boost:d=4:c";
  asset::UploadTarget target;
  target.path = "/SCRIPTS/Racer/boost.mp3";
  target.result.status = 200;
  TEST_ASSERT_EQUAL_INT(200, upload(target, "ID3", storage.backend).status);
  TEST_ASSERT_EQUAL_STRING("ID3", storage.files["/SCRIPTS/Racer/boost.mp3"].c_str());
}

void test_listing_streams_each_entry_with_escaping_and_full_size() {
  std::vector<std::string> chunks;
  asset::JsonListing listing(asset::JsonListing::Kind::Files,
                             [&](const std::string& chunk) { chunks.push_back(chunk); });
  listing.begin();
  listing.file("one\".gif", 4294967297ULL);
  listing.file("two.gif", 4);
  listing.end(4294967301ULL, 8589934592ULL);
  TEST_ASSERT_EQUAL_UINT(4, chunks.size());
  std::string json;
  for (const auto& chunk : chunks) json += chunk;
  TEST_ASSERT_TRUE(isWellFormed(json));
  TEST_ASSERT_EQUAL_STRING(
      "{\"files\":[{\"name\":\"one\\\".gif\",\"size\":4294967297},{\"name\":\"two.gif\",\"size\":4}],"
      "\"usedBytes\":4294967301,\"totalBytes\":8589934592}", json.c_str());
}

void test_melody_listing_skips_invalid_filename_but_reports_invalid_content() {
  std::vector<std::string> chunks;
  asset::JsonListing listing(asset::JsonListing::Kind::Melodies,
                             [&](const std::string& chunk) { chunks.push_back(chunk); });
  listing.begin();
  listing.melody("/MELODIES/ignored.gif", "GIF8", 4);
  listing.melody("/MELODIES/broken.txt", "bad", 3);
  listing.melody("bell.txt", "bell:d=4:c", 10);
  listing.end(13, 100);
  TEST_ASSERT_EQUAL_UINT(4, chunks.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, chunks[1].find("\"valid\":false"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, chunks[2].find("\"valid\":true"));
  TEST_ASSERT_EQUAL_CHAR(',', chunks[2][0]);
  std::string json;
  for (const auto& chunk : chunks) json += chunk;
  TEST_ASSERT_TRUE(isWellFormed(json));
}

void test_empty_listing_and_reuse_are_valid_json() {
  std::string json;
  asset::JsonListing listing(asset::JsonListing::Kind::Files,
                             [&](const std::string& chunk) { json += chunk; });
  listing.begin();
  listing.end(0, 100);
  TEST_ASSERT_EQUAL_STRING("{\"files\":[],\"usedBytes\":0,\"totalBytes\":100}", json.c_str());
  json.clear();
  listing.begin();
  listing.file("x", 1);
  listing.end(1, 100);
  TEST_ASSERT_TRUE(isWellFormed(json));
}

}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_melody_create_replace_and_retitle);
  RUN_TEST(test_melody_validation_precedes_io);
  RUN_TEST(test_melody_storage_failure_is_not_announced);
  RUN_TEST(test_melody_delete_never_strips_attacker_path);
  RUN_TEST(test_file_delete_clears_origin_before_bytes);
  RUN_TEST(test_file_delete_propagates_origin_failure_without_deleting);
  RUN_TEST(test_file_delete_rejects_ambiguous_paths_before_io);
  RUN_TEST(test_mp3_delete_validates_names_and_existence);
  RUN_TEST(test_directory_normalization_rejects_traversal);
  RUN_TEST(test_upload_target_validates_flat_mp3_names_before_io);
  RUN_TEST(test_upload_validator_accepts_every_chunk_boundary);
  RUN_TEST(test_upload_validator_rejects_invalid_final_content_for_every_split);
  RUN_TEST(test_upload_validator_rejects_empty_and_bounds_melodies);
  RUN_TEST(test_upload_file_rejects_before_writing_and_notifies_only_success);
  RUN_TEST(test_mp3s_and_melodies_name_each_other);
  RUN_TEST(test_melody_cannot_take_an_mp3_name);
  RUN_TEST(test_upload_cannot_take_the_other_kinds_name);
  RUN_TEST(test_script_sound_may_share_a_melody_name);
  RUN_TEST(test_listing_streams_each_entry_with_escaping_and_full_size);
  RUN_TEST(test_melody_listing_skips_invalid_filename_but_reports_invalid_content);
  RUN_TEST(test_empty_listing_and_reuse_are_valid_json);
  return UNITY_END();
}

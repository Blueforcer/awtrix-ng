#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "backup_fixture.h"
#include "core/backup/RestoreApplier.h"
#include "core/backup/ZipReader.h"

using namespace awtrix;

namespace {

struct MockSink : backup::RestoreSink {
  std::string wifiSsid, wifiPass, systemJson, settingsJson, appLoopJson;
  std::string radioJson;
  bool committed = false;
  struct File {
    std::string path;
    std::string data;
    bool ended = false;
    bool aborted = false;
  };
  std::vector<File> files;
  bool declareNextBeginFails = false;

  bool applyWifi(const std::string& ssid, const std::string& pass, std::string&) override {
    wifiSsid = ssid;
    wifiPass = pass;
    return true;
  }
  bool applySystem(const std::string& json, std::string&) override {
    systemJson = json;
    return true;
  }
  bool applySettings(const std::string& json, std::string&) override {
    settingsJson = json;
    return true;
  }
  bool applyAppLoop(const std::string& json, std::string&) override {
    appLoopJson = json;
    return true;
  }
  bool applyRadioStations(const std::string& json, std::string&) override {
    radioJson = json;
    return true;
  }
  void commit() override { committed = true; }
  bool beginFile(const std::string& path, std::string&) override {
    if (declareNextBeginFails) {
      declareNextBeginFails = false;
      return false;
    }
    files.push_back(File{path, "", false, false});
    return true;
  }
  bool writeFile(const uint8_t* data, std::size_t n) override {
    files.back().data.append(reinterpret_cast<const char*>(data), n);
    return true;
  }
  bool endFile() override {
    files.back().ended = true;
    return true;
  }
  void abortFile() override {
    if (!files.empty()) files.back().aborted = true;
  }

  const File* find(const std::string& path) const {
    for (const auto& f : files)
      if (f.path == path) return &f;
    return nullptr;
  }
};

backup::RestoreResult run(const unsigned char* zip, unsigned len, MockSink& sink) {
  backup::RestoreApplier applier(sink);
  backup::ZipReader reader(applier);
  reader.feed(zip, len);
  reader.finish();
  return applier.result();
}

void test_routes_every_category() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kBackup, awtrix_test::kBackup_len, sink);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("", r.error.c_str());

  TEST_ASSERT_EQUAL_STRING("HomeNet", sink.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("s3cr3t", sink.wifiPass.c_str());
  TEST_ASSERT_TRUE(sink.systemJson.find("awtrix-lab") != std::string::npos);
  TEST_ASSERT_TRUE(sink.settingsJson.find("brightness") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("[\"clock\",\"weather\"]", sink.appLoopJson.c_str());
  TEST_ASSERT_TRUE(sink.committed);

  TEST_ASSERT_EQUAL_INT(1, r.wifi);
  TEST_ASSERT_EQUAL_INT(1, r.system);
  TEST_ASSERT_EQUAL_INT(1, r.settings);
  TEST_ASSERT_EQUAL_INT(1, r.appLoop);
  TEST_ASSERT_EQUAL_INT(1, r.icons);
  TEST_ASSERT_EQUAL_INT(1, r.melodies);
  TEST_ASSERT_EQUAL_INT(1, r.palettes);
  TEST_ASSERT_EQUAL_INT(2, r.scripts);
}

void test_asset_files_written_to_absolute_paths() {
  MockSink sink;
  run(awtrix_test::kBackup, awtrix_test::kBackup_len, sink);

  const MockSink::File* icon = sink.find("/ICONS/smile.gif");
  TEST_ASSERT_NOT_NULL(icon);
  TEST_ASSERT_TRUE(icon->ended);
  TEST_ASSERT_FALSE(icon->aborted);
  TEST_ASSERT_EQUAL_HEX8('G', icon->data[0]);

  TEST_ASSERT_NOT_NULL(sink.find("/MELODIES/bell.txt"));
  TEST_ASSERT_NOT_NULL(sink.find("/PALETTES/fire.txt"));
  TEST_ASSERT_NOT_NULL(sink.find("/SCRIPTS/weather.ax"));
  TEST_ASSERT_NOT_NULL(sink.find("/SCRIPTS/weather.store.json"));
}

void test_rejects_foreign_backup_without_touching_anything() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kForeignBackup, awtrix_test::kForeignBackup_len, sink);

  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_TRUE(r.error.size() > 0);
  TEST_ASSERT_EQUAL_STRING("", sink.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_INT(0, r.wifi);
  TEST_ASSERT_FALSE(sink.committed);
}

void test_rejects_path_traversal_entry() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kEvilBackup, awtrix_test::kEvilBackup_len, sink);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_UINT(0, sink.files.size());
  TEST_ASSERT_EQUAL_INT(0, r.icons);
  TEST_ASSERT_TRUE(r.warnings.size() >= 1);
}

void test_sounds_restore_and_content_sniff() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kSoundsBackup, awtrix_test::kSoundsBackup_len,
                                      sink);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(1, r.sounds);

  const MockSink::File* mp3 = sink.find("/SOUNDS/beep.mp3");
  TEST_ASSERT_NOT_NULL(mp3);
  TEST_ASSERT_TRUE(mp3->ended);
  TEST_ASSERT_EQUAL_HEX8('I', mp3->data[0]);

  // Text smuggled into SOUNDS/ fails the sniff and only leaves a warning behind.
  const MockSink::File* txt = sink.find("/SOUNDS/readme.txt");
  TEST_ASSERT_NOT_NULL(txt);
  TEST_ASSERT_TRUE(txt->aborted);
  TEST_ASSERT_FALSE(txt->ended);
  TEST_ASSERT_TRUE(r.warnings.size() >= 1);

  TEST_ASSERT_TRUE(r.toJson().find("\"sounds\":1") != std::string::npos);
}

void test_result_json_reports_counts() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kBackup, awtrix_test::kBackup_len, sink);
  const std::string json = r.toJson();
  TEST_ASSERT_TRUE(json.find("\"ok\":true") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"icons\":1") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"scripts\":2") != std::string::npos);
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_routes_every_category);
  RUN_TEST(test_asset_files_written_to_absolute_paths);
  RUN_TEST(test_rejects_foreign_backup_without_touching_anything);
  RUN_TEST(test_rejects_path_traversal_entry);
  RUN_TEST(test_sounds_restore_and_content_sniff);
  RUN_TEST(test_result_json_reports_counts);
  UNITY_END();
  return 0;
}

#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "backup_fixture.h"
#include "core/Settings.h"
#include "core/api/RestoreApi.h"
#include "core/backup/RestoreApplier.h"
#include "core/backup/ZipReader.h"
#include "core/icons/IconOrigins.h"

using namespace awtrix;

namespace {

struct MockSink : backup::RestoreSink {
  std::string wifiSsid, wifiPass, systemJson, settingsJson, appLoopJson;
  std::string radioJson, originsJson;
  bool originsAfterFiles = false;
  bool committed = false;
  struct File {
    std::string path;
    std::string data;
    bool ended = false;
    bool aborted = false;
  };
  std::vector<File> files;
  bool declareNextBeginFails = false;
  bool declareEndFails = false;
  unsigned aborts = 0;
  unsigned commits = 0;

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
  bool applyIconOrigins(const std::string& json, std::string&) override {
    originsJson = json;
    originsAfterFiles = !files.empty() && files.back().ended;
    return true;
  }
  void commit() override { committed = true; ++commits; }
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
    if (declareEndFails) return false;
    files.back().ended = true;
    return true;
  }
  void abortFile() override {
    ++aborts;
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
  return api::finishRestore(reader, applier);
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

void test_mp3_restore_and_content_sniff() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kSoundsBackup, awtrix_test::kSoundsBackup_len,
                                      sink);

  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(1, r.mp3);

  const MockSink::File* mp3 = sink.find("/MP3/beep.mp3");
  TEST_ASSERT_NOT_NULL(mp3);
  TEST_ASSERT_TRUE(mp3->ended);
  TEST_ASSERT_EQUAL_HEX8('I', mp3->data[0]);

  // Text smuggled into MP3/ fails the sniff and only leaves a warning behind.
  const MockSink::File* txt = sink.find("/MP3/readme.txt");
  TEST_ASSERT_NOT_NULL(txt);
  TEST_ASSERT_TRUE(txt->aborted);
  TEST_ASSERT_FALSE(txt->ended);
  TEST_ASSERT_TRUE(r.warnings.size() >= 1);

  TEST_ASSERT_TRUE(r.toJson().find("\"mp3\":1") != std::string::npos);
}

void entry(backup::RestoreApplier& applier, const std::string& name,
           const std::string& data, bool crcOk = true) {
  applier.onEntryStart(name, static_cast<uint32_t>(data.size()));
  applier.onEntryData(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  applier.onEntryEnd(crcOk);
}

void test_settings_backups_migrate_old_bars_and_preserve_separate_new_bars() {
  for (bool separate : {false, true}) {
    MockSink sink;
    backup::RestoreApplier applier(sink);
    entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
    const std::string settings = separate
        ? R"({"clockFace":"ring","weekdayBar":{"show":false,"activeColor":"#102030"},"dateWeekdayBar":{"show":true,"activeColor":"#405060"},"removedOldField":1})"
        : R"({"clockFace":"ring","weekdayBar":{"show":false,"activeColor":"#102030"},"removedOldField":1})";
    entry(applier, "config/settings.json", settings);
    applier.onArchiveEnd();
    TEST_ASSERT_TRUE(applier.result().ok);
    TEST_ASSERT_EQUAL_INT(1, applier.result().settings);
    TEST_ASSERT_EQUAL_STRING(settings.c_str(), sink.settingsJson.c_str());
    SettingsError err;
    TEST_ASSERT_TRUE_MESSAGE(Settings::validateRead(api::JsonReader(sink.settingsJson), err,
                                                   Settings::UnknownKeys::Skip), err.field.c_str());
    // The filesystem sink restores into a fresh Settings through applyStored, so fields removed
    // since the backup are ignored while the presence of dateWeekdayBar controls migration.
    Settings restored;
    restored.applyStored(api::JsonReader(sink.settingsJson));
    TEST_ASSERT_FALSE(restored.weekdayBar.show);
    TEST_ASSERT_EQUAL_HEX32(0x102030u, restored.weekdayBar.activeColor);
    TEST_ASSERT_EQUAL_INT(kClockFaceRing, restored.clockFace);
    TEST_ASSERT_EQUAL(separate, restored.dateWeekdayBar.show);
    TEST_ASSERT_EQUAL_HEX32(separate ? 0x405060u : 0x102030u,
                            restored.dateWeekdayBar.activeColor);
  }
}

void test_an_empty_settings_backup_restores_both_default_weekday_bars() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
  entry(applier, "config/settings.json", "{}");
  applier.onArchiveEnd();
  TEST_ASSERT_TRUE(applier.result().ok);
  Settings restored;
  restored.applyStored(api::JsonReader(sink.settingsJson));
  TEST_ASSERT_TRUE(restored.weekdayBar.show);
  TEST_ASSERT_TRUE(restored.dateWeekdayBar.show);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, restored.weekdayBar.activeColor);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, restored.dateWeekdayBar.activeColor);
  TEST_ASSERT_EQUAL_HEX32(0x666666u, restored.weekdayBar.inactiveColor);
  TEST_ASSERT_EQUAL_HEX32(0x666666u, restored.dateWeekdayBar.inactiveColor);
}

void test_origins_restore_runs_after_all_files_regardless_of_zip_order() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
  const auto json = iconorigins::serialize({{"mail.gif", "https://hub.example/icons/", "mail",
                                           std::string(64, 'a')}});
  entry(applier, "config/icon-origins.json", json);
  TEST_ASSERT_TRUE(sink.originsJson.empty());
  entry(applier, "ICONS/mail.gif", "GIF89a");
  applier.onArchiveEnd();
  TEST_ASSERT_TRUE(applier.result().ok);
  TEST_ASSERT_TRUE(sink.originsAfterFiles);
  TEST_ASSERT_EQUAL_STRING(json.c_str(), sink.originsJson.c_str());
  TEST_ASSERT_EQUAL_INT(1, applier.result().iconOrigins);
}

void test_invalid_or_corrupt_origins_do_not_replace_existing_metadata() {
  for (int mode = 0; mode < 3; ++mode) {
    MockSink sink;
    backup::RestoreApplier applier(sink);
    entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
    const std::string json = mode == 0 ? "{broken" : mode == 1 ? "{\"icons\":[]}"
                                                                  : std::string(iconorigins::kMaxBytes + 1, ' ');
    entry(applier, "config/icon-origins.json", json, mode != 1);
    applier.onArchiveEnd();
    TEST_ASSERT_TRUE(applier.result().ok);
    TEST_ASSERT_TRUE(sink.originsJson.empty());
    TEST_ASSERT_EQUAL_INT(0, applier.result().iconOrigins);
    TEST_ASSERT_FALSE(applier.result().warnings.empty());
  }
}

// A script's sounds come back into its folder, checked and counted like every other MP3.
void test_script_sounds_restore_into_their_folder() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
  entry(applier, "SCRIPTS/racer.ax", "def draw() end");
  entry(applier, "SCRIPTS/racer/boost.mp3", "ID3data");
  entry(applier, "SCRIPTS/racer/lap_2.mp3", std::string("\xff\xfb\x90\x00", 4));
  applier.onArchiveEnd();

  const backup::RestoreResult& r = applier.result();
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(1, r.scripts);
  TEST_ASSERT_EQUAL_INT(2, r.mp3);
  TEST_ASSERT_EQUAL_INT(0, r.skipped);
  const MockSink::File* boost = sink.find("/SCRIPTS/racer/boost.mp3");
  TEST_ASSERT_NOT_NULL(boost);
  TEST_ASSERT_TRUE(boost->ended);
  TEST_ASSERT_EQUAL_STRING("ID3data", boost->data.c_str());
  TEST_ASSERT_NOT_NULL(sink.find("/SCRIPTS/racer/lap_2.mp3"));
}

// Nothing reaches deeper than a script's folder, and nothing but a playable name lands in it.
void test_script_sound_paths_are_held_to_the_name_rules() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
  const char* unsafe[] = {"SCRIPTS/racer/sub/boost.mp3", "SCRIPTS/racer/boost.txt",
                          "SCRIPTS/racer/bad name.mp3",  "SCRIPTS/ra.cer/boost.mp3",
                          "SCRIPTS/racer/.mp3",          "SCRIPTS/racer/../boost.mp3",
                          "SCRIPTS/../MP3/boost.mp3",    "SCRIPTS//boost.mp3"};
  for (const char* name : unsafe) entry(applier, name, "ID3");
  // A playable path whose bytes are no MP3 is refused like any other.
  entry(applier, "SCRIPTS/racer/text.mp3", "just text");
  applier.onArchiveEnd();

  const backup::RestoreResult& r = applier.result();
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(0, r.mp3);
  TEST_ASSERT_EQUAL_INT(0, r.scripts);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(sizeof(unsafe) / sizeof(unsafe[0])) + 1, r.skipped);
  TEST_ASSERT_EQUAL_size_t(1, sink.files.size());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/text.mp3", sink.files[0].path.c_str());
  TEST_ASSERT_TRUE(sink.files[0].aborted);
  TEST_ASSERT_FALSE(sink.files[0].ended);
  TEST_ASSERT_EQUAL_size_t(static_cast<std::size_t>(r.skipped), r.warnings.size());
}

void test_result_json_reports_counts() {
  MockSink sink;
  const backup::RestoreResult r = run(awtrix_test::kBackup, awtrix_test::kBackup_len, sink);
  const std::string json = r.toJson();
  TEST_ASSERT_TRUE(json.find("\"ok\":true") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"icons\":1") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"scripts\":2") != std::string::npos);
}

void test_zip_restore_is_independent_of_chunk_boundaries() {
  for (unsigned chunk : {1u, 2u, 3u, 7u, 256u}) {
    MockSink sink;
    backup::RestoreApplier applier(sink);
    backup::ZipReader reader(applier);
    for (unsigned offset = 0; offset < awtrix_test::kBackup_len; offset += chunk) {
      const unsigned remaining = awtrix_test::kBackup_len - offset;
      reader.feed(awtrix_test::kBackup + offset, remaining < chunk ? remaining : chunk);
    }
    const auto result = api::finishRestore(reader, applier);
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_INT(1, result.icons);
    TEST_ASSERT_EQUAL_INT(1, result.melodies);
    TEST_ASSERT_EQUAL_INT(1, result.palettes);
    TEST_ASSERT_EQUAL_INT(2, result.scripts);
    TEST_ASSERT_EQUAL_UINT(0, sink.aborts);
    TEST_ASSERT_EQUAL_UINT(1, sink.commits);
    TEST_ASSERT_TRUE(api::finishRestore(reader, applier).ok);
    TEST_ASSERT_EQUAL_UINT(1, sink.commits);
  }
}

std::size_t truncatedIconSize() {
  const std::string bytes(reinterpret_cast<const char*>(awtrix_test::kBackup),
                          awtrix_test::kBackup_len);
  const auto at = bytes.find("GIF8");
  TEST_ASSERT_TRUE(at != std::string::npos);
  return at + 2;
}

void test_parser_error_aborts_an_unfinished_asset_and_reports_why() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  backup::ZipReader reader(applier);
  reader.feed(awtrix_test::kBackup, truncatedIconSize());
  const auto result = api::finishRestore(reader, applier);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_TRUE(result.error.find("truncated") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT(1, sink.aborts);
  TEST_ASSERT_FALSE(sink.committed);
  TEST_ASSERT_EQUAL_INT(0, result.icons);
  TEST_ASSERT_FALSE(api::restoreChangedAssets(result));
  TEST_ASSERT_EQUAL_INT(400, api::restoreResponse(result, true).status);
  const auto repeated = api::finishRestore(reader, applier);
  TEST_ASSERT_EQUAL_STRING(result.error.c_str(), repeated.error.c_str());
  TEST_ASSERT_EQUAL_UINT(1, sink.aborts);
}

void test_explicit_abort_and_destructor_release_pending_files() {
  for (bool explicitAbort : {false, true}) {
    MockSink sink;
    {
      backup::RestoreApplier applier(sink);
      backup::ZipReader reader(applier);
      reader.feed(awtrix_test::kBackup, truncatedIconSize());
      if (explicitAbort) {
        applier.abort("backup upload aborted");
        TEST_ASSERT_FALSE(applier.result().ok);
        TEST_ASSERT_FALSE(applier.result().error.empty());
        reader.feed(awtrix_test::kBackup + truncatedIconSize(),
                    awtrix_test::kBackup_len - truncatedIconSize());
        TEST_ASSERT_FALSE(api::finishRestore(reader, applier).ok);
      }
    }
    TEST_ASSERT_EQUAL_UINT(1, sink.aborts);
    TEST_ASSERT_FALSE(sink.committed);
  }
}

void test_restore_response_preserves_envelope_and_storage_failure() {
  MockSink sink;
  const auto result = run(awtrix_test::kSoundsBackup, awtrix_test::kSoundsBackup_len, sink);
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_TRUE(api::restoreChangedAssets(result));
  const unsigned commits = sink.commits;
  const auto response = api::restoreResponse(result);
  TEST_ASSERT_EQUAL_INT(200, response.status);
  TEST_ASSERT_EQUAL_STRING(result.toJson().c_str(), response.body.c_str());
  const auto pending = api::restoreResponse(result, true);
  TEST_ASSERT_EQUAL_INT(507, pending.status);
  TEST_ASSERT_TRUE(pending.body.find("insufficientStorage") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT(commits, sink.commits);  // Building a response has no storage effect.
}

void test_invalid_zip_response_has_a_parser_error() {
  MockSink sink;
  const std::string invalid = "this is not a zip";
  const auto result = run(reinterpret_cast<const unsigned char*>(invalid.data()), invalid.size(), sink);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_FALSE(result.error.empty());
  TEST_ASSERT_FALSE(sink.committed);
  const auto response = api::restoreResponse(result);
  TEST_ASSERT_EQUAL_INT(400, response.status);
  TEST_ASSERT_TRUE(response.body.find("\"ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(response.body.find("\"error\":") != std::string::npos);
}

void test_late_invalid_palette_and_failed_finalize_abort_the_file() {
  for (bool failFinalize : {false, true}) {
    MockSink sink;
    sink.declareEndFails = failFinalize;
    backup::RestoreApplier applier(sink);
    entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
    applier.onEntryStart("PALETTES/late.txt", 8);
    const uint8_t valid[] = {'F', 'F', '0', '0', '0', '0', '\n'};
    applier.onEntryData(valid, sizeof(valid));
    const uint8_t final = failFinalize ? '\n' : 0;
    applier.onEntryData(&final, 1);
    applier.onEntryEnd(true);
    applier.onArchiveEnd();
    TEST_ASSERT_TRUE(applier.result().ok);
    TEST_ASSERT_EQUAL_INT(0, applier.result().palettes);
    TEST_ASSERT_EQUAL_UINT(1, sink.aborts);
    TEST_ASSERT_FALSE(applier.result().warnings.empty());
    TEST_ASSERT_FALSE(api::restoreChangedAssets(applier.result()));
  }
}

void test_metadata_and_manifest_bounds_reject_declared_and_actual_overflow() {
  for (bool declared : {false, true}) {
    MockSink sink;
    backup::RestoreApplier applier(sink);
    entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
    const std::string large(backup::kMaxRestoreMetadataBytes + 1, ' ');
    applier.onEntryStart("config/system.json", declared ? large.size() : 0);
    applier.onEntryData(reinterpret_cast<const uint8_t*>(large.data()), large.size());
    applier.onEntryEnd(true);
    applier.onArchiveEnd();
    TEST_ASSERT_TRUE(applier.result().ok);
    TEST_ASSERT_EQUAL_INT(0, applier.result().system);
    TEST_ASSERT_TRUE(sink.systemJson.empty());
    TEST_ASSERT_EQUAL_UINT(1, applier.result().warnings.size());

    MockSink manifestSink;
    backup::RestoreApplier manifest(manifestSink);
    const std::string oversizedManifest(backup::kMaxRestoreManifestBytes + 1, ' ');
    manifest.onEntryStart("manifest.json", declared ? oversizedManifest.size() : 0);
    manifest.onEntryData(reinterpret_cast<const uint8_t*>(oversizedManifest.data()),
                         oversizedManifest.size());
    manifest.onEntryEnd(true);
    manifest.onArchiveEnd();
    TEST_ASSERT_FALSE(manifest.result().ok);
    TEST_ASSERT_FALSE(manifest.result().error.empty());
    TEST_ASSERT_FALSE(manifestSink.committed);
  }
}

void test_metadata_at_the_limit_remains_compatible() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  std::string manifest = R"({"app":"awtrix-ng","backupFormat":1})";
  manifest.resize(backup::kMaxRestoreManifestBytes, ' ');
  entry(applier, "manifest.json", manifest);
  std::string metadata = "{}";
  metadata.resize(backup::kMaxRestoreMetadataBytes, ' ');
  entry(applier, "config/system.json", metadata);
  applier.onArchiveEnd();
  TEST_ASSERT_TRUE(applier.result().ok);
  TEST_ASSERT_EQUAL_INT(1, applier.result().system);
  TEST_ASSERT_EQUAL_UINT(metadata.size(), sink.systemJson.size());
  TEST_ASSERT_TRUE(applier.result().warnings.empty());
}

void test_every_warning_counts_one_skipped_entry() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
  entry(applier, "zz/unknown.bin", "data");
  entry(applier, "ICONS/zz.png", "PNG data");
  entry(applier, "PALETTES/zz.txt", "hello world");
  applier.onArchiveEnd();
  const backup::RestoreResult& r = applier.result();
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(3, r.skipped);
  TEST_ASSERT_EQUAL_size_t(3, r.warnings.size());
  TEST_ASSERT_EQUAL_INT(0, r.icons);
  TEST_ASSERT_EQUAL_INT(0, r.palettes);
}

void test_warnings_are_bounded_without_masking_a_later_fatal_error() {
  MockSink sink;
  backup::RestoreApplier applier(sink);
  entry(applier, "manifest.json", R"({"app":"awtrix-ng","backupFormat":1})");
  for (std::size_t n = 0; n < backup::kMaxRestoreWarnings + 20; ++n)
    entry(applier, "unknown-" + std::to_string(n), "ignored");
  TEST_ASSERT_EQUAL_UINT(backup::kMaxRestoreWarnings + 1, applier.result().warnings.size());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(backup::kMaxRestoreWarnings + 20), applier.result().skipped);
  TEST_ASSERT_FALSE(applier.result().warnings.back().empty());
  applier.abort("later transport failure");
  TEST_ASSERT_FALSE(applier.result().ok);
  TEST_ASSERT_EQUAL_STRING("later transport failure", applier.result().error.c_str());
  TEST_ASSERT_FALSE(sink.committed);
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_routes_every_category);
  RUN_TEST(test_settings_backups_migrate_old_bars_and_preserve_separate_new_bars);
  RUN_TEST(test_an_empty_settings_backup_restores_both_default_weekday_bars);
  RUN_TEST(test_asset_files_written_to_absolute_paths);
  RUN_TEST(test_rejects_foreign_backup_without_touching_anything);
  RUN_TEST(test_rejects_path_traversal_entry);
  RUN_TEST(test_mp3_restore_and_content_sniff);
  RUN_TEST(test_script_sounds_restore_into_their_folder);
  RUN_TEST(test_script_sound_paths_are_held_to_the_name_rules);
  RUN_TEST(test_every_warning_counts_one_skipped_entry);
  RUN_TEST(test_result_json_reports_counts);
  RUN_TEST(test_origins_restore_runs_after_all_files_regardless_of_zip_order);
  RUN_TEST(test_invalid_or_corrupt_origins_do_not_replace_existing_metadata);
  RUN_TEST(test_zip_restore_is_independent_of_chunk_boundaries);
  RUN_TEST(test_parser_error_aborts_an_unfinished_asset_and_reports_why);
  RUN_TEST(test_explicit_abort_and_destructor_release_pending_files);
  RUN_TEST(test_restore_response_preserves_envelope_and_storage_failure);
  RUN_TEST(test_invalid_zip_response_has_a_parser_error);
  RUN_TEST(test_late_invalid_palette_and_failed_finalize_abort_the_file);
  RUN_TEST(test_metadata_and_manifest_bounds_reject_declared_and_actual_overflow);
  RUN_TEST(test_metadata_at_the_limit_remains_compatible);
  RUN_TEST(test_warnings_are_bounded_without_masking_a_later_fatal_error);
  return UNITY_END();
}

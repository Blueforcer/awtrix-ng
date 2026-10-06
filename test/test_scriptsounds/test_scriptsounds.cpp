#include "core/sound/RoutedPcmSink.h"
#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/AssetPaths.h"
#include "core/api/ApiRouter.h"
#include "core/api/AssetApi.h"
#include "core/api/AssetUploadSession.h"
#include "core/api/JsonReader.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/sound/AudioRouter.h"
#include "core/sound/SoundMp3.h"

using namespace awtrix;
using namespace awtrix::api;

void setUp() {}
void tearDown() {}

namespace {

// Names that must never become part of a path, whatever position they take in it.
const std::vector<std::string> kBadNames = {
    "", ".", "..", "../x", "a/b", "/a", "a\\b", "a.b", "a b", "boost.mp3", "C:x", "%2e%2e",
    std::string("a\0b", 3), "\xc3\xa4", std::string(33, 'a')};

// The stored sources a request sees while scripting is off.
struct Sources {
  std::vector<std::string> names;
  std::string header = "# @name  AWTRIX GP\n";
  mutable std::vector<std::string> reads;
  script::ConfigTextFn read = [this](const std::string& name, std::string& out) {
    reads.push_back(name);
    for (const std::string& n : names)
      if (n == name) {
        out = header + "def draw() end\n";
        return true;
      }
    return false;
  };
};

struct Folder : scriptsounds::Backend {
  std::vector<std::string> files;
  std::vector<std::string> calls;
  unsigned notifications = 0;
  scriptsounds::Backend& backend = *this;
  std::function<void(const std::string&)> onRelease;
  std::function<bool(const std::string&)> removable;
  bool remove(const std::string& path) override {
    if (removable && !removable(path)) return false;
    calls.push_back("remove:" + path);
    for (auto it = files.begin(); it != files.end(); ++it)
      if (*it == path) { files.erase(it); return true; }
    return false;
  }
  void eachFile(const std::string& dir, const Visit& visit) override {
    calls.push_back("files:" + dir);
    for (const std::string& file : files)
      if (file.rfind(dir + "/", 0) == 0) visit(file.substr(dir.size() + 1));
  }
  bool sha256(const std::string&, std::string&, uint64_t&) override { return false; }
  void release(const std::string& path) override { if (onRelease) onRelease(path); }
  void prune(const std::string& dir) override { calls.push_back("prune:" + dir); }
  void changed() override { ++notifications; }
};

// A speaker that holds open every file it plays, and a flash that refuses to delete an open file:
// the ESP32-S3's LittleFS in miniature.
struct Speaker : sound::RoutedPcmSink {
  std::vector<std::string> open;
  void setVolumes(const sound::Volumes&) override {}
  bool playMp3(const std::string& path, sound::Group) override { return hold(path); }
  void stopOneShot() override {}
  bool oneShotPlaying() const override { return !open.empty(); }
  bool mixes() const override { return true; }
  bool playEffect(const std::string& path) override { return hold(path); }
  bool playLoop(const std::string& path) override { return hold(path); }
  void release(const std::string& path) override {
    open.erase(std::remove_if(open.begin(), open.end(),
                              [&](const std::string& file) { return sound::within(file, path); }),
               open.end());
  }
  DispatchResult playStream(const std::string&, const std::string&, DispatchDetail&) override {
    return DispatchResult::Ok;
  }
  void stopStream() override {}
  void tick(int64_t) override {}
  bool isOpen(const std::string& path) const {
    return std::find(open.begin(), open.end(), path) != open.end();
  }

 private:
  bool hold(const std::string& path) {
    open.push_back(path);
    return true;
  }
};

struct Probe : sound::IAssetProbe {
  std::vector<std::string>* files = nullptr;
  bool hasFile(const std::string& path) const override {
    return std::find(files->begin(), files->end(), path) != files->end();
  }
};

std::string listingOf(asset::JsonListing::Kind kind,
                      const std::function<void(asset::JsonListing&)>& fill) {
  std::string json;
  asset::JsonListing listing(kind, [&](const std::string& chunk) { json += chunk; });
  listing.begin();
  fill(listing);
  listing.end(12, 34);
  return json;
}

}

// ---- The path rule -------------------------------------------------------------

void test_a_script_sound_lives_in_the_script_folder() {
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer", sound::scriptSoundDir("racer").c_str());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/boost.mp3",
                           sound::scriptMp3PathFor("racer", "boost").c_str());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/awtrix-gp/Lap_2.mp3",
                           sound::scriptMp3PathFor("awtrix-gp", "Lap_2").c_str());
  const std::string longest(32, 'z');
  TEST_ASSERT_EQUAL_STRING(("/SCRIPTS/" + longest + "/" + longest + ".mp3").c_str(),
                           sound::scriptMp3PathFor(longest, longest).c_str());
}

void test_no_name_can_step_out_of_its_folder() {
  for (const std::string& bad : kBadNames) {
    TEST_ASSERT_TRUE_MESSAGE(sound::scriptSoundDir(bad).empty(), bad.c_str());
    TEST_ASSERT_TRUE_MESSAGE(sound::scriptMp3PathFor(bad, "boost").empty(), bad.c_str());
    TEST_ASSERT_TRUE_MESSAGE(sound::scriptMp3PathFor("racer", bad).empty(), bad.c_str());
    TEST_ASSERT_TRUE_MESSAGE(sound::mp3PathFor(bad).empty(), bad.c_str());
  }
}

// A script's install name is its folder's name, so the two rules must never disagree.
void test_the_folder_rule_is_the_app_name_rule() {
  std::vector<std::string> names = kBadNames;
  for (const char* good : {"a", "Racer", "awtrix-gp", "my_script-2", "0", "-", "_"})
    names.push_back(good);
  names.push_back(std::string(32, 'a'));
  for (const std::string& name : names)
    TEST_ASSERT_EQUAL_MESSAGE(isValidAppName(name), sound::validName(name), name.c_str());
}

void test_a_sound_file_name_is_its_name_and_mp3() {
  TEST_ASSERT_EQUAL_STRING("boost", sound::mp3NameOfFile("boost.mp3").c_str());
  for (const char* bad : {"boost", ".mp3", "boost.MP3", "boost.mp3.mp3", "a/b.mp3", "../b.mp3",
                          "bo ost.mp3", "boost.txt", ""})
    TEST_ASSERT_TRUE_MESSAGE(sound::mp3NameOfFile(bad).empty(), bad);
}

void test_only_exact_script_sound_paths_qualify() {
  TEST_ASSERT_TRUE(sound::isScriptMp3Path("/SCRIPTS/racer/boost.mp3"));
  for (const char* bad : {"/SCRIPTS/racer.ax", "/SCRIPTS/racer/", "/SCRIPTS/racer/boost",
                          "/SCRIPTS/racer/sub/boost.mp3", "/SCRIPTS/../MP3/boost.mp3",
                          "/SCRIPTS/racer/../boost.mp3", "/SCRIPTS//boost.mp3",
                          "/SCRIPTS/ra.cer/boost.mp3", "/MP3/boost.mp3", "SCRIPTS/racer/boost.mp3",
                          "/SCRIPTS/racer/boost.mp3/"})
    TEST_ASSERT_FALSE_MESSAGE(sound::isScriptMp3Path(bad), bad);
}

// Restore and uploads check a script's sound like any MP3, and write nothing deeper.
void test_a_script_sound_is_an_mp3_asset() {
  TEST_ASSERT_TRUE(assets::kindFor("/SCRIPTS/racer/boost.mp3") == assets::AssetKind::Mp3);
  TEST_ASSERT_TRUE(assets::kindFor("/SCRIPTS/racer.ax") == assets::AssetKind::Unknown);
  TEST_ASSERT_TRUE(assets::kindFor("/SCRIPTS/racer/boost.txt") == assets::AssetKind::Unknown);
  TEST_ASSERT_TRUE(assets::isBackupWritable("/SCRIPTS/racer.ax"));
  TEST_ASSERT_TRUE(assets::isBackupWritable("/SCRIPTS/racer.store.json"));
  TEST_ASSERT_TRUE(assets::isBackupWritable("/SCRIPTS/racer/boost.mp3"));
  for (const char* bad : {"/SCRIPTS/racer/boost.txt", "/SCRIPTS/racer/sub/boost.mp3",
                          "/SCRIPTS/racer/bad name.mp3", "/SCRIPTS/ra.cer/boost.mp3",
                          "/SCRIPTS/../boost.mp3", "/SCRIPTS/racer/..mp3"})
    TEST_ASSERT_FALSE_MESSAGE(assets::isBackupWritable(bad), bad);
  asset::UploadValidator mp3("/SCRIPTS/racer/boost.mp3");
  TEST_ASSERT_TRUE(mp3.append(reinterpret_cast<const uint8_t*>("ID3"), 3));
  TEST_ASSERT_TRUE(mp3.finish());
  asset::UploadValidator text("/SCRIPTS/racer/boost.mp3");
  TEST_ASSERT_TRUE(text.append(reinterpret_cast<const uint8_t*>("hello"), 5));
  TEST_ASSERT_FALSE(text.finish());
}

// ---- Routes --------------------------------------------------------------------

void test_routes_are_matched_by_shape() {
  scriptsounds::Route r = scriptsounds::match("/api/v1/apps/script/racer/sounds");
  TEST_ASSERT_TRUE(r.matched);
  TEST_ASSERT_FALSE(r.item);
  TEST_ASSERT_EQUAL_STRING("racer", r.script.c_str());

  r = scriptsounds::match("/api/v1/apps/script/racer/sounds/boost");
  TEST_ASSERT_TRUE(r.matched && r.item);
  TEST_ASSERT_EQUAL_STRING("racer", r.script.c_str());
  TEST_ASSERT_EQUAL_STRING("boost", r.sound.c_str());

  // Malformed names still land here, to be refused as names.
  r = scriptsounds::match("/api/v1/apps/script/ba d/sounds/a/b");
  TEST_ASSERT_TRUE(r.matched && r.item);
  TEST_ASSERT_EQUAL_STRING("ba d", r.script.c_str());
  TEST_ASSERT_EQUAL_STRING("a/b", r.sound.c_str());
  r = scriptsounds::match("/api/v1/apps/script//sounds/");
  TEST_ASSERT_TRUE(r.matched && r.item);
  TEST_ASSERT_TRUE(r.script.empty() && r.sound.empty());

  for (const char* other : {"/api/v1/apps/script/racer", "/api/v1/apps/script/sounds",
                            "/api/v1/apps/script/racer/soundsx", "/api/v1/apps/script/a/b/sounds",
                            "/api/v1/apps/racer/sounds", "/api/v1/apps/script-update/racer/sounds"})
    TEST_ASSERT_FALSE_MESSAGE(scriptsounds::match(other).matched, other);

  TEST_ASSERT_TRUE(scriptsounds::isUpload("/api/v1/apps/script/racer/sounds"));
  TEST_ASSERT_FALSE(scriptsounds::isUpload("/api/v1/apps/script/racer/sounds/boost"));
  TEST_ASSERT_FALSE(scriptsounds::isUpload("/api/v1/audio/mp3"));
}

// The command router leaves the sound routes to the transport, and a PUT there is no source upload.
void test_the_command_router_leaves_sounds_alone() {
  Command c;
  HttpResult imm;
  for (const char* method : {"GET", "POST", "PUT", "DELETE", "PATCH"}) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(RouteOutcome::NoMatch),
        static_cast<int>(routeHttp(method, "/api/v1/apps/script/racer/sounds", "", c, imm)), method);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        static_cast<int>(RouteOutcome::NoMatch),
        static_cast<int>(routeHttp(method, "/api/v1/apps/script/racer/sounds/boost", "", c, imm)),
        method);
  }
  TEST_ASSERT_FALSE(isRawBodyWrite("PUT", "/api/v1/apps/script/racer/sounds"));
  TEST_ASSERT_TRUE(isRawBodyWrite("PUT", "/api/v1/apps/script/racer"));
  TEST_ASSERT_TRUE(resolveHttpMethod("POST", "/api/v1/apps/script/racer/sounds/boost", "DELETE")
                       .error == nullptr);
}

// ---- Which scripts exist ---------------------------------------------------------

void test_a_stored_script_is_found_with_its_title() {
  Sources sources;
  sources.names = {"racer", "plain"};
  const scriptsounds::InstalledScripts installed(nullptr, sources.read);
  std::string title;
  TEST_ASSERT_TRUE(installed.find("racer", &title));
  TEST_ASSERT_EQUAL_STRING("AWTRIX GP", title.c_str());
  sources.header = "# @desc no name here\n";
  TEST_ASSERT_TRUE(installed.find("plain", &title));
  TEST_ASSERT_EQUAL_STRING("plain", title.c_str());
  TEST_ASSERT_FALSE(installed.find("ghost", &title));
}

void test_a_malformed_name_is_never_looked_up() {
  Sources sources;
  sources.names = {"racer"};
  const scriptsounds::InstalledScripts installed(nullptr, sources.read);
  for (const std::string& bad : kBadNames) TEST_ASSERT_FALSE(installed.find(bad));
  TEST_ASSERT_TRUE(sources.reads.empty());

  const script::ConfigTextFn none;
  TEST_ASSERT_FALSE(scriptsounds::InstalledScripts(nullptr, none).find("racer"));
}

void test_a_script_is_checked_before_its_sounds_are_listed() {
  Sources sources;
  sources.names = {"racer"};
  const scriptsounds::InstalledScripts installed(nullptr, sources.read);
  TEST_ASSERT_EQUAL_INT(200, scriptsounds::checkSounds("racer", installed, false).status);
  HttpResult r = scriptsounds::checkSounds("ghost", installed, false);
  TEST_ASSERT_EQUAL_INT(404, r.status);
  TEST_ASSERT_TRUE(r.body.find("\"notFound\"") != std::string::npos);
  // Deleting a script keeps its sounds, and they stay listable until they are deleted too.
  TEST_ASSERT_EQUAL_INT(200, scriptsounds::checkSounds("ghost", installed, true).status);
  r = scriptsounds::checkSounds("../x", installed, true);
  TEST_ASSERT_EQUAL_INT(400, r.status);
  TEST_ASSERT_TRUE(r.body.find("\"invalidName\"") != std::string::npos);
  TEST_ASSERT_TRUE(r.body.find("\"field\":\"name\"") != std::string::npos);
}

// ---- Upload ----------------------------------------------------------------------

void test_an_upload_lands_in_the_script_folder() {
  Sources sources;
  sources.names = {"racer"};
  const scriptsounds::InstalledScripts installed(nullptr, sources.read);
  const asset::UploadTarget target = scriptsounds::uploadTarget("racer", "boost.mp3", installed);
  TEST_ASSERT_TRUE(target.ok());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/boost.mp3", target.path.c_str());
}

// Both names are judged before anything is looked up; a script that is not there comes after.
void test_an_upload_is_refused_in_the_documented_order() {
  Sources sources;
  sources.names = {"racer"};
  const scriptsounds::InstalledScripts installed(nullptr, sources.read);

  asset::UploadTarget target = scriptsounds::uploadTarget("bad!", "boost.mp3", installed);
  TEST_ASSERT_EQUAL_INT(400, target.result.status);
  TEST_ASSERT_TRUE(target.result.body.find("\"field\":\"name\"") != std::string::npos);
  for (const char* file : {"boost", "boost.txt", "bad name.mp3", "../boost.mp3", "/ICONS/x.mp3",
                           "a/boost.mp3", ".mp3", ""}) {
    target = scriptsounds::uploadTarget("racer", file, installed);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, target.result.status, file);
    TEST_ASSERT_TRUE_MESSAGE(target.result.body.find("\"invalidName\"") != std::string::npos, file);
    TEST_ASSERT_TRUE_MESSAGE(target.path.empty(), file);
  }
  TEST_ASSERT_TRUE(sources.reads.empty());

  target = scriptsounds::uploadTarget("ghost", "boost.mp3", installed);
  TEST_ASSERT_EQUAL_INT(404, target.result.status);
  TEST_ASSERT_TRUE(target.path.empty());
}

// The upload streams through the same session as every other file: sniffed, staged, published.
void test_an_upload_session_takes_a_resolved_target() {
  std::vector<std::string> calls;
  asset::StreamStorage storage;
  storage.begin = [&](const std::string& path) { calls.push_back("begin:" + path); return true; };
  storage.write = [&](const uint8_t*, std::size_t) { return true; };
  storage.publish = [&](const std::string& path) { calls.push_back("publish:" + path); return true; };
  storage.discard = [&] { calls.push_back("discard"); };

  asset::UploadSession session;
  session.reset(storage);
  session.authorize(HttpResult{});
  asset::UploadTarget target;
  target.path = "/SCRIPTS/racer/boost.mp3";
  session.start(target);
  session.append(reinterpret_cast<const uint8_t*>("ID3"), 3);
  session.end();
  TEST_ASSERT_EQUAL_INT(200, session.complete().status);
  TEST_ASSERT_EQUAL_size_t(2, calls.size());
  TEST_ASSERT_EQUAL_STRING("publish:/SCRIPTS/racer/boost.mp3", calls[1].c_str());

  calls.clear();
  session.reset(storage);
  session.authorize(HttpResult{});
  session.start(target);
  session.append(reinterpret_cast<const uint8_t*>("text"), 4);
  session.end();
  const HttpResult refused = session.complete();
  TEST_ASSERT_EQUAL_INT(415, refused.status);
  TEST_ASSERT_EQUAL_STRING("discard", calls.back().c_str());

  // A refused target writes nothing at all.
  calls.clear();
  session.reset(storage);
  session.authorize(HttpResult{});
  asset::UploadTarget missing;
  missing.result.status = 404;
  missing.result.body = errorJson("notFound", "no such script");
  session.start(missing);
  TEST_ASSERT_EQUAL_INT(404, session.complete().status);
  TEST_ASSERT_TRUE(calls.empty());
}

// ---- Delete ----------------------------------------------------------------------

void test_deleting_a_sound_prunes_the_folder() {
  Folder folder;
  folder.files = {"/SCRIPTS/racer/boost.mp3"};
  const HttpResult r = scriptsounds::deleteSound("racer", "boost", folder.backend);
  TEST_ASSERT_EQUAL_INT(200, r.status);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", r.body.c_str());
  TEST_ASSERT_EQUAL_size_t(2, folder.calls.size());
  TEST_ASSERT_EQUAL_STRING("remove:/SCRIPTS/racer/boost.mp3", folder.calls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("prune:/SCRIPTS/racer", folder.calls[1].c_str());
  TEST_ASSERT_EQUAL_UINT(1, folder.notifications);
}

void test_deleting_refuses_before_touching_anything() {
  Folder folder;
  folder.files = {"/SCRIPTS/racer/boost.mp3"};

  HttpResult r = scriptsounds::deleteSound("../racer", "boost", folder.backend);
  TEST_ASSERT_EQUAL_INT(400, r.status);
  TEST_ASSERT_TRUE(r.body.find("\"field\":\"name\"") != std::string::npos);
  for (const std::string& bad : kBadNames) {
    r = scriptsounds::deleteSound("racer", bad, folder.backend);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, r.status, bad.c_str());
    TEST_ASSERT_TRUE(r.body.find("\"invalidName\"") != std::string::npos);
  }
  TEST_ASSERT_EQUAL_INT(400, scriptsounds::deleteSounds("../racer", folder.backend).status);
  TEST_ASSERT_TRUE(folder.calls.empty());

  r = scriptsounds::deleteSound("racer", "missing", folder.backend);
  TEST_ASSERT_EQUAL_INT(404, r.status);
  TEST_ASSERT_TRUE(r.body.find("no such MP3") != std::string::npos);
  TEST_ASSERT_EQUAL_size_t(1, folder.calls.size());
  TEST_ASSERT_EQUAL_UINT(0, folder.notifications);
  TEST_ASSERT_EQUAL_size_t(1, folder.files.size());
}

// A sound kept from a deleted script is deleted like any other: no installed script is asked for.
void test_a_kept_sound_is_deleted_without_its_script() {
  Folder folder;
  folder.files = {"/SCRIPTS/gone/boost.mp3"};
  TEST_ASSERT_EQUAL_INT(200, scriptsounds::deleteSound("gone", "boost", folder.backend).status);
  TEST_ASSERT_TRUE(folder.files.empty());
}

// All of a script's sounds go at once: let go of first, then every file, then the folder.
void test_deleting_all_sounds_empties_the_folder() {
  Folder folder;
  folder.files = {"/SCRIPTS/racer/boost.mp3", "/SCRIPTS/racer/lap.mp3", "/SCRIPTS/racer2/boost.mp3"};
  std::vector<std::string> released;
  folder.onRelease = [&](const std::string& path) {
    released.push_back(path);
    folder.calls.push_back("release:" + path);
  };
  const HttpResult r = scriptsounds::deleteSounds("racer", folder.backend);
  TEST_ASSERT_EQUAL_INT(200, r.status);
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", r.body.c_str());
  TEST_ASSERT_TRUE(released == std::vector<std::string>({"/SCRIPTS/racer"}));
  TEST_ASSERT_TRUE(folder.files == std::vector<std::string>({"/SCRIPTS/racer2/boost.mp3"}));
  TEST_ASSERT_EQUAL_STRING("files:/SCRIPTS/racer", folder.calls.front().c_str());
  TEST_ASSERT_EQUAL_STRING("release:/SCRIPTS/racer", folder.calls[1].c_str());
  TEST_ASSERT_EQUAL_STRING("prune:/SCRIPTS/racer", folder.calls.back().c_str());
  TEST_ASSERT_EQUAL_UINT(1, folder.notifications);

  // Nothing there is no error, and changes nothing.
  folder.calls.clear();
  TEST_ASSERT_EQUAL_INT(200, scriptsounds::deleteSounds("racer", folder.backend).status);
  TEST_ASSERT_EQUAL_UINT(1, folder.notifications);
  TEST_ASSERT_TRUE(released.size() == 1);
}

// The loop a script plays holds its file open. The delete asks the output to let go first, and
// only the file being deleted stops.
void test_a_playing_sound_is_let_go_of_before_it_is_deleted() {
  Folder folder;
  folder.files = {"/SCRIPTS/racer/theme.mp3", "/SCRIPTS/racer/boost.mp3", "/MP3/ding.mp3"};
  Speaker speaker;
  Probe probe;
  probe.files = &folder.files;
  sound::AudioRouter router;
  router.setPcm(&speaker);
  router.setAssets(&probe);
  DispatchDetail detail;
  const auto spec = [](const char* json) {
    sound::Choices choices;
    DispatchDetail ignored;
    TEST_ASSERT_TRUE(sound::parse(json, sound::Origin::Script, choices, ignored));
    return choices;
  };
  TEST_ASSERT_TRUE(router.play(spec("{\"file\":\"theme\",\"loop\":true}"), sound::Group::App,
                               "racer", detail) == sound::PlayResult::Ok);
  TEST_ASSERT_TRUE(router.playEffect(spec("\"boost\""), "racer", detail) == sound::PlayResult::Ok);
  TEST_ASSERT_TRUE(router.playEffect(spec("\"ding\""), "racer", detail) == sound::PlayResult::Ok);
  TEST_ASSERT_EQUAL_size_t(3, speaker.open.size());

  folder.removable = [&](const std::string& path) { return !speaker.isOpen(path); };
  // Without the release the flash refuses, and the sound stays where it was.
  TEST_ASSERT_EQUAL_INT(404, scriptsounds::deleteSound("racer", "theme", folder.backend).status);
  TEST_ASSERT_EQUAL_size_t(3, folder.files.size());

  folder.onRelease = [&](const std::string& path) { router.release(path); };
  TEST_ASSERT_EQUAL_INT(200, scriptsounds::deleteSound("racer", "theme", folder.backend).status);
  TEST_ASSERT_FALSE(speaker.isOpen("/SCRIPTS/racer/theme.mp3"));
  TEST_ASSERT_TRUE(speaker.isOpen("/SCRIPTS/racer/boost.mp3"));
  TEST_ASSERT_TRUE(speaker.isOpen("/MP3/ding.mp3"));
  TEST_ASSERT_EQUAL_size_t(2, folder.files.size());
}

// A folder takes everything inside it along, and nothing that only shares the start of its name.
void test_within_follows_folder_boundaries() {
  TEST_ASSERT_TRUE(sound::within("/SCRIPTS/racer/boost.mp3", "/SCRIPTS/racer"));
  TEST_ASSERT_TRUE(sound::within("/SCRIPTS/racer/boost.mp3", "/SCRIPTS/racer/"));
  TEST_ASSERT_TRUE(sound::within("/SCRIPTS/racer/boost.mp3", "/SCRIPTS/racer/boost.mp3"));
  TEST_ASSERT_TRUE(sound::within("/SCRIPTS/racer", "/SCRIPTS/racer"));
  TEST_ASSERT_FALSE(sound::within("/SCRIPTS/racer2/boost.mp3", "/SCRIPTS/racer"));
  TEST_ASSERT_FALSE(sound::within("/SCRIPTS/racer/boost.mp3", "/SCRIPTS/racer/boost"));
  TEST_ASSERT_FALSE(sound::within("/SCRIPTS", "/SCRIPTS/racer"));
  TEST_ASSERT_FALSE(sound::within("/SCRIPTS/racer/boost.mp3", ""));
  TEST_ASSERT_FALSE(sound::within("", "/SCRIPTS/racer"));
}

// ---- Listings ----------------------------------------------------------------------

void test_the_mp3_listing_groups_sounds_by_script() {
  const std::string json = listingOf(asset::JsonListing::Kind::Mp3, [](asset::JsonListing& l) {
    l.file("ding.mp3", 40118);
    l.group("awtrix-gp", "AWTRIX GP");
    l.file("boost.mp3", 20411);
    l.file("lap.mp3", 8);
    l.group("empty", "Empty");
    l.group("doom", "Doom \"2\"");
    l.file("shot.mp3", 5);
    l.group("gone", "gone", true);
    l.file("old.mp3", 3);
  });
  TEST_ASSERT_TRUE(isWellFormed(json));
  TEST_ASSERT_EQUAL_STRING(
      "{\"files\":[{\"name\":\"ding.mp3\",\"size\":40118}],"
      "\"scripts\":[{\"name\":\"awtrix-gp\",\"title\":\"AWTRIX GP\",\"orphan\":false,\"files\":["
      "{\"name\":\"boost.mp3\",\"size\":20411},{\"name\":\"lap.mp3\",\"size\":8}]},"
      "{\"name\":\"doom\",\"title\":\"Doom \\\"2\\\"\",\"orphan\":false,\"files\":[{\"name\":\"shot.mp3\",\"size\":5}]},"
      "{\"name\":\"gone\",\"title\":\"gone\",\"orphan\":true,\"files\":[{\"name\":\"old.mp3\",\"size\":3}]}],"
      "\"usedBytes\":12,\"totalBytes\":34}",
      json.c_str());
}

void test_the_mp3_listing_is_valid_without_any_script() {
  TEST_ASSERT_EQUAL_STRING(
      "{\"files\":[],\"scripts\":[],\"usedBytes\":12,\"totalBytes\":34}",
      listingOf(asset::JsonListing::Kind::Mp3, [](asset::JsonListing&) {}).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "{\"files\":[],\"scripts\":[],\"usedBytes\":12,\"totalBytes\":34}",
      listingOf(asset::JsonListing::Kind::Mp3, [](asset::JsonListing& l) {
        l.group("empty", "Empty");
      }).c_str());
  // A second listing from the same object starts over.
  std::string json;
  asset::JsonListing listing(asset::JsonListing::Kind::Mp3,
                             [&](const std::string& chunk) { json += chunk; });
  listing.begin();
  listing.group("racer", "Racer");
  listing.file("boost.mp3", 1);
  listing.end(1, 2);
  json.clear();
  listing.begin();
  listing.file("ding.mp3", 1);
  listing.end(1, 2);
  TEST_ASSERT_EQUAL_STRING(
      "{\"files\":[{\"name\":\"ding.mp3\",\"size\":1}],\"scripts\":[],\"usedBytes\":1,"
      "\"totalBytes\":2}",
      json.c_str());
}

// The other listings never grow a "scripts" member.
void test_groups_belong_to_the_mp3_listing_alone() {
  const std::string json = listingOf(asset::JsonListing::Kind::Files, [](asset::JsonListing& l) {
    l.group("racer", "Racer");
    l.file("boost.mp3", 1, std::string(64, 'a'));
  });
  TEST_ASSERT_TRUE(isWellFormed(json));
  TEST_ASSERT_EQUAL_STRING(
      ("{\"files\":[{\"name\":\"boost.mp3\",\"size\":1,\"sha256\":\"" + std::string(64, 'a') +
       "\"}],\"usedBytes\":12,\"totalBytes\":34}").c_str(),
      json.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_script_sound_lives_in_the_script_folder);
  RUN_TEST(test_no_name_can_step_out_of_its_folder);
  RUN_TEST(test_the_folder_rule_is_the_app_name_rule);
  RUN_TEST(test_a_sound_file_name_is_its_name_and_mp3);
  RUN_TEST(test_only_exact_script_sound_paths_qualify);
  RUN_TEST(test_a_script_sound_is_an_mp3_asset);
  RUN_TEST(test_routes_are_matched_by_shape);
  RUN_TEST(test_the_command_router_leaves_sounds_alone);
  RUN_TEST(test_a_stored_script_is_found_with_its_title);
  RUN_TEST(test_a_malformed_name_is_never_looked_up);
  RUN_TEST(test_a_script_is_checked_before_its_sounds_are_listed);
  RUN_TEST(test_an_upload_lands_in_the_script_folder);
  RUN_TEST(test_an_upload_is_refused_in_the_documented_order);
  RUN_TEST(test_an_upload_session_takes_a_resolved_target);
  RUN_TEST(test_deleting_a_sound_prunes_the_folder);
  RUN_TEST(test_deleting_refuses_before_touching_anything);
  RUN_TEST(test_a_kept_sound_is_deleted_without_its_script);
  RUN_TEST(test_deleting_all_sounds_empties_the_folder);
  RUN_TEST(test_a_playing_sound_is_let_go_of_before_it_is_deleted);
  RUN_TEST(test_within_follows_folder_boundaries);
  RUN_TEST(test_the_mp3_listing_groups_sounds_by_script);
  RUN_TEST(test_the_mp3_listing_is_valid_without_any_script);
  RUN_TEST(test_groups_belong_to_the_mp3_listing_alone);
  return UNITY_END();
}

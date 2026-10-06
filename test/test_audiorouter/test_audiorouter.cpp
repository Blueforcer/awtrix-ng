#include "platform/tc002/audio/AudioClip.h"
#include "core/sound/RoutedPcmSink.h"
#include <unity.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "core/sound/AudioRouter.h"
#include "core/sound/SoundMp3.h"
#include "core/sound/SoundSpec.h"

using namespace awtrix;
using awtrix::sound::AudioRouter;
using awtrix::sound::Choices;
using awtrix::sound::Group;
using awtrix::sound::Origin;
using awtrix::sound::PlayResult;
using awtrix::sound::Stop;

void setUp() {}
void tearDown() {}

namespace {

struct FakeTone : sound::IToneSink {
  std::vector<std::string> stored;   // melodies that exist
  std::vector<std::string> asked;    // names looked up
  std::vector<std::string> played;   // RTTTL handed over
  std::vector<Group> groups;
  sound::Volumes volumes;
  int stops = 0, ticks = 0, volumeWrites = 0;
  bool playing = false;
  // False while the speaker is taken, as the TC002's is by its voice.
  bool ready = true;
  // True where melodies come out of the station's speaker, as on the TC002.
  bool shared = false;
  bool sharesPcmOutput() const override { return shared; }

  void begin() override {}
  void setVolumes(const sound::Volumes& v) override {
    volumes = v;
    ++volumeWrites;
  }
  bool playRtttl(const std::string& rtttl, Group group) override {
    if (!ready) return false;
    played.push_back(rtttl);
    groups.push_back(group);
    playing = true;
    return true;
  }
  bool playMelodyFile(const std::string& name, Group group) override {
    asked.push_back(name);
    if (!ready || std::find(stored.begin(), stored.end(), name) == stored.end()) return false;
    groups.push_back(group);
    playing = true;
    return true;
  }
  void stop() override {
    ++stops;
    playing = false;
  }
  void tick() override { ++ticks; }
  bool isPlaying() const override { return playing; }
};

struct FakeTrack : sound::ITrackSink {
  std::vector<int> played;
  std::vector<Group> groups;
  sound::Volumes volumes;
  int stops = 0, ticks = 0, volumeWrites = 0;
  bool playing = false;

  void begin() override {}
  void setVolumes(const sound::Volumes& v) override {
    volumes = v;
    ++volumeWrites;
  }
  // The real module cannot say whether the track is on its card, so neither does this.
  bool playTrack(int track, Group group) override {
    played.push_back(track);
    groups.push_back(group);
    playing = true;
    return true;
  }
  void stop() override {
    ++stops;
    playing = false;
  }
  void tick() override { ++ticks; }
  bool isPlaying() const override { return playing; }
};

struct FakePcm : sound::RoutedPcmSink {
  sound::Volumes volumes;
  int volumeWrites = 0;
  void setVolumes(const sound::Volumes& v) override {
    volumes = v;
    ++volumeWrites;
  }

  std::vector<std::pair<std::string, Group>> mp3s;
  std::vector<std::string> streams;
  int mp3Stops = 0, streamStops = 0, ticks = 0;
  bool mp3Running = false, streamRunning = false;
  bool playMp3(const std::string& path, Group group) override {
    mp3s.push_back({path, group});
    mp3Running = true;
    return true;
  }
  void stopOneShot() override {
    ++mp3Stops;
    mp3Running = false;
  }
  bool oneShotPlaying() const override { return mp3Running; }
  // Set where a test plays the TC002's voice: the sink's own one-shot in that group.
  bool reportsGroup = false;
  Group playingGroup = Group::Alert;
  bool oneShotGroup(Group& group) const override {
    if (!reportsGroup || !mp3Running) return false;
    group = playingGroup;
    return true;
  }

  bool mixing = false;
  std::vector<std::string> effects, loops;
  int effectStops = 0, loopStops = 0;
  bool effectRunning = false, loopRunning = false;
  bool mixes() const override { return mixing; }
  bool playEffect(const std::string& path) override {
    if (!mixing) return RoutedPcmSink::playEffect(path);
    effects.push_back(path);
    effectRunning = true;
    return true;
  }
  bool playLoop(const std::string& path) override {
    if (!mixing) return RoutedPcmSink::playLoop(path);
    loops.push_back(path);
    loopRunning = true;
    return true;
  }
  void stopEffects() override {
    ++effectStops;
    effectRunning = false;
  }
  void stopLoop() override {
    ++loopStops;
    loopRunning = false;
  }
  bool effectsPlaying() const override { return effectRunning; }
  bool loopPlaying() const override { return loopRunning; }

  std::vector<std::string> releases;
  void release(const std::string& path) override { releases.push_back(path); }
  DispatchResult playStream(const std::string& url, const std::string&,
                            DispatchDetail&) override {
    streams.push_back(url);
    streamRunning = true;
    loopRunning = effectRunning = false;
    return DispatchResult::Ok;
  }
  void stopStream() override {
    ++streamStops;
    streamRunning = false;
  }
  void tick(int64_t) override { ++ticks; }

  // A synthesizer that takes every song but "bad".
  bool synth = false;
  bool speakerReady = true;
  std::vector<std::pair<std::string, bool>> songs;
  std::vector<std::string> fxs;
  std::vector<std::pair<std::string, Group>> songsOnce;
  bool synthesizes() const override { return synth; }
  bool checkSong(const std::string& text, std::string& error) override {
    if (text != "bad") return true;
    error = "unexpected 'x' (line 1, column 5)";
    return false;
  }
  bool playSong(const std::string& text, bool nextBar) override {
    if (!speakerReady) return false;
    songs.push_back({text, nextBar});
    loopRunning = true;
    return true;
  }
  bool playFx(const std::string& text) override {
    if (!speakerReady) return false;
    fxs.push_back(text);
    effectRunning = true;
    return true;
  }
  bool playSongOnce(const std::string& text, Group group) override {
    if (!speakerReady) return false;
    songsOnce.push_back({text, group});
    mp3Running = true;
    return true;
  }
  // Plays every clip but one that starts "bad".
  bool clipping = false;
  std::vector<std::string> clipsPlayed;
  bool clips() const override { return clipping; }
  DispatchResult playClip(std::string&& bytes, std::string& error) override {
    if (!bytes.compare(0, 3, "bad")) {
      error = "not WAV or MP3";
      return DispatchResult::ValidationError;
    }
    if (!speakerReady) return DispatchResult::Unavailable;
    clipsPlayed.push_back(std::move(bytes));
    mp3Running = true;
    return DispatchResult::Ok;
  }

  // Speaks every text but "bad" while the speaker is ready.
  bool speaking = false;
  std::vector<std::pair<std::string, Group>> speechRequests;
  bool speaks() const override { return speaking; }
  bool checkSpeech(const std::string& text, DispatchDetail& detail) override {
    if (text != "bad") return true;
    detail.message = "no words to speak";
    return false;
  }
  DispatchResult playSpeech(const std::string& text, Group group,
                            DispatchDetail& detail) override {
    if (!checkSpeech(text, detail)) return DispatchResult::ValidationError;
    if (!speakerReady) {
      detail.message = "speaker unavailable";
      return DispatchResult::Unavailable;
    }
    speechRequests.push_back({text, group});
    mp3Running = true;
    return DispatchResult::Ok;
  }

  // Fetches every address while the speaker is ready.
  bool fetching = false;
  Group urlGroup = Group::Alert;
  std::vector<std::tuple<std::string, Group, bool>> urls;
  std::string pendingUrlError;
  bool fetches() const override { return fetching; }
  bool playUrl(const std::string& url, Group group, bool music) override {
    if (!speakerReady) return false;
    urls.emplace_back(url, group, music);
    if (!music) urlGroup = group;
    (music ? loopRunning : mp3Running) = true;
    return true;
  }
  bool takeError(sound::PcmError& error) override {
    bool music = false;
    if (!takeUrlError(error.message, music)) return false;
    error.group = music ? Group::App : urlGroup;
    error.stopRepeat = !music;
    return true;
  }
  bool pendingUrlMusic = false;
  bool takeUrlError(std::string& error, bool& music) override {
    if (pendingUrlError.empty()) return false;
    error = pendingUrlError;
    music = pendingUrlMusic;
    pendingUrlError.clear();
    return true;
  }

  // The hold the router asked for, every change in order.
  std::vector<bool> holds;
  void holdStream(bool held) override { holds.push_back(held); }
};

// stored names MP3s in /MP3, melodies the melodies the tone side keeps; files holds any other
// path, a script's sounds among them.
struct FakeAssets : sound::IAssetProbe {
  std::vector<std::string> stored;
  std::vector<std::string> files;
  const std::vector<std::string>* melodies = nullptr;
  mutable std::vector<std::string> asked;
  bool hasFile(const std::string& path) const override {
    asked.push_back(path);
    if (std::find(files.begin(), files.end(), path) != files.end()) return true;
    for (const std::string& name : stored)
      if (sound::mp3PathFor(name) == path) return true;
    if (melodies)
      for (const std::string& name : *melodies)
        if (sound::melodyPathFor(name) == path) return true;
    return false;
  }
};

Choices parsed(const char* json, Origin origin) {
  Choices choices;
  DispatchDetail detail;
  TEST_ASSERT_TRUE_MESSAGE(sound::parse(json, origin, choices, detail), json);
  return choices;
}

// Everything wired up, which is the case that can actually get the routing wrong.
struct Rig {
  FakeTone tone;
  FakeTrack track;
  FakePcm pcm;
  FakeAssets assets;
  AudioRouter router;
  DispatchDetail detail;

  Rig() {
    assets.melodies = &tone.stored;
    router.setTone(&tone);
    router.setTrack(&track);
    router.setPcm(&pcm);
    router.setAssets(&assets);
  }

  // A script's own call when script is named, a request over the API otherwise.
  PlayResult play(const char* json, Group group = Group::Alert, const std::string& script = "") {
    const Choices choices = parsed(json, script.empty() ? Origin::Play : Origin::Script);
    detail.clear();
    return router.play(choices, group, script, detail);
  }

  PlayResult effect(const char* json, const std::string& script) {
    const Choices choices = parsed(json, Origin::Script);
    detail.clear();
    return router.playEffect(choices, script, detail);
  }

  DispatchResult clip(std::string bytes) {
    detail.clear();
    return tc002::playClip(&pcm, router, std::move(bytes), detail);
  }
};

void assertResult(PlayResult expected, PlayResult actual, const char* what) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), what);
}

void assertResult(DispatchResult expected, DispatchResult actual, const char* what) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), what);
}

}  // namespace

// ---- Finding a sound ---------------------------------------------------------

// The lookup order of a name: the asking script's own sound, a shared MP3, then a melody.
void test_a_name_finds_own_sound_then_shared_then_melody(void) {
  Rig rig;
  rig.assets.files.push_back("/SCRIPTS/racer/boost.mp3");
  rig.assets.stored.push_back("boost");
  rig.tone.stored.push_back("boost");
  assertResult(PlayResult::Ok, rig.play("\"boost\"", Group::App, "racer"), "own first");
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/boost.mp3", rig.pcm.mp3s.back().first.c_str());
  assertResult(PlayResult::Ok, rig.play("\"boost\""), "shared without a script");
  TEST_ASSERT_EQUAL_STRING("/MP3/boost.mp3", rig.pcm.mp3s.back().first.c_str());
  rig.assets.stored.clear();
  assertResult(PlayResult::Ok, rig.play("\"boost\""), "melody last");
  TEST_ASSERT_EQUAL_STRING("boost", rig.tone.asked.back().c_str());
}

// "Script/name" is that folder alone.
void test_a_script_path_never_falls_back(void) {
  Rig rig;
  rig.assets.stored.push_back("boost");
  assertResult(PlayResult::NotFound, rig.play("\"racer/boost\""), "no shared fallback");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  rig.assets.files.push_back("/SCRIPTS/racer/boost.mp3");
  assertResult(PlayResult::Ok, rig.play("\"racer/boost\""), "the folder's own file");
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/boost.mp3", rig.pcm.mp3s.back().first.c_str());
}

void test_a_missing_name_says_what_it_looked_for(void) {
  Rig rig;
  assertResult(PlayResult::NotFound, rig.play("\"nope\""), "nothing stored");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, rig.detail.message.find("nope"));
}

// Every path the probe or a sink sees is one the name rule built: a caller that is no valid
// script name never reaches a script folder.
void test_names_that_could_leave_a_folder_are_never_looked_up(void) {
  Rig rig;
  for (const char* owner : {"..", "a/b", "."}) {
    rig.assets.asked.clear();
    rig.play("\"boost\"", Group::App, owner);
    TEST_ASSERT_EQUAL_size_t(2, rig.assets.asked.size());
    TEST_ASSERT_EQUAL_STRING("/MP3/boost.mp3", rig.assets.asked[0].c_str());
    TEST_ASSERT_EQUAL_STRING("/MELODIES/boost.txt", rig.assets.asked[1].c_str());
  }
  Choices choices;
  DispatchDetail detail;
  for (const char* json : {"\"../boost\"", "\"racer/../x\"", "\"boost.mp3\"", "\"a/b/c\""})
    TEST_ASSERT_FALSE_MESSAGE(sound::parse(json, Origin::Play, choices, detail), json);
  TEST_ASSERT_TRUE(rig.pcm.mp3s.empty());
}

// An app sound under an alert is dropped, but only one that exists: a name with nothing behind it
// is the same mistake at any moment.
void test_a_missing_melody_is_not_found_under_an_alert(void) {
  Rig rig;
  rig.assets.stored = {"ding"};
  rig.play("\"ding\"");
  assertResult(PlayResult::NotFound, rig.play("\"nope\"", Group::App, "racer"), "nothing called");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  rig.tone.stored = {"tune"};
  assertResult(PlayResult::Ok, rig.play("\"tune\"", Group::App, "racer"), "a melody is dropped");
  TEST_ASSERT_TRUE(rig.tone.asked.empty());
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
}

// ---- Missing hardware ----------------------------------------------------------

void test_each_kind_needs_its_output(void) {
  Rig rig;
  rig.router.setTone(nullptr);
  assertResult(PlayResult::NoSink, rig.play("{\"rtttl\":\"x:d=4,o=5,b=120:c\"}"), "rtttl");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  TEST_ASSERT_TRUE(rig.detail.field.empty());
  rig.router.setTrack(nullptr);
  assertResult(PlayResult::NoSink, rig.play("{\"track\":7}"), "track");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  rig.router.setPcm(nullptr);
  assertResult(PlayResult::NoSink, rig.play("\"racer/boost\""), "script path");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  assertResult(PlayResult::NoSink, rig.play("\"ding\""), "a name");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  TEST_ASSERT_TRUE(rig.pcm.mp3s.empty() && rig.track.played.empty());
}

// A melody the parser accepted, or one that is stored, is never a mistake: while the speaker is
// taken it cannot play now.
void test_a_melody_the_speaker_cannot_take_is_unavailable(void) {
  Rig rig;
  rig.tone.ready = false;
  rig.tone.stored = {"ding"};
  assertResult(PlayResult::NoSink, rig.play("{\"rtttl\":\"x:d=4,o=5,b=120:c\"}"), "rtttl");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  TEST_ASSERT_TRUE(rig.detail.field.empty());
  assertResult(PlayResult::NoSink, rig.play("\"ding\""), "stored melody");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  assertResult(PlayResult::NotFound, rig.play("\"nope\""), "no melody");
}

// A bare router routes nowhere, and says so rather than crashing.
void test_a_router_without_sinks_is_harmless(void) {
  AudioRouter router;
  DispatchDetail detail;
  assertResult(PlayResult::NoSink, router.play(parsed("\"ding\"", Origin::Play), Group::Alert, "",
                                               detail),
               "a name, no sinks");
  router.release("/SCRIPTS/racer");
  router.stop(Stop::All);
  router.tick(1000);
  TEST_ASSERT_FALSE(router.alertPlaying());
  TEST_ASSERT_FALSE(router.appSoundPlaying());
}

// ---- Volumes -------------------------------------------------------------------

void test_volumes_are_master_times_group_and_reach_every_sink(void) {
  Rig rig;
  rig.router.setVolumes(50, 80, 100, 60);
  TEST_ASSERT_EQUAL_UINT8(30, rig.pcm.volumes.alert);
  TEST_ASSERT_EQUAL_UINT8(50, rig.pcm.volumes.app);
  TEST_ASSERT_EQUAL_UINT8(40, rig.pcm.volumes.radio);
  TEST_ASSERT_EQUAL_UINT8(30, rig.tone.volumes.alert);
  TEST_ASSERT_EQUAL_UINT8(30, rig.track.volumes.alert);
  rig.router.setVolumes(50, 80, 100, 60);
  TEST_ASSERT_EQUAL_INT(1, rig.track.volumeWrites);
}

// A sink attached after the settings were applied hears them at once.
void test_a_late_sink_gets_the_current_volumes(void) {
  AudioRouter router;
  router.setVolumes(100, 50, 100, 100);
  FakePcm pcm;
  router.setPcm(&pcm);
  TEST_ASSERT_EQUAL_UINT8(50, pcm.volumes.radio);
}

// ---- Groups --------------------------------------------------------------------

void test_the_group_reaches_the_sink(void) {
  Rig rig;
  rig.assets.stored.push_back("ding");
  rig.play("\"ding\"");
  TEST_ASSERT_EQUAL_INT((int)Group::Alert, (int)rig.pcm.mp3s.back().second);
  rig.pcm.mp3Running = false;
  rig.play("\"ding\"", Group::App, "racer");
  TEST_ASSERT_EQUAL_INT((int)Group::App, (int)rig.pcm.mp3s.back().second);
  rig.pcm.mp3Running = false;
  rig.play("{\"track\":3}", Group::App, "racer");
  TEST_ASSERT_EQUAL_INT((int)Group::App, (int)rig.track.groups.back());
  rig.play("{\"rtttl\":\"a:d=4,o=5,b=120:c\"}");
  TEST_ASSERT_EQUAL_INT((int)Group::Alert, (int)rig.tone.groups.back());
}

// An app sound never cuts an alert off; it is taken and dropped.
void test_an_app_one_shot_waits_for_no_alert(void) {
  Rig rig;
  rig.assets.stored.push_back("ding");
  rig.play("\"ding\"");
  assertResult(PlayResult::Ok, rig.play("{\"rtttl\":\"a:d=4,o=5,b=120:c\"}", Group::App, "racer"),
               "accepted");
  TEST_ASSERT_EQUAL_INT(0, (int)rig.tone.played.size());
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
}

// On a mixer the app's effects and music still start under an alert; the sink ducks them.
void test_app_layers_start_under_an_alert_on_a_mixer(void) {
  Rig rig;
  rig.pcm.mixing = true;
  rig.assets.stored.push_back("ding");
  rig.assets.stored.push_back("theme");
  rig.play("\"ding\"");
  rig.router.playEffect(parsed("\"ding\"", Origin::Script), "racer", rig.detail);
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  TEST_ASSERT_EQUAL_INT(1, (int)rig.pcm.effects.size());
  TEST_ASSERT_EQUAL_INT(1, (int)rig.pcm.loops.size());
}

void test_an_alert_replaces_an_app_one_shot(void) {
  Rig rig;
  rig.assets.stored.push_back("ding");
  rig.play("\"ding\"", Group::App, "racer");
  rig.play("\"ding\"");
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  TEST_ASSERT_FALSE(rig.router.appSoundPlaying());
}

// One sound at a time: a one-shot silences the other outputs, never its own.
void test_a_one_shot_silences_the_other_outputs(void) {
  Rig rig;
  rig.assets.stored = {"ding"};
  rig.tone.stored = {"alarm"};
  assertResult(PlayResult::Ok, rig.play("\"ding\""), "mp3");
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  assertResult(PlayResult::Ok, rig.play("\"alarm\""), "melody");
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.streamStops);
}

void test_a_failed_lookup_leaves_a_playing_sound_alone(void) {
  Rig rig;
  rig.assets.stored = {"ding"};
  assertResult(PlayResult::Ok, rig.play("\"ding\""), "mp3");
  assertResult(PlayResult::NotFound, rig.play("\"nope\""), "unknown name");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_TRUE(rig.pcm.oneShotPlaying());
}

// ---- Stopping ------------------------------------------------------------------

// stop(ScriptSounds) takes only what that script started.
void test_a_script_stops_only_its_own_sounds(void) {
  Rig rig;
  rig.pcm.mixing = true;
  rig.assets.stored.push_back("theme");
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  rig.router.stop(Stop::ScriptSounds, "other");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.loopStops);
  rig.router.stop(Stop::ScriptSounds, "racer");
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.loopStops);
  TEST_ASSERT_FALSE(rig.router.appStatus().playing);
}

void test_a_script_stops_its_effects_and_one_shot_but_no_alert(void) {
  Rig rig;
  rig.pcm.mixing = true;
  rig.assets.stored = {"boom", "ding"};
  rig.effect("\"boom\"", "racer");
  rig.play("\"ding\"");
  rig.router.stop(Stop::ScriptSounds, "racer");
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.effectStops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  rig.pcm.mp3Running = false;
  rig.play("\"ding\"", Group::App, "racer");
  rig.router.stop(Stop::ScriptSounds, "other");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  rig.router.stop(Stop::ScriptSounds, "racer");
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
}

// Whoever starts the music last owns it.
void test_whoever_plays_the_music_last_owns_it(void) {
  Rig rig;
  rig.pcm.mixing = rig.pcm.synth = true;
  rig.play("{\"song\":\"a\",\"loop\":true}", Group::App, "racer");
  rig.play("{\"song\":\"b\",\"loop\":true}", Group::App, "other");
  rig.router.stop(Stop::ScriptMusic, "racer");
  rig.router.stop(Stop::ScriptSounds, "racer");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.loopStops);
  rig.router.stop(Stop::ScriptMusic, "other");
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.loopStops);
}

// Once a script's one-shot ended, a sound the router did not start (the TC002's boot sound) is
// the device's own alert, and no script stops it.
void test_an_ended_one_shot_leaves_no_owner(void) {
  Rig rig;
  rig.assets.stored = {"ding"};
  rig.play("\"ding\"", Group::App, "racer");
  rig.pcm.mp3Running = false;
  rig.router.tick(10);
  rig.pcm.mp3Running = true;
  rig.router.stop(Stop::ScriptSounds, "racer");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  TEST_ASSERT_FALSE(rig.router.appSoundPlaying());
}

void test_stopped_music_has_no_owner(void) {
  Rig rig;
  rig.pcm.mixing = rig.pcm.synth = true;
  for (Stop what : {Stop::App, Stop::All}) {
    rig.play("{\"song\":\"a\",\"loop\":true}", Group::App, "racer");
    rig.router.stop(what);
    const int stops = rig.pcm.loopStops;
    rig.router.stop(Stop::ScriptMusic, "racer");
    TEST_ASSERT_EQUAL_INT(stops, rig.pcm.loopStops);
  }
}

void test_stop_by_group(void) {
  Rig rig;
  rig.assets.stored.push_back("ding");
  rig.router.playStream("http://x/", "X", rig.detail);
  rig.play("\"ding\"");
  rig.router.stop(Stop::App);
  TEST_ASSERT_TRUE(rig.pcm.mp3Running);
  rig.router.stop(Stop::Alert);
  TEST_ASSERT_FALSE(rig.pcm.mp3Running);
  TEST_ASSERT_TRUE(rig.pcm.streamRunning);
  rig.router.stop(Stop::Radio);
  TEST_ASSERT_FALSE(rig.pcm.streamRunning);
}

void test_stop_all_stops_everything(void) {
  Rig rig;
  rig.router.stop(Stop::All);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.effectStops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.loopStops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.streamStops);
}

// ---- Repeating -----------------------------------------------------------------

// A one-shot with loop starts again after it ended, at most once per 250 ms.
void test_a_looping_one_shot_repeats_until_stopped(void) {
  Rig rig;
  rig.assets.stored.push_back("siren");
  rig.play("{\"file\":\"siren\",\"loop\":true}");
  rig.pcm.mp3Running = false;
  rig.router.tick(1000);
  TEST_ASSERT_EQUAL_INT(2, (int)rig.pcm.mp3s.size());
  rig.pcm.mp3Running = false;
  rig.router.tick(1100);
  TEST_ASSERT_EQUAL_INT(2, (int)rig.pcm.mp3s.size());
  rig.router.tick(1250);
  TEST_ASSERT_EQUAL_INT(3, (int)rig.pcm.mp3s.size());
  rig.router.stop(Stop::Alert);
  rig.pcm.mp3Running = false;
  rig.router.tick(2000);
  TEST_ASSERT_EQUAL_INT(3, (int)rig.pcm.mp3s.size());
}

// Between two plays the alert still plays: its status does not blink and no app sound takes the
// gap.
void test_a_repeating_alert_counts_between_its_plays(void) {
  Rig rig;
  rig.assets.stored = {"siren", "ding"};
  rig.play("{\"file\":\"siren\",\"loop\":true}");
  rig.router.takeStatusChanged();
  rig.pcm.mp3Running = false;
  rig.router.tick(100);
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  TEST_ASSERT_TRUE(rig.router.alertStatus().playing);
  TEST_ASSERT_FALSE(rig.router.takeStatusChanged());
  assertResult(PlayResult::Ok, rig.play("\"ding\"", Group::App, "racer"), "taken and dropped");
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.mp3s.size());
  rig.router.tick(300);
  TEST_ASSERT_EQUAL_size_t(2, rig.pcm.mp3s.size());
  TEST_ASSERT_EQUAL_STRING("/MP3/siren.mp3", rig.pcm.mp3s.back().first.c_str());
  TEST_ASSERT_FALSE(rig.router.takeStatusChanged());
}

// The same for an app's music on a clock without a mixer: sound.playing() stays true.
void test_a_repeating_app_sound_counts_between_its_plays(void) {
  Rig rig;
  rig.assets.stored = {"theme"};
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  rig.pcm.mp3Running = false;
  rig.router.tick(100);
  TEST_ASSERT_TRUE(rig.router.appSoundPlaying());
  TEST_ASSERT_TRUE(rig.router.appStatus().playing);
}

// A speaker that is taken for a while does not end the repeat; it plays again once it is free.
void test_a_busy_speaker_keeps_a_repeat_waiting(void) {
  Rig rig;
  rig.play("{\"rtttl\":\"x:d=8,o=5,b=120:c\",\"loop\":true}");
  rig.tone.playing = false;
  rig.tone.ready = false;
  rig.router.tick(1000);
  TEST_ASSERT_EQUAL_size_t(1, rig.tone.played.size());
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  rig.tone.ready = true;
  rig.router.tick(1250);
  TEST_ASSERT_EQUAL_size_t(2, rig.tone.played.size());
}

// A looping sound from an address that cannot be fetched is not fetched again and again.
void test_a_looping_address_that_fails_ends_its_repeat(void) {
  Rig rig;
  rig.pcm.fetching = true;
  rig.play("{\"file\":\"https://x.de/a.mp3\",\"loop\":true}");
  rig.pcm.mp3Running = false;
  rig.pcm.pendingUrlError = "HTTP 404";
  rig.router.tick(1000);
  rig.router.tick(2000);
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.urls.size());
  TEST_ASSERT_FALSE(rig.router.alertPlaying());
  TEST_ASSERT_EQUAL_STRING("HTTP 404", rig.router.alertStatus().error.c_str());
}

// The station stays away while a one-shot repeats, and comes back once the repeat ends.
void test_the_stream_is_held_while_a_one_shot_repeats(void) {
  Rig rig;
  rig.assets.stored = {"siren", "ding"};
  rig.play("{\"file\":\"siren\",\"loop\":true}");
  rig.pcm.mp3Running = false;
  rig.router.tick(1000);
  rig.play("\"ding\"");
  rig.play("{\"file\":\"siren\",\"loop\":true}");
  rig.router.stop(Stop::Alert);
  const std::vector<bool> expected = {true, false, true, false};
  TEST_ASSERT_TRUE(rig.pcm.holds == expected);
}

// A buzzer or a DFPlayer is no speaker the station needs: a melody repeating there holds nothing,
// unless the melodies come out of the station's own speaker.
void test_a_repeating_melody_holds_the_stream_only_on_a_shared_speaker(void) {
  Rig rig;
  rig.tone.stored = {"bell"};
  rig.assets.files = {"/MELODIES/bell.txt"};
  rig.play("{\"file\":\"bell\",\"loop\":true}");
  TEST_ASSERT_TRUE(rig.pcm.holds.empty());
  rig.router.stop(Stop::Alert);
  rig.tone.shared = true;
  rig.play("{\"file\":\"bell\",\"loop\":true}");
  const std::vector<bool> held = {true};
  TEST_ASSERT_TRUE(rig.pcm.holds == held);
}

// The voice replaces a script's sound with its answer: that is an alert, and the script's
// sound neither owns it nor comes back as the answer's repeat.
void test_a_sound_the_sink_replaced_by_itself_is_over(void) {
  Rig rig;
  rig.pcm.reportsGroup = true;
  rig.assets.stored = {"theme"};
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  rig.pcm.playingGroup = Group::App;
  TEST_ASSERT_TRUE(rig.router.appStatus().playing);
  rig.pcm.playingGroup = Group::Alert;
  rig.router.tick(1000);
  TEST_ASSERT_TRUE(rig.router.alertStatus().playing);
  TEST_ASSERT_FALSE(rig.router.appStatus().playing);
  rig.pcm.mp3Running = false;
  rig.router.tick(2000);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(rig.pcm.mp3s.size()));
}

// A repeating alert keeps one token across its plays; a stop by an older token ends nothing.
void test_a_repeating_alert_is_stopped_by_its_own_token_only(void) {
  Rig rig;
  rig.assets.stored = {"siren"};
  rig.play("{\"file\":\"siren\",\"loop\":true}");
  const uint32_t first = rig.router.repeatingAlert();
  TEST_ASSERT_NOT_EQUAL(0, first);
  rig.pcm.mp3Running = false;
  rig.router.tick(1000);
  TEST_ASSERT_EQUAL_size_t(2, rig.pcm.mp3s.size());
  TEST_ASSERT_EQUAL_UINT32(first, rig.router.repeatingAlert());
  rig.play("{\"file\":\"siren\",\"loop\":true}");
  const uint32_t second = rig.router.repeatingAlert();
  TEST_ASSERT_NOT_EQUAL(first, second);
  rig.router.stopRepeatingAlert(first);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  rig.router.stopRepeatingAlert(second);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
  TEST_ASSERT_FALSE(rig.router.alertPlaying());
  rig.play("\"siren\"");
  TEST_ASSERT_EQUAL_UINT32(0, rig.router.repeatingAlert());
}

// Without a mixer an app's music is its one-shot, repeating.
void test_music_without_a_mixer_repeats_the_one_shot(void) {
  Rig rig;
  rig.assets.stored.push_back("theme");
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  TEST_ASSERT_EQUAL_INT(0, (int)rig.pcm.loops.size());
  rig.pcm.mp3Running = false;
  rig.router.tick(1000);
  TEST_ASSERT_EQUAL_INT(2, (int)rig.pcm.mp3s.size());
  rig.router.stop(Stop::ScriptMusic, "racer");
  rig.pcm.mp3Running = false;
  rig.router.tick(2000);
  TEST_ASSERT_EQUAL_INT(2, (int)rig.pcm.mp3s.size());
}

// Without a mixer an effect is a one-shot that never repeats.
void test_an_effect_without_a_mixer_plays_once(void) {
  Rig rig;
  rig.assets.stored = {"boom"};
  assertResult(PlayResult::Ok, rig.effect("{\"file\":\"boom\",\"loop\":true}", "racer"), "effect");
  TEST_ASSERT_EQUAL_STRING("/MP3/boom.mp3", rig.pcm.mp3s[0].first.c_str());
  TEST_ASSERT_EQUAL_INT((int)Group::App, (int)rig.pcm.mp3s[0].second);
  TEST_ASSERT_TRUE(rig.pcm.effects.empty());
  TEST_ASSERT_TRUE(rig.router.appSoundPlaying());
  rig.pcm.mp3Running = false;
  rig.router.tick(1000);
  TEST_ASSERT_EQUAL_INT(1, (int)rig.pcm.mp3s.size());
}

// ---- Lists ---------------------------------------------------------------------

void test_a_list_plays_its_first_playable_entry(void) {
  Rig rig;
  rig.assets.stored.push_back("ding");
  assertResult(PlayResult::Ok, rig.play("[{\"speech\":\"Door\"},\"ding\"]"), "falls to ding");
  TEST_ASSERT_EQUAL_STRING("/MP3/ding.mp3", rig.pcm.mp3s.back().first.c_str());
  assertResult(PlayResult::NoSink, rig.play("[{\"speech\":\"Door\"},{\"song\":\"bpm 9\"}]"),
               "nothing playable");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
}

// A mistake in an entry answers at once instead of trying the next.
void test_a_list_stops_at_a_mistake(void) {
  Rig rig;
  rig.pcm.synth = true;
  rig.assets.stored.push_back("ding");
  assertResult(PlayResult::Invalid, rig.play("[{\"song\":\"bad\"},\"ding\"]"), "song mistake");
  TEST_ASSERT_EQUAL_INT(0, (int)rig.pcm.mp3s.size());
}

// ---- Status --------------------------------------------------------------------

void test_status_names_what_each_group_plays(void) {
  Rig rig;
  rig.assets.stored.push_back("ding");
  rig.play("\"ding\"");
  TEST_ASSERT_TRUE(rig.router.takeStatusChanged());
  TEST_ASSERT_TRUE(rig.router.alertStatus().playing);
  TEST_ASSERT_EQUAL_STRING("ding", rig.router.alertStatus().name.c_str());
  TEST_ASSERT_FALSE(rig.router.takeStatusChanged());
  rig.pcm.mp3Running = false;
  rig.router.tick(10);
  TEST_ASSERT_TRUE(rig.router.takeStatusChanged());
  TEST_ASSERT_FALSE(rig.router.alertStatus().playing);
}

void test_a_url_error_lands_in_the_group_that_asked(void) {
  Rig rig;
  rig.pcm.fetching = true;
  rig.play("\"https://x.de/a.mp3\"");
  rig.pcm.pendingUrlError = "HTTP 404";
  rig.router.tick(10);
  TEST_ASSERT_EQUAL_STRING("HTTP 404", rig.router.alertStatus().error.c_str());
  TEST_ASSERT_TRUE(rig.router.appStatus().error.empty());
  rig.assets.stored.push_back("ding");
  rig.play("\"ding\"");
  TEST_ASSERT_TRUE(rig.router.alertStatus().error.empty());
}

// An alert's address and the app's music from an address fail apart, each under its own group.
void test_a_url_error_lands_in_the_layer_that_failed(void) {
  Rig rig;
  rig.pcm.fetching = rig.pcm.mixing = true;
  rig.play("\"https://x.de/a.mp3\"");
  rig.play("{\"file\":\"https://x.de/m.mp3\",\"loop\":true}", Group::App, "racer");
  rig.pcm.pendingUrlError = "HTTP 404";
  rig.router.tick(10);
  TEST_ASSERT_EQUAL_STRING("HTTP 404", rig.router.alertStatus().error.c_str());
  TEST_ASSERT_TRUE(rig.router.appStatus().error.empty());
  rig.pcm.pendingUrlError = "HTTP 500";
  rig.pcm.pendingUrlMusic = true;
  rig.router.tick(20);
  TEST_ASSERT_EQUAL_STRING("HTTP 500", rig.router.appStatus().error.c_str());
  TEST_ASSERT_EQUAL_STRING("HTTP 404", rig.router.alertStatus().error.c_str());
}

// Music the sink dropped by itself (the voice took the speaker, its file went, it failed) no
// longer plays for the status and has no owner left.
void test_music_the_sink_dropped_is_over(void) {
  Rig rig;
  rig.pcm.mixing = true;
  rig.assets.stored = {"theme"};
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  TEST_ASSERT_TRUE(rig.router.appStatus().playing);
  rig.pcm.loopRunning = false;
  rig.router.tick(10);
  TEST_ASSERT_FALSE(rig.router.appStatus().playing);
  rig.router.stop(Stop::ScriptMusic, "racer");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.loopStops);
}

// ---- The stream ----------------------------------------------------------------

void test_a_station_ends_app_music(void) {
  Rig rig;
  rig.pcm.mixing = true;
  rig.assets.stored.push_back("theme");
  rig.play("{\"file\":\"theme\",\"loop\":true}", Group::App, "racer");
  rig.router.playStream("http://x/", "X", rig.detail);
  TEST_ASSERT_FALSE(rig.router.appStatus().playing);
}

void test_a_stream_needs_the_pcm_sink(void) {
  Rig rig;
  rig.router.setPcm(nullptr);
  assertResult(DispatchResult::Unavailable, rig.router.playStream("http://x/", "X", rig.detail),
               "no pcm");
}

void test_tick_pumps_every_sink(void) {
  Rig rig;
  rig.router.tick(1000);
  rig.router.tick(2000);
  TEST_ASSERT_EQUAL_INT(2, rig.tone.ticks);
  TEST_ASSERT_EQUAL_INT(2, rig.track.ticks);
  TEST_ASSERT_EQUAL_INT(2, rig.pcm.ticks);
}

// The files are the PCM output's to let go of; nothing else reads a file while it plays.
void test_a_release_reaches_the_output_that_holds_files(void) {
  Rig rig;
  rig.router.release("/SCRIPTS/racer");
  rig.router.release("/SCRIPTS/racer/boost.mp3");
  rig.router.release("");
  TEST_ASSERT_EQUAL_size_t(2, rig.pcm.releases.size());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer", rig.pcm.releases[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/SCRIPTS/racer/boost.mp3", rig.pcm.releases[1].c_str());
  TEST_ASSERT_EQUAL_INT(0, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
}

// ---- Capabilities --------------------------------------------------------------

void test_caps_and_can_play(void) {
  Rig rig;
  sound::Caps c = rig.router.caps();
  TEST_ASSERT_TRUE(c.mp3 && c.rtttl && c.track && c.radio);
  TEST_ASSERT_FALSE(c.song || c.speech || c.effect || c.url || c.clip);
  TEST_ASSERT_FALSE(rig.router.canPlay(parsed("{\"speech\":\"Hi\"}", Origin::Play)));
  TEST_ASSERT_TRUE(rig.router.canPlay(parsed("[{\"speech\":\"Hi\"},\"ding\"]", Origin::Play)));
}

void test_caps_follow_the_attached_sinks(void) {
  Rig rig;
  rig.pcm.synth = rig.pcm.speaking = rig.pcm.mixing = rig.pcm.fetching = rig.pcm.clipping = true;
  sound::Caps c = rig.router.caps();
  TEST_ASSERT_TRUE(c.song && c.speech && c.effect && c.url && c.clip);
  rig.router.setTrack(nullptr);
  rig.router.setPcm(nullptr);
  c = rig.router.caps();
  TEST_ASSERT_TRUE(c.rtttl);
  TEST_ASSERT_FALSE(c.track || c.mp3 || c.radio || c.song || c.url);
  TEST_ASSERT_FALSE(rig.router.canPlay(parsed("\"https://x.de/a.mp3\"", Origin::Play)));
  TEST_ASSERT_FALSE(rig.router.canPlay(parsed("\"racer/boost\"", Origin::Play)));
  TEST_ASSERT_TRUE(rig.router.canPlay(parsed("\"ding\"", Origin::Play)));
}

void test_check_names_the_field_of_a_bad_song_in_a_list(void) {
  Rig rig;
  rig.pcm.synth = true;
  TEST_ASSERT_FALSE(rig.router.check(parsed("[\"ding\",{\"song\":\"bad\"}]", Origin::Notification),
                                     Origin::Notification, rig.detail));
  TEST_ASSERT_EQUAL_STRING("sound[1].song", rig.detail.field.c_str());
}

// Where nothing speaks or synthesizes, the text is not judged here; it simply finds no output.
void test_check_judges_only_where_an_output_exists(void) {
  Rig rig;
  DispatchDetail detail;
  TEST_ASSERT_TRUE(rig.router.check(parsed("{\"speech\":\"bad\"}", Origin::Play), Origin::Play,
                                    detail));
  TEST_ASSERT_TRUE(rig.router.check(parsed("{\"song\":\"bad\"}", Origin::Play), Origin::Play,
                                    detail));
  rig.pcm.speaking = true;
  TEST_ASSERT_TRUE(rig.router.check(parsed("{\"speech\":\"Hi\"}", Origin::Play), Origin::Play,
                                    detail));
  TEST_ASSERT_FALSE(rig.router.check(parsed("{\"speech\":\"bad\"}", Origin::Notification),
                                     Origin::Notification, detail));
  TEST_ASSERT_EQUAL_STRING("sound.speech", detail.field.c_str());
  TEST_ASSERT_TRUE(detail.message.c_str()[0] != '\0');
}

// ---- Songs ---------------------------------------------------------------------

void test_a_song_needs_a_synthesizer(void) {
  Rig rig;
  rig.pcm.mixing = true;
  assertResult(PlayResult::NoSink, rig.play("{\"song\":\"ok\"}"), "song without synth");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  assertResult(PlayResult::NoSink, rig.effect("{\"song\":\"ok\"}", "racer"), "effect");
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.songs.size() + rig.pcm.fxs.size() +
                                  rig.pcm.songsOnce.size());
}

// Music on the loop layer, an effect over it, or once as the one-shot of its group.
void test_songs_reach_the_synthesizer_as_music_effect_or_once(void) {
  Rig rig;
  rig.pcm.mixing = rig.pcm.synth = true;
  assertResult(PlayResult::Ok, rig.play("{\"song\":\"a\",\"loop\":true}", Group::App, "racer"),
               "music");
  assertResult(PlayResult::Ok,
               rig.play("{\"song\":\"b\",\"loop\":true,\"nextBar\":true}", Group::App, "racer"),
               "music on the bar");
  assertResult(PlayResult::Ok, rig.effect("{\"song\":\"c\"}", "racer"), "effect");
  assertResult(PlayResult::Ok, rig.play("{\"song\":\"d\"}"), "once");
  TEST_ASSERT_EQUAL_size_t(2, rig.pcm.songs.size());
  TEST_ASSERT_FALSE(rig.pcm.songs[0].second);
  TEST_ASSERT_TRUE(rig.pcm.songs[1].second);
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.fxs.size());
  TEST_ASSERT_EQUAL_STRING("c", rig.pcm.fxs[0].c_str());
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.songsOnce.size());
  TEST_ASSERT_EQUAL_INT((int)Group::Alert, (int)rig.pcm.songsOnce[0].second);
}

// Music and effects are layers: they silence nothing else.
void test_app_layers_leave_the_one_shot_alone(void) {
  Rig rig;
  rig.pcm.mixing = rig.pcm.synth = true;
  rig.assets.stored = {"ding"};
  rig.play("\"ding\"");
  const int toneStops = rig.tone.stops, trackStops = rig.track.stops;
  rig.play("{\"song\":\"a\",\"loop\":true}", Group::App, "racer");
  rig.effect("{\"song\":\"b\"}", "racer");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(toneStops, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(trackStops, rig.track.stops);
  TEST_ASSERT_TRUE(rig.pcm.mp3Running);
}

void test_a_bad_song_is_invalid_with_its_reason(void) {
  Rig rig;
  rig.pcm.mixing = rig.pcm.synth = true;
  assertResult(PlayResult::Invalid, rig.play("{\"song\":\"bad\"}"), "bad song");
  TEST_ASSERT_EQUAL_STRING("song", rig.detail.field.c_str());
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.songs.size() + rig.pcm.fxs.size() +
                                  rig.pcm.songsOnce.size());
}

void test_a_busy_speaker_plays_no_song(void) {
  Rig rig;
  rig.pcm.mixing = rig.pcm.synth = true;
  rig.pcm.speakerReady = false;
  assertResult(PlayResult::NoSink, rig.play("{\"song\":\"a\",\"loop\":true}", Group::App, "racer"),
               "busy music");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  assertResult(PlayResult::NoSink, rig.effect("{\"song\":\"a\"}", "racer"), "busy effect");
  assertResult(PlayResult::NoSink, rig.play("{\"song\":\"a\"}"), "busy once");
}

// ---- Addresses -----------------------------------------------------------------

void test_an_address_is_fetched_not_looked_up(void) {
  Rig rig;
  rig.pcm.fetching = rig.pcm.mixing = true;
  rig.tone.playing = true;
  assertResult(PlayResult::Ok, rig.play("\"https://a.example/ding.mp3\""), "alert");
  assertResult(PlayResult::Ok,
               rig.play("{\"file\":\"http://a.example:8080/rain.mp3?x=1\",\"loop\":true}",
                        Group::App, "racer"),
               "app music");
  TEST_ASSERT_EQUAL_size_t(2, rig.pcm.urls.size());
  TEST_ASSERT_EQUAL_STRING("https://a.example/ding.mp3", std::get<0>(rig.pcm.urls[0]).c_str());
  TEST_ASSERT_EQUAL_INT((int)Group::Alert, (int)std::get<1>(rig.pcm.urls[0]));
  TEST_ASSERT_FALSE(std::get<2>(rig.pcm.urls[0]));
  TEST_ASSERT_EQUAL_INT((int)Group::App, (int)std::get<1>(rig.pcm.urls[1]));
  TEST_ASSERT_TRUE(std::get<2>(rig.pcm.urls[1]));
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_TRUE(rig.pcm.mp3s.empty() && rig.pcm.loops.empty());
  TEST_ASSERT_TRUE(rig.assets.asked.empty());
}

void test_an_address_is_refused_where_it_cannot_play(void) {
  Rig rig;
  assertResult(PlayResult::NoSink, rig.play("\"https://a.example/x.mp3\""), "no fetch");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  assertResult(PlayResult::Invalid, rig.play("\"https://\""), "bad address");
  TEST_ASSERT_EQUAL_STRING("file", rig.detail.field.c_str());
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  rig.pcm.fetching = true;
  rig.pcm.speakerReady = false;
  rig.tone.playing = true;
  assertResult(PlayResult::NoSink, rig.play("\"https://a.example/x.mp3\""), "busy");
  TEST_ASSERT_EQUAL_INT(0, rig.tone.stops);
  TEST_ASSERT_TRUE(rig.pcm.urls.empty());
}

// ---- Clips ---------------------------------------------------------------------

void test_a_clip_plays_as_the_alert_one_shot(void) {
  Rig rig;
  rig.pcm.clipping = true;
  rig.tone.playing = rig.track.playing = true;
  const std::string bytes("RIFF\0\1", 6);
  assertResult(DispatchResult::Ok, rig.clip(bytes), "clip");
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.clipsPlayed.size());
  TEST_ASSERT_TRUE(rig.pcm.clipsPlayed[0] == bytes);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  TEST_ASSERT_EQUAL_STRING("clip", rig.router.alertStatus().name.c_str());
  TEST_ASSERT_TRUE(rig.assets.asked.empty());

  std::string big(4096, 'x');
  const char* const at = big.data();
  assertResult(DispatchResult::Ok, tc002::playClip(&rig.pcm, rig.router, std::move(big), rig.detail), "big clip");
  TEST_ASSERT_TRUE(rig.pcm.clipsPlayed[1].data() == at);
}

void test_a_bad_clip_is_invalid_and_stops_nothing(void) {
  Rig rig;
  rig.pcm.clipping = true;
  rig.tone.playing = true;
  assertResult(DispatchResult::ValidationError, rig.clip("bad bytes"), "bad clip");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  TEST_ASSERT_TRUE(rig.pcm.clipsPlayed.empty());
  TEST_ASSERT_EQUAL_INT(0, rig.tone.stops);
}

// The sink judges a clip, so without one even garbage answers "unavailable".
void test_a_clip_needs_a_sink_that_plays_clips(void) {
  Rig rig;
  assertResult(DispatchResult::Unavailable, rig.clip("bad bytes"), "no clip sink");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  rig.router.setPcm(nullptr);
  assertResult(DispatchResult::Unavailable, tc002::playClip<FakePcm>(nullptr, rig.router, "RIFF", rig.detail), "no pcm sink");
}

void test_a_busy_speaker_plays_no_clip(void) {
  Rig rig;
  rig.pcm.clipping = true;
  rig.pcm.speakerReady = false;
  assertResult(DispatchResult::Unavailable, rig.clip("RIFF"), "busy clip");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
}

void test_platform_clip_route_validates_method_body_and_transfers_recording(void) {
  Rig rig;
  rig.pcm.clipping = true;
  std::string response;
  const std::string bytes("RIFF\0\0\0\0WAVEdata", 16);
  TEST_ASSERT_EQUAL_INT(0, tc002::routeClip("POST", "/other", bytes, response, &rig.pcm, rig.router));
  TEST_ASSERT_EQUAL_INT(405, tc002::routeClip("GET", "/api/v1/audio/clip", bytes, response, &rig.pcm, rig.router));
  TEST_ASSERT_EQUAL_INT(422, tc002::routeClip("POST", "/api/v1/audio/clip", "", response, &rig.pcm, rig.router));
  TEST_ASSERT_TRUE(rig.pcm.clipsPlayed.empty());
  TEST_ASSERT_EQUAL_INT(200, tc002::routeClip("POST", "/api/v1/audio/clip", bytes, response, &rig.pcm, rig.router));
  TEST_ASSERT_TRUE(rig.pcm.clipsPlayed.back() == bytes);
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  TEST_ASSERT_EQUAL_INT(422, tc002::routeClip("POST", "/api/v1/audio/clip", "bad clip", response, &rig.pcm, rig.router));
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
  TEST_ASSERT_EQUAL_INT(503, tc002::routeClip<FakePcm>("POST", "/api/v1/audio/clip", bytes, response, nullptr, rig.router));
}

// ---- Speech --------------------------------------------------------------------

void test_speech_needs_a_sink_that_speaks(void) {
  Rig rig;
  assertResult(PlayResult::NoSink, rig.play("{\"speech\":\"hello\"}"), "no voice");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  rig.pcm.speaking = true;
  assertResult(PlayResult::Ok, rig.play("{\"speech\":\"hello\"}", Group::App, "racer"), "voice");
  TEST_ASSERT_EQUAL_STRING("hello", rig.pcm.speechRequests[0].first.c_str());
  TEST_ASSERT_EQUAL_INT((int)Group::App, (int)rig.pcm.speechRequests[0].second);
}

void test_speech_the_sink_refuses_plays_nothing(void) {
  Rig rig;
  rig.pcm.speaking = true;
  rig.tone.stored = {"ding"};
  rig.play("\"ding\"");
  assertResult(PlayResult::Invalid, rig.play("{\"speech\":\"bad\"}"), "bad speech");
  TEST_ASSERT_EQUAL_STRING("speech", rig.detail.field.c_str());
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
  TEST_ASSERT_TRUE(rig.pcm.speechRequests.empty());
  TEST_ASSERT_TRUE(rig.tone.playing);
}

void test_speech_replaces_a_melody(void) {
  Rig rig;
  rig.pcm.speaking = true;
  rig.tone.stored = {"ding"};
  rig.play("\"ding\"");
  assertResult(PlayResult::Ok, rig.play("{\"speech\":\"hello\"}"), "speech");
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.speechRequests.size());
  TEST_ASSERT_FALSE(rig.tone.playing);
  TEST_ASSERT_TRUE(rig.router.alertPlaying());
}

void test_a_busy_speaker_speaks_nothing(void) {
  Rig rig;
  rig.pcm.speaking = true;
  rig.pcm.speakerReady = false;
  assertResult(PlayResult::NoSink, rig.play("{\"speech\":\"hello\"}"), "busy speaker");
  TEST_ASSERT_TRUE(rig.detail.message.c_str()[0] != '\0');
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_name_finds_own_sound_then_shared_then_melody);
  RUN_TEST(test_a_script_path_never_falls_back);
  RUN_TEST(test_a_missing_name_says_what_it_looked_for);
  RUN_TEST(test_names_that_could_leave_a_folder_are_never_looked_up);
  RUN_TEST(test_a_missing_melody_is_not_found_under_an_alert);
  RUN_TEST(test_each_kind_needs_its_output);
  RUN_TEST(test_a_melody_the_speaker_cannot_take_is_unavailable);
  RUN_TEST(test_a_router_without_sinks_is_harmless);
  RUN_TEST(test_volumes_are_master_times_group_and_reach_every_sink);
  RUN_TEST(test_a_late_sink_gets_the_current_volumes);
  RUN_TEST(test_the_group_reaches_the_sink);
  RUN_TEST(test_an_app_one_shot_waits_for_no_alert);
  RUN_TEST(test_app_layers_start_under_an_alert_on_a_mixer);
  RUN_TEST(test_an_alert_replaces_an_app_one_shot);
  RUN_TEST(test_a_one_shot_silences_the_other_outputs);
  RUN_TEST(test_a_failed_lookup_leaves_a_playing_sound_alone);
  RUN_TEST(test_a_script_stops_only_its_own_sounds);
  RUN_TEST(test_a_script_stops_its_effects_and_one_shot_but_no_alert);
  RUN_TEST(test_whoever_plays_the_music_last_owns_it);
  RUN_TEST(test_an_ended_one_shot_leaves_no_owner);
  RUN_TEST(test_stopped_music_has_no_owner);
  RUN_TEST(test_stop_by_group);
  RUN_TEST(test_stop_all_stops_everything);
  RUN_TEST(test_a_looping_one_shot_repeats_until_stopped);
  RUN_TEST(test_a_repeating_alert_counts_between_its_plays);
  RUN_TEST(test_a_repeating_app_sound_counts_between_its_plays);
  RUN_TEST(test_a_busy_speaker_keeps_a_repeat_waiting);
  RUN_TEST(test_a_looping_address_that_fails_ends_its_repeat);
  RUN_TEST(test_the_stream_is_held_while_a_one_shot_repeats);
  RUN_TEST(test_a_repeating_melody_holds_the_stream_only_on_a_shared_speaker);
  RUN_TEST(test_a_sound_the_sink_replaced_by_itself_is_over);
  RUN_TEST(test_a_repeating_alert_is_stopped_by_its_own_token_only);
  RUN_TEST(test_a_url_error_lands_in_the_layer_that_failed);
  RUN_TEST(test_music_the_sink_dropped_is_over);
  RUN_TEST(test_music_without_a_mixer_repeats_the_one_shot);
  RUN_TEST(test_an_effect_without_a_mixer_plays_once);
  RUN_TEST(test_a_list_plays_its_first_playable_entry);
  RUN_TEST(test_a_list_stops_at_a_mistake);
  RUN_TEST(test_status_names_what_each_group_plays);
  RUN_TEST(test_a_url_error_lands_in_the_group_that_asked);
  RUN_TEST(test_a_station_ends_app_music);
  RUN_TEST(test_a_stream_needs_the_pcm_sink);
  RUN_TEST(test_tick_pumps_every_sink);
  RUN_TEST(test_a_release_reaches_the_output_that_holds_files);
  RUN_TEST(test_caps_and_can_play);
  RUN_TEST(test_caps_follow_the_attached_sinks);
  RUN_TEST(test_check_names_the_field_of_a_bad_song_in_a_list);
  RUN_TEST(test_check_judges_only_where_an_output_exists);
  RUN_TEST(test_a_song_needs_a_synthesizer);
  RUN_TEST(test_songs_reach_the_synthesizer_as_music_effect_or_once);
  RUN_TEST(test_app_layers_leave_the_one_shot_alone);
  RUN_TEST(test_a_bad_song_is_invalid_with_its_reason);
  RUN_TEST(test_a_busy_speaker_plays_no_song);
  RUN_TEST(test_an_address_is_fetched_not_looked_up);
  RUN_TEST(test_an_address_is_refused_where_it_cannot_play);
  RUN_TEST(test_a_clip_plays_as_the_alert_one_shot);
  RUN_TEST(test_a_bad_clip_is_invalid_and_stops_nothing);
  RUN_TEST(test_a_clip_needs_a_sink_that_plays_clips);
  RUN_TEST(test_a_busy_speaker_plays_no_clip);
  RUN_TEST(test_platform_clip_route_validates_method_body_and_transfers_recording);
  RUN_TEST(test_speech_needs_a_sink_that_speaks);
  RUN_TEST(test_speech_the_sink_refuses_plays_nothing);
  RUN_TEST(test_speech_replaces_a_melody);
  RUN_TEST(test_a_busy_speaker_speaks_nothing);
  return UNITY_END();
}

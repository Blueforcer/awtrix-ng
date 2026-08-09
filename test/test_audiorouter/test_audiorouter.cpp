#include <unity.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/sound/AudioRouter.h"

using namespace awtrix;
// awtrix::Source is the transport a command came in on; the sound source stays spelled out.
using awtrix::sound::AudioRouter;
using awtrix::sound::PlayResult;
using awtrix::sound::StopScope;

void setUp() {}
void tearDown() {}

namespace {

struct FakeTone : sound::IToneSink {
  std::vector<std::string> stored;   // melodies that exist
  std::vector<std::string> asked;    // names looked up
  std::vector<std::string> played;   // RTTTL handed over
  int stops = 0, ticks = 0, volume = -1, volumeWrites = 0;
  bool playing = false;

  void begin() override {}
  void setVolume(uint8_t percent) override {
    volume = percent;
    ++volumeWrites;
  }
  bool playRtttl(const std::string& rtttl) override {
    played.push_back(rtttl);
    playing = true;
    return true;
  }
  bool playMelodyFile(const std::string& name) override {
    asked.push_back(name);
    if (std::find(stored.begin(), stored.end(), name) == stored.end()) return false;
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
  int stops = 0, ticks = 0, volume = -1, volumeWrites = 0;
  bool playing = false;

  void begin() override {}
  void setVolume(uint8_t percent) override {
    volume = percent;
    ++volumeWrites;
  }
  // The real module cannot say whether the track is on its card, so neither does this.
  bool playTrack(int track) override {
    played.push_back(track);
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

struct FakePcm : sound::IPcmSink {
  std::vector<std::string> mp3s;
  std::vector<std::string> streams;
  int mp3Stops = 0, streamStops = 0, ticks = 0;
  int soundVolume = -1, streamVolume = -1, soundWrites = 0, streamWrites = 0;
  bool mp3Running = false, streamRunning = false;

  void setSoundVolume(uint8_t percent) override {
    soundVolume = percent;
    ++soundWrites;
  }
  void setStreamVolume(uint8_t percent) override {
    streamVolume = percent;
    ++streamWrites;
  }
  bool playMp3(const std::string& path) override {
    mp3s.push_back(path);
    mp3Running = true;
    return true;
  }
  void stopMp3() override {
    ++mp3Stops;
    mp3Running = false;
  }
  bool mp3Playing() const override { return mp3Running; }
  DispatchResult playStream(const std::string& url, const std::string&,
                            DispatchDetail&) override {
    streams.push_back(url);
    streamRunning = true;
    return DispatchResult::Ok;
  }
  void stopStream() override {
    ++streamStops;
    streamRunning = false;
  }
  void tick(int64_t) override { ++ticks; }
};

struct FakeAssets : sound::IAssetProbe {
  std::vector<std::string> stored;
  bool hasMp3(const std::string& name) const override {
    return std::find(stored.begin(), stored.end(), name) != stored.end();
  }
};

// Everything wired up, which is the case that can actually get the routing wrong.
struct Rig {
  FakeTone tone;
  FakeTrack track;
  FakePcm pcm;
  FakeAssets assets;
  AudioRouter router;
  DispatchDetail detail;

  Rig() {
    router.setTone(&tone);
    router.setTrack(&track);
    router.setPcm(&pcm);
    router.setAssets(&assets);
  }

  PlayResult play(sound::Source source, const std::string& value) {
    detail.clear();
    return router.play(source, value, detail);
  }
};

void assertResult(PlayResult expected, PlayResult actual, const char* what) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), what);
}

// ---- Auto ------------------------------------------------------------------

void test_auto_prefers_a_stored_mp3(void) {
  Rig rig;
  rig.assets.stored = {"ding"};
  rig.tone.stored = {"ding"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "ding"), "auto with an mp3");
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.mp3s.size());
  TEST_ASSERT_EQUAL_STRING("/MP3/ding.mp3", rig.pcm.mp3s[0].c_str());
  TEST_ASSERT_EQUAL_size_t(0, rig.tone.asked.size());
}

void test_auto_falls_back_to_a_melody(void) {
  Rig rig;
  rig.tone.stored = {"alarm"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "alarm"), "auto with a melody only");
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.mp3s.size());
  TEST_ASSERT_EQUAL_size_t(1, rig.tone.asked.size());
  TEST_ASSERT_EQUAL_size_t(0, rig.track.played.size());
}

void test_auto_sends_a_bare_number_to_the_track_sink(void) {
  Rig rig;

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "7"), "auto with a number");
  TEST_ASSERT_EQUAL_size_t(1, rig.track.played.size());
  TEST_ASSERT_EQUAL_INT(7, rig.track.played[0]);
}

// The order that matters: the DFPlayer can never say no, so it must not be asked first.
void test_auto_lets_a_file_win_over_a_track_number(void) {
  Rig rig;
  rig.assets.stored = {"7"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "7"), "auto with 7.mp3 present");
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.mp3s.size());
  TEST_ASSERT_EQUAL_STRING("/MP3/7.mp3", rig.pcm.mp3s[0].c_str());
  TEST_ASSERT_EQUAL_size_t(0, rig.track.played.size());
}

void test_auto_lets_a_melody_win_over_a_track_number(void) {
  Rig rig;
  rig.tone.stored = {"7"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "7"), "auto with a melody named 7");
  TEST_ASSERT_EQUAL_size_t(0, rig.track.played.size());
}

void test_auto_rejects_a_track_number_out_of_range(void) {
  Rig rig;

  assertResult(PlayResult::NotFound, rig.play(sound::Source::Auto, "0"), "track 0");
  assertResult(PlayResult::NotFound, rig.play(sound::Source::Auto, "3000"), "track 3000");
  TEST_ASSERT_EQUAL_size_t(0, rig.track.played.size());
}

void test_auto_names_what_it_looked_for(void) {
  Rig rig;

  assertResult(PlayResult::NotFound, rig.play(sound::Source::Auto, "nope"), "auto with nothing stored");
  TEST_ASSERT_EQUAL_STRING("nothing called \"nope\"", rig.detail.message.c_str());
}

// ---- Explicit sources never fall back ---------------------------------------

void test_mp3_without_a_pcm_sink_is_unavailable(void) {
  Rig rig;
  rig.router.setPcm(nullptr);
  rig.tone.stored = {"ding"};

  assertResult(PlayResult::NoSink, rig.play(sound::Source::Mp3, "ding"), "mp3 without I2S");
  TEST_ASSERT_EQUAL_size_t(0, rig.tone.asked.size());
}

void test_melody_without_a_tone_sink_is_unavailable(void) {
  Rig rig;
  rig.router.setTone(nullptr);
  rig.assets.stored = {"ding"};

  assertResult(PlayResult::NoSink, rig.play(sound::Source::Melody, "ding"), "melody without a buzzer");
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.mp3s.size());
}

void test_track_without_a_track_sink_is_unavailable(void) {
  Rig rig;
  rig.router.setTrack(nullptr);

  assertResult(PlayResult::NoSink, rig.play(sound::Source::Track, "7"), "track without a DFPlayer");
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.mp3s.size());
}

// A DFPlayer board answering "not supported" instead of "your request is wrong" is the whole
// point: the caller did nothing wrong, the hardware is simply not there.
void test_rtttl_without_a_tone_sink_is_unavailable(void) {
  Rig rig;
  rig.router.setTone(nullptr);

  assertResult(PlayResult::NoSink, rig.play(sound::Source::Rtttl, "x:d=4,o=5,b=120:c"), "rtttl, no buzzer");
  TEST_ASSERT_TRUE(rig.detail.field.empty());
}

void test_mp3_does_not_settle_for_a_melody(void) {
  Rig rig;
  rig.tone.stored = {"ding"};

  assertResult(PlayResult::NotFound, rig.play(sound::Source::Mp3, "ding"), "mp3 of a melody name");
  TEST_ASSERT_EQUAL_STRING("no MP3 called \"ding\"", rig.detail.message.c_str());
  TEST_ASSERT_EQUAL_size_t(0, rig.tone.asked.size());
}

void test_melody_does_not_settle_for_an_mp3(void) {
  Rig rig;
  rig.assets.stored = {"ding"};

  assertResult(PlayResult::NotFound, rig.play(sound::Source::Melody, "ding"), "melody of an mp3 name");
  TEST_ASSERT_EQUAL_STRING("no melody called \"ding\"", rig.detail.message.c_str());
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.mp3s.size());
}

void test_mp3_rejects_a_name_that_cannot_be_a_path(void) {
  Rig rig;
  rig.assets.stored = {"../etc/passwd"};

  assertResult(PlayResult::NotFound, rig.play(sound::Source::Mp3, "../etc/passwd"), "escaping name");
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.mp3s.size());
}

void test_track_range_is_enforced(void) {
  Rig rig;

  assertResult(PlayResult::Ok, rig.play(sound::Source::Track, "1"), "track 1");
  assertResult(PlayResult::Ok, rig.play(sound::Source::Track, "2999"), "track 2999");
  assertResult(PlayResult::Invalid, rig.play(sound::Source::Track, "0"), "track 0");
  TEST_ASSERT_EQUAL_STRING("track", rig.detail.field.c_str());
  assertResult(PlayResult::Invalid, rig.play(sound::Source::Track, "3000"), "track 3000");
  assertResult(PlayResult::Invalid, rig.play(sound::Source::Track, "-1"), "track -1");
  assertResult(PlayResult::Invalid, rig.play(sound::Source::Track, "ding"), "track by name");
  assertResult(PlayResult::Invalid, rig.play(sound::Source::Track, ""), "empty track");
  TEST_ASSERT_EQUAL_size_t(2, rig.track.played.size());
}

// A typo is a typo on every panel: the payload is judged before the hardware, so the answer does
// not turn into "no such output" on a board that happens to lack one.
void test_a_broken_payload_outranks_a_missing_sink(void) {
  Rig rig;
  rig.router.setTone(nullptr);
  rig.router.setTrack(nullptr);

  assertResult(PlayResult::Invalid, rig.play(sound::Source::Rtttl, "not a melody"), "bad rtttl");
  TEST_ASSERT_EQUAL_STRING("rtttl", rig.detail.field.c_str());

  assertResult(PlayResult::Invalid, rig.play(sound::Source::Track, "0"), "track 0");
  TEST_ASSERT_EQUAL_STRING("track", rig.detail.field.c_str());
}

void test_unparsable_rtttl_is_a_validation_error(void) {
  Rig rig;

  assertResult(PlayResult::Invalid, rig.play(sound::Source::Rtttl, "not a melody"), "bad rtttl");
  TEST_ASSERT_EQUAL_STRING("rtttl", rig.detail.field.c_str());
  TEST_ASSERT_FALSE(rig.detail.message.empty());
  TEST_ASSERT_EQUAL_size_t(0, rig.tone.played.size());
}

void test_good_rtttl_reaches_the_tone_sink(void) {
  Rig rig;

  assertResult(PlayResult::Ok, rig.play(sound::Source::Rtttl, "beep:d=4,o=5,b=120:c"), "good rtttl");
  TEST_ASSERT_EQUAL_size_t(1, rig.tone.played.size());
}

// ---- Muting -----------------------------------------------------------------

void test_muted_reaches_no_sink_at_all(void) {
  Rig rig;
  rig.assets.stored = {"ding"};
  rig.tone.stored = {"ding"};
  rig.router.setMuted(true);

  assertResult(PlayResult::Muted, rig.play(sound::Source::Auto, "ding"), "muted auto");
  assertResult(PlayResult::Muted, rig.play(sound::Source::Mp3, "ding"), "muted mp3");
  assertResult(PlayResult::Muted, rig.play(sound::Source::Melody, "ding"), "muted melody");
  assertResult(PlayResult::Muted, rig.play(sound::Source::Track, "7"), "muted track");
  assertResult(PlayResult::Muted, rig.play(sound::Source::Rtttl, "beep:d=4,o=5,b=120:c"), "muted rtttl");
  TEST_ASSERT_EQUAL_size_t(0, rig.pcm.mp3s.size());
  TEST_ASSERT_EQUAL_size_t(0, rig.tone.asked.size());
  TEST_ASSERT_EQUAL_size_t(0, rig.tone.played.size());
  TEST_ASSERT_EQUAL_size_t(0, rig.track.played.size());
}

// Muting is about one-shots. A station the user started keeps playing.
void test_muting_leaves_the_stream_alone(void) {
  Rig rig;
  rig.router.setMuted(true);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Ok),
                        static_cast<int>(rig.router.playStream("http://x/", "X", rig.detail)));
  TEST_ASSERT_EQUAL_size_t(1, rig.pcm.streams.size());
}

// ---- One sound at a time -----------------------------------------------------

void test_an_mp3_silences_the_other_sinks(void) {
  Rig rig;
  rig.assets.stored = {"ding"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "ding"), "mp3");
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.streamStops);
}

void test_a_melody_silences_the_other_sinks(void) {
  Rig rig;
  rig.tone.stored = {"alarm"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Melody, "alarm"), "melody");
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.streamStops);
}

void test_a_failed_lookup_leaves_a_playing_sound_alone(void) {
  Rig rig;
  rig.assets.stored = {"ding"};

  assertResult(PlayResult::Ok, rig.play(sound::Source::Auto, "ding"), "mp3");
  assertResult(PlayResult::NotFound, rig.play(sound::Source::Auto, "nope"), "unknown name");
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_TRUE(rig.pcm.mp3Playing());
}

// ---- Stop scopes --------------------------------------------------------------

void test_stop_sounds_spares_the_stream(void) {
  Rig rig;
  rig.router.playStream("http://x/", "X", rig.detail);

  rig.router.stop(StopScope::Sounds);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.streamStops);
  TEST_ASSERT_TRUE(rig.pcm.streamRunning);
}

void test_stop_stream_spares_the_sounds(void) {
  Rig rig;
  rig.router.playStream("http://x/", "X", rig.detail);

  rig.router.stop(StopScope::Stream);
  TEST_ASSERT_EQUAL_INT(0, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(0, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.streamStops);
}

void test_stop_all_stops_everything(void) {
  Rig rig;

  rig.router.stop(StopScope::All);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.track.stops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.mp3Stops);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.streamStops);
}

// ---- Volumes ------------------------------------------------------------------

void test_volumes_reach_their_own_sink(void) {
  Rig rig;

  rig.router.setVolumes(10, 20, 30, 40);
  TEST_ASSERT_EQUAL_INT(10, rig.tone.volume);
  TEST_ASSERT_EQUAL_INT(20, rig.track.volume);
  TEST_ASSERT_EQUAL_INT(30, rig.pcm.soundVolume);
  TEST_ASSERT_EQUAL_INT(40, rig.pcm.streamVolume);
}

void test_unchanged_volumes_are_not_pushed_again(void) {
  Rig rig;

  rig.router.setVolumes(10, 20, 30, 40);
  rig.router.setVolumes(10, 20, 30, 40);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.volumeWrites);
  TEST_ASSERT_EQUAL_INT(1, rig.track.volumeWrites);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.soundWrites);
  TEST_ASSERT_EQUAL_INT(1, rig.pcm.streamWrites);

  rig.router.setVolumes(10, 21, 30, 40);
  TEST_ASSERT_EQUAL_INT(2, rig.track.volumeWrites);
  TEST_ASSERT_EQUAL_INT(1, rig.tone.volumeWrites);
}

// A sink attached after the settings were applied still gets its value.
void test_a_late_sink_still_gets_its_volume(void) {
  FakeTone tone;
  AudioRouter router;

  router.setVolumes(55, 55, 55, 55);
  TEST_ASSERT_EQUAL_INT(-1, tone.volume);

  router.setTone(&tone);
  router.setVolumes(55, 55, 55, 55);
  TEST_ASSERT_EQUAL_INT(55, tone.volume);
  TEST_ASSERT_EQUAL_INT(1, tone.volumeWrites);
}

// ---- Everything else -----------------------------------------------------------

void test_a_stream_is_not_a_sound_playing(void) {
  Rig rig;
  rig.router.playStream("http://x/", "X", rig.detail);

  TEST_ASSERT_TRUE(rig.pcm.streamRunning);
  TEST_ASSERT_FALSE(rig.router.isPlaying());

  rig.assets.stored = {"ding"};
  rig.play(sound::Source::Auto, "ding");
  TEST_ASSERT_TRUE(rig.router.isPlaying());
}

void test_a_stream_needs_the_pcm_sink(void) {
  Rig rig;
  rig.router.setPcm(nullptr);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(DispatchResult::Unavailable),
                        static_cast<int>(rig.router.playStream("http://x/", "X", rig.detail)));
}

void test_tick_pumps_every_sink(void) {
  Rig rig;

  rig.router.tick(1000);
  rig.router.tick(2000);
  TEST_ASSERT_EQUAL_INT(2, rig.tone.ticks);
  TEST_ASSERT_EQUAL_INT(2, rig.track.ticks);
  TEST_ASSERT_EQUAL_INT(2, rig.pcm.ticks);
}

void test_caps_report_the_attached_sinks(void) {
  Rig rig;
  sound::Caps caps = rig.router.caps();
  TEST_ASSERT_TRUE(caps.buzzer);
  TEST_ASSERT_TRUE(caps.track);
  TEST_ASSERT_TRUE(caps.mp3);
  TEST_ASSERT_TRUE(caps.radio);

  rig.router.setTrack(nullptr);
  rig.router.setPcm(nullptr);
  caps = rig.router.caps();
  TEST_ASSERT_TRUE(caps.buzzer);
  TEST_ASSERT_FALSE(caps.track);
  TEST_ASSERT_FALSE(caps.mp3);
  TEST_ASSERT_FALSE(caps.radio);
}

// A bare router routes nowhere, and says so rather than crashing.
void test_a_router_without_sinks_is_harmless(void) {
  AudioRouter router;
  DispatchDetail detail;

  assertResult(PlayResult::NotFound, router.play(sound::Source::Auto, "ding", detail), "auto, no sinks");
  assertResult(PlayResult::NoSink, router.play(sound::Source::Mp3, "ding", detail), "mp3, no sinks");
  router.stop(StopScope::All);
  router.tick(1000);
  TEST_ASSERT_FALSE(router.isPlaying());
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_auto_prefers_a_stored_mp3);
  RUN_TEST(test_auto_falls_back_to_a_melody);
  RUN_TEST(test_auto_sends_a_bare_number_to_the_track_sink);
  RUN_TEST(test_auto_lets_a_file_win_over_a_track_number);
  RUN_TEST(test_auto_lets_a_melody_win_over_a_track_number);
  RUN_TEST(test_auto_rejects_a_track_number_out_of_range);
  RUN_TEST(test_auto_names_what_it_looked_for);
  RUN_TEST(test_mp3_without_a_pcm_sink_is_unavailable);
  RUN_TEST(test_melody_without_a_tone_sink_is_unavailable);
  RUN_TEST(test_track_without_a_track_sink_is_unavailable);
  RUN_TEST(test_rtttl_without_a_tone_sink_is_unavailable);
  RUN_TEST(test_mp3_does_not_settle_for_a_melody);
  RUN_TEST(test_melody_does_not_settle_for_an_mp3);
  RUN_TEST(test_mp3_rejects_a_name_that_cannot_be_a_path);
  RUN_TEST(test_track_range_is_enforced);
  RUN_TEST(test_a_broken_payload_outranks_a_missing_sink);
  RUN_TEST(test_unparsable_rtttl_is_a_validation_error);
  RUN_TEST(test_good_rtttl_reaches_the_tone_sink);
  RUN_TEST(test_muted_reaches_no_sink_at_all);
  RUN_TEST(test_muting_leaves_the_stream_alone);
  RUN_TEST(test_an_mp3_silences_the_other_sinks);
  RUN_TEST(test_a_melody_silences_the_other_sinks);
  RUN_TEST(test_a_failed_lookup_leaves_a_playing_sound_alone);
  RUN_TEST(test_stop_sounds_spares_the_stream);
  RUN_TEST(test_stop_stream_spares_the_sounds);
  RUN_TEST(test_stop_all_stops_everything);
  RUN_TEST(test_volumes_reach_their_own_sink);
  RUN_TEST(test_unchanged_volumes_are_not_pushed_again);
  RUN_TEST(test_a_late_sink_still_gets_its_volume);
  RUN_TEST(test_a_stream_is_not_a_sound_playing);
  RUN_TEST(test_a_stream_needs_the_pcm_sink);
  RUN_TEST(test_tick_pumps_every_sink);
  RUN_TEST(test_caps_report_the_attached_sinks);
  RUN_TEST(test_a_router_without_sinks_is_harmless);
  return UNITY_END();
}

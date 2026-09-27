#include <unity.h>
#include "core/sound/ToneSamples.h"
#include "core/sound/AudioRouter.h"
#include "core/sound/NotificationSound.h"
#include "core/payload/PayloadParser.h"
#include "core/api/CapabilitiesJson.h"
#include <cstdio>
#include <cstdlib>
using namespace awtrix;
void setUp() {}
void tearDown() {}

static void waveform_and_volume() {
  sound::ToneSamples full, half, silent, clamped;
  full.setVolume(100); half.setVolume(50); silent.setVolume(0); clamped.setVolume(255);
  for (auto* s : {&full, &half, &silent, &clamped}) TEST_ASSERT_TRUE(s->play("x:d=1,o=4,b=120:a"));
  int positiveEdges = 0;
  int16_t last = -1;
  for (uint32_t i = 0; i < sound::ToneSamples::SampleRate; ++i) {
    const auto a = full.next();
    TEST_ASSERT_EQUAL_INT(32767, std::abs(static_cast<int>(a)));
    TEST_ASSERT_EQUAL_INT(a > 0 ? 16383 : -16383, half.next());
    TEST_ASSERT_EQUAL_INT(0, silent.next());
    TEST_ASSERT_EQUAL_INT(a, clamped.next());
    if (a > 0 && last < 0) ++positiveEdges;
    last = a;
  }
  TEST_ASSERT_EQUAL_INT(440, positiveEdges);
  TEST_ASSERT_EQUAL_HEX32(0xffffFFFFu, sound::ToneSamples::stereo(-1));
  TEST_ASSERT_EQUAL_HEX32(0x12341234u, sound::ToneSamples::stereo(0x1234));
}

static void duration_rest_gap_and_completion() {
  const std::string text = "s:d=4,o=6,b=125:c,p,c.,32g";
  const auto parsed = rtttl::parse(text);
  sound::ToneSamples s;
  s.setVolume(100);
  TEST_ASSERT_TRUE(s.play(text));
  for (const auto& note : parsed.notes) {
    const auto ms = rtttl::noteMs(note.duration, parsed.timeUnit);
    const auto gap = note.frequency && ms > 12 ? 6u : 0u;
    for (uint32_t i = 0; i < ms * 24; ++i) {
      TEST_ASSERT_TRUE(s.playing());
      const auto sample = s.next();
      if (!note.frequency || i >= (ms - gap) * 24) TEST_ASSERT_EQUAL_INT(0, sample);
      else TEST_ASSERT_NOT_EQUAL(0, sample);
    }
  }
  TEST_ASSERT_FALSE(s.playing());
  TEST_ASSERT_EQUAL_INT(0, s.next());
}

static void replacement_stop_and_live_gain() {
  sound::ToneSamples s;
  s.setVolume(100);
  TEST_ASSERT_FALSE(s.play("bad"));
  TEST_ASSERT_FALSE(s.playing());
  TEST_ASSERT_TRUE(s.play("x::c"));
  TEST_ASSERT_FALSE(s.play("bad"));
  TEST_ASSERT_TRUE(s.playing());
  s.setVolume(0);
  TEST_ASSERT_EQUAL_INT(0, s.next());
  s.setVolume(25);
  TEST_ASSERT_EQUAL_INT(8191, std::abs(static_cast<int>(s.next())));
  TEST_ASSERT_TRUE(s.play("s:d=4,o=6,b=125:c,e,g"));
  TEST_ASSERT_EQUAL_INT(8191, s.next());
  s.stop();
  TEST_ASSERT_FALSE(s.playing());
  TEST_ASSERT_EQUAL_INT(0, s.next());
}

// Exercise the real payload selector/router with the real sample generator;
// only the device filesystem and I2S transport are replaced on the host.
class HostTone final : public sound::IToneSink {
 public:
  sound::ToneSamples samples;
  bool fileRequested = false;
  void begin() override {}
  void setVolume(uint8_t value) override { samples.setVolume(value); }
  bool playRtttl(const std::string& value) override { return samples.play(value); }
  bool playMelodyFile(const std::string& name) override {
    fileRequested = true;
    return name == "alert" && samples.play("s:d=4,o=6,b=125:c,e,g");
  }
  void stop() override { samples.stop(); }
  void tick() override {}
  bool isPlaying() const override { return samples.playing(); }
};

static void notification_routes_and_capabilities() {
  HostTone sink;
  sound::AudioRouter router;
  router.setTone(&sink);
  router.setVolumes(50, 100, 100, 100);
  AppSpec spec;
  DispatchDetail detail;
  TEST_ASSERT_TRUE(payload::parse("{\"sound\":\"alert\"}", true, spec));
  auto request = sound::requestForSpec(spec);
  TEST_ASSERT_TRUE(request.present);
  TEST_ASSERT_TRUE(router.play(request.source, request.value, detail) == sound::PlayResult::Ok);
  TEST_ASSERT_TRUE(sink.fileRequested);
  TEST_ASSERT_EQUAL_INT(16383, sink.samples.next());
  sink.fileRequested = false;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"sound\":\"missing\",\"soundRtttl\":\"s:d=4,o=6,b=125:c,e,g\"}", true, spec));
  request = sound::requestForSpec(spec);
  TEST_ASSERT_TRUE(request.source == sound::Source::Rtttl);
  TEST_ASSERT_TRUE(router.play(request.source, request.value, detail) == sound::PlayResult::Ok);
  TEST_ASSERT_FALSE(sink.fileRequested);
  TEST_ASSERT_EQUAL_INT(16383, sink.samples.next());
  router.setVolumes(0, 100, 100, 100);
  TEST_ASSERT_EQUAL_INT(0, sink.samples.next());
  router.stop(sound::StopScope::All);
  TEST_ASSERT_FALSE(router.isPlaying());
  router.setMuted(true);
  TEST_ASSERT_TRUE(router.play(request.source, request.value, detail) == sound::PlayResult::Muted);
  TEST_ASSERT_FALSE(router.isPlaying());
  const auto json = api::capabilitiesJson({}, {}, {}, router.caps());
  const std::string audio = "\"audio\":{\"buzzer\":true,\"track\":false,\"mp3\":false,\"radio\":false}";
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(audio));
  std::printf("tone-only capabilities: %s\n", audio.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(notification_routes_and_capabilities);
  RUN_TEST(waveform_and_volume);
  RUN_TEST(duration_rest_gap_and_completion);
  RUN_TEST(replacement_stop_and_live_gain);
  return UNITY_END();
}

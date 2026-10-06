#include "../support.h"
#include "../../test/EngineFakes.h"
// The boot sound through Tc002Speaker and a scripted speaker helper: it starts past the decoder's
// lead at the alert level, reports when it becomes audible, stays silent while the boot sound is
// off, and reports a start that can no longer happen.
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "platform/tc002/runtime/BootIntroWide.h"
#include "core/sound/AudioRouter.h"
#include "persistence/AssetProbe.h"
#include "platform/tc002/runtime/Tc002Speaker.h"
#include "platform/tc002/contract/AudioProtocol.h"
#include "platform/tc002/audio/Tc002AudioSources.h"
#include "system/MonotonicClock.h"

using namespace awtrix;
using Start = tc002::Tc002AudioSink::SystemStart;

namespace {

using awtrix::test::require;

using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;

std::vector<int16_t> decodeAll(const std::string& path) {
  tc002::Mp3FileSource source(path);
  require(source.opened(), "reference decode opens " + path);
  std::vector<int16_t> pcm;
  const int16_t* samples = nullptr;
  std::size_t frames = 0;
  while (source.next(samples, frames) == tc002::PcmSource::Read::Data)
    pcm.insert(pcm.end(), samples, samples + frames * source.channels());
  require(source.channels() == 1 && source.rate() == 44100, "the boot sound is 44.1 kHz mono");
  return pcm;
}

// Plays the helper's part: grants credit for everything it receives and completes a drained
// generation at once.
struct Helper {
  int fd;
  uint32_t generation = 0, rate = 0, opens = 0, consumed = 0, stops = 0;
  uint8_t channels = 0;
  int volume = -1;
  bool drained = false;
  std::vector<int16_t> pcm;

  explicit Helper(int descriptor) : fd(descriptor) {}
  ~Helper() { ::close(fd); }

  void send(const uint8_t* message, std::size_t size) {
    require(size && ::send(fd, message, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size), "helper send");
  }
  void hello() {
    uint8_t message[TC002_AUDIO_MAX_MESSAGE];
    tc002_audio_hello hello{};
    hello.device_rate = TC002_AUDIO_DEVICE_RATE;
    hello.device_channels = 1;
    hello.min_db = TC002_AUDIO_MIN_DB;
    hello.max_db = TC002_AUDIO_MAX_DB;
    hello.window_bytes = TC002_AUDIO_WINDOW_BYTES;
    hello.max_pcm_bytes = TC002_AUDIO_MAX_PCM_BYTES;
    send(message, tc002_audio_encode_hello(message, sizeof message, &hello));
  }
  void status() {
    uint8_t message[TC002_AUDIO_MAX_MESSAGE];
    tc002_audio_status status{};
    status.state = drained ? TC002_AUDIO_IDLE : TC002_AUDIO_PLAYING;
    status.consumed_bytes = consumed;
    status.completed_generation = drained ? generation : 0;
    send(message, tc002_audio_encode_status(message, sizeof message, generation, &status));
  }
  bool receive(int timeoutMs) {
    pollfd ready{fd, POLLIN, 0};
    if (::poll(&ready, 1, timeoutMs) <= 0) return false;
    uint8_t message[TC002_AUDIO_MAX_MESSAGE + 1];
    const ssize_t n = ::recv(fd, message, sizeof message, MSG_DONTWAIT);
    if (n <= 0) return false;
    tc002_audio_frame frame;
    require(tc002_audio_parse(message, static_cast<std::size_t>(n), &frame), "runtime sends valid frames");
    uint8_t percent = 0;
    if (frame.type == TC002_AUDIO_OPEN) {
      require(tc002_audio_read_open(&frame, &rate, &channels), "open message");
      generation = frame.generation;
      ++opens;
      consumed = 0;
      drained = false;
      pcm.clear();
      status();
    } else if (frame.type == TC002_AUDIO_PCM && frame.generation == generation) {
      const std::size_t count = frame.length / 2;
      for (std::size_t i = 0; i < count; ++i)
        pcm.push_back(static_cast<int16_t>(tc002_audio_get16(frame.payload + 2 * i)));
      consumed += frame.length;
      status();
    } else if (frame.type == TC002_AUDIO_DRAIN && frame.generation == generation) {
      drained = true;
      status();
    } else if (frame.type == TC002_AUDIO_STOP) {
      ++stops;
    } else if (tc002_audio_read_volume(&frame, &percent)) {
      volume = percent;
    }
    return true;
  }
};

struct Rig {
  Display display;
  System system;
  sound::AudioRouter router;
  AssetProbe assets;
  CoreEngine engine{router, display, system};
  Settings settings;
  int ends[2] = {-1, -1};
  std::unique_ptr<Helper> helper;
  std::unique_ptr<Tc002Speaker> speaker;

  Rig() {
    router.setAssets(&assets);
    require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ends) == 0, "socketpair");
    helper.reset(new Helper(ends[1]));
    speaker.reset(new Tc002Speaker(engine, router, ends[0]));
    settings = engine.state().settings();
    settings.volume = 50;
    settings.alertVolume = 80;
    settings.appVolume = 20;
    applyAudioSettings(router, settings);
  }
  ~Rig() { speaker.reset(); }

  Start start(int64_t& at) { return speaker->bootSoundStart(at); }
  bool until(const std::function<bool()>& done, int timeoutMs) {
    const int64_t deadline = monotonicMs() + timeoutMs;
    while (monotonicMs() < deadline) {
      router.tick(monotonicMs());
      if (done()) return true;
      helper->receive(5);
    }
    return done();
  }
};

void playsPastTheLead(const std::string& path) {
  const std::vector<int16_t> reference = decodeAll(path);
  require(reference.size() > render::kBootSoundLeadFrames + 44100, "the reference decode holds the sound");
  Rig rig;
  rig.helper->hello();
  int64_t at = 0;
  rig.settings.bootSound = false;
  require(!rig.speaker->playBootSound(path, render::kBootSoundLeadFrames, rig.settings), "boot sound off: none");
  require(rig.start(at) == Start::None, "boot sound off: nothing was requested");
  rig.until([] { return false; }, 150);
  require(rig.helper->opens == 0, "boot sound off: the helper hears nothing");

  rig.settings.bootSound = true;
  const int64_t requested = monotonicMs();
  require(rig.speaker->playBootSound(path, render::kBootSoundLeadFrames, rig.settings), "boot sound accepted");
  require(rig.start(at) == Start::Pending && at == -1, "pending until its first frame is queued");
  require(rig.until([&] { return rig.start(at) == Start::Audible; }, 3000), "the boot sound becomes audible");
  require(at >= requested && at <= monotonicMs() + 1000, "audible time is plausible: " + std::to_string(at - requested));
  require(rig.until([&] { return rig.helper->drained; }, 15000), "the whole sound reaches the helper");
  require(rig.helper->opens == 1 && rig.helper->rate == 44100 && rig.helper->channels == 1, "one 44.1 kHz mono stream");
  require(rig.helper->volume == 40, "at the alert level, master 50 % of 80 %: " + std::to_string(rig.helper->volume));
  const std::vector<int16_t> expected(reference.begin() + render::kBootSoundLeadFrames, reference.end());
  require(rig.helper->pcm.size() == expected.size(),
          "all frames after the lead: " + std::to_string(rig.helper->pcm.size()) + " of " +
              std::to_string(expected.size()));
  require(rig.helper->pcm == expected, "the samples are the decoded sound past its lead");
  require(rig.start(at) == Start::Audible, "a finished sound stays audible-started");
}

void startsThatCannotHappen(const std::string& path) {
  {
    Rig rig;
    rig.helper->hello();
    require(rig.speaker->playBootSound("/nonexistent/boot.mp3", 0, rig.settings), "request accepted");
    int64_t at = 0;
    require(rig.until([&] { return rig.start(at) == Start::Failed; }, 2000), "a missing file fails the start");
  }
  {
    Rig rig;
    require(rig.speaker->playBootSound(path, render::kBootSoundLeadFrames, rig.settings), "request accepted");
    int64_t at = 0;
    rig.until([] { return false; }, 200);
    require(rig.start(at) == Start::Pending, "a helper that is not ready keeps the start pending");
    rig.speaker->stopBootSound();
    require(rig.until([&] { return rig.start(at) == Start::Failed; }, 2000), "a cancelled start fails");
  }
  {
    Rig rig;
    require(rig.speaker->playBootSound(path, render::kBootSoundLeadFrames, rig.settings), "request accepted");
    rig.until([] { return false; }, 100);
    Command command;
    api::HttpResult immediate;
    require(api::routeHttp("POST", "/api/v1/audio/play", "{\"rtttl\":\"t:d=8,o=5,b=180:c,e\"}", command, immediate) ==
                api::RouteOutcome::Routed,
            "melody routes");
    require(rig.engine.execute(command) == DispatchResult::Ok, "melody accepted");
    int64_t at = 0;
    require(rig.until([&] { return rig.start(at) == Start::Failed; }, 2000), "a melody taking over fails the start");
    rig.helper->hello();
    require(rig.until([&] { return rig.helper->opens == 1; }, 3000) && rig.helper->volume == 40,
            "and the melody plays at the alert level");
  }
  {
    Rig rig;
    rig.helper->hello();
    require(rig.speaker->playBootSound(path, render::kBootSoundLeadFrames, rig.settings), "request accepted");
    int64_t at = 0;
    require(rig.until([&] { return rig.start(at) == Start::Audible; }, 3000), "audible");
    rig.router.stop(sound::Stop::All);
    require(rig.until([&] { return rig.helper->stops == 1; }, 2000), "stopping the sounds stops the boot sound");
    require(rig.start(at) == Start::Audible, "and keeps its start");
  }
}

}

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s BOOT.mp3\n", argv[0]);
    return 2;
  }
  playsPastTheLead(argv[1]);
  startsThatCannotHappen(argv[1]);
  std::puts("boot sound: ok");
  return 0;
}

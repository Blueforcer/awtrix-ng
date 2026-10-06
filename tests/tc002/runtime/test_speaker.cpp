#include "../../support.h"
#include "../../../test/EngineFakes.h"
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/api/ApiRouter.h"
#include "core/api/CapabilitiesJson.h"
#include "core/sound/AudioRouter.h"
#include "persistence/AssetProbe.h"
#include "platform/tc002/runtime/Tc002Speaker.h"
#include "platform/tc002/contract/AudioProtocol.h"
#include "system/MonotonicClock.h"

using namespace awtrix;

namespace {
using awtrix::test::require;

using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;

std::string audioCaps(const sound::AudioRouter& router) {
  const std::string json = api::capabilitiesJson({}, {}, {}, router.caps());
  const auto at = json.find("\"audio\":");
  return at == std::string::npos ? std::string() : json.substr(at, json.find('}', at) - at + 1);
}

struct Helper {
  int fd;
  std::vector<uint8_t> types;
  int volume = -1;
  uint32_t pcmBytes = 0;

  explicit Helper(int descriptor) : fd(descriptor) {}
  ~Helper() { if (fd >= 0) ::close(fd); }

  void hello() {
    uint8_t message[TC002_AUDIO_MAX_MESSAGE];
    tc002_audio_hello hello{};
    hello.device_rate = TC002_AUDIO_DEVICE_RATE;
    hello.device_channels = 1;
    hello.min_db = TC002_AUDIO_MIN_DB;
    hello.max_db = TC002_AUDIO_MAX_DB;
    hello.window_bytes = TC002_AUDIO_WINDOW_BYTES;
    hello.max_pcm_bytes = TC002_AUDIO_MAX_PCM_BYTES;
    const std::size_t size = tc002_audio_encode_hello(message, sizeof message, &hello);
    require(size && ::send(fd, message, size, 0) == static_cast<ssize_t>(size), "helper hello");
  }

  bool receive(int timeoutMs) {
    pollfd ready{fd, POLLIN, 0};
    if (::poll(&ready, 1, timeoutMs) <= 0) return false;
    uint8_t message[TC002_AUDIO_MAX_MESSAGE + 1];
    const ssize_t n = ::recv(fd, message, sizeof message, MSG_DONTWAIT);
    if (n <= 0) return false;
    tc002_audio_frame frame;
    require(tc002_audio_parse(message, static_cast<std::size_t>(n), &frame), "runtime sends valid frames");
    types.push_back(frame.type);
    uint8_t percent = 0;
    if (tc002_audio_read_volume(&frame, &percent)) volume = percent;
    if (frame.type == TC002_AUDIO_PCM) pcmBytes += frame.length;
    return true;
  }

  bool saw(uint8_t type) const {
    for (uint8_t seen : types)
      if (seen == type) return true;
    return false;
  }
};

void play(CoreEngine& engine, const std::string& body) {
  Command command;
  api::HttpResult immediate;
  require(api::routeHttp("POST", "/api/v1/audio/play", std::string(body), command, immediate) ==
              api::RouteOutcome::Routed,
          "play request routes");
  require(engine.execute(command) == DispatchResult::Ok, "play request accepted");
}
}

int main() {
  std::signal(SIGPIPE, SIG_IGN);
  Display display;
  System system;
  sound::AudioRouter router;
  AssetProbe assets;
  router.setAssets(&assets);
  CoreEngine engine{router, display, system};
  const std::string silent =
      "\"audio\":{\"mp3\":false,\"rtttl\":false,\"song\":false,\"speech\":false,\"track\":false,"
      "\"radio\":false,\"url\":false,\"effect\":false,\"clip\":false}";
  require(audioCaps(router) == silent, "no speaker: audio stays unavailable");
  applyAudioSettings(router, engine.state().settings());

  int ends[2];
  require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ends) == 0, "socketpair");
  Helper helper(ends[1]);
  {
    Tc002Speaker speaker(engine, router, ends[0]);
    require(speaker.available(), "speaker starts");
    require(audioCaps(router) ==
                "\"audio\":{\"mp3\":true,\"rtttl\":true,\"song\":true,\"speech\":false,\"track\":false,"
                "\"radio\":true,\"url\":true,\"effect\":true,\"clip\":true}",
            "speaker: MP3, melodies, songs, radio, addresses, effects and clips; no voice, no track player");
    helper.hello();
    Settings settings = engine.state().settings();
    settings.volume = 50;
    settings.alertVolume = 70;
    applyAudioSettings(router, settings);
    play(engine, "{\"rtttl\":\"t:d=8,o=5,b=180:c,e,g,c6\"}");
    const int64_t deadline = monotonicMs() + 5000;
    while (monotonicMs() < deadline && !(helper.saw(TC002_AUDIO_OPEN) && helper.pcmBytes >= 4096)) {
      router.tick(monotonicMs());
      helper.receive(20);
    }
    require(helper.saw(TC002_AUDIO_OPEN), "a melody opens a stream on the helper");
    require(helper.pcmBytes >= 4096, "and sends its PCM");
    require(helper.volume == 35, "an API melody is an alert: master 50 % of alert 70 %");
  }
  require(audioCaps(router) == silent, "a released speaker leaves no dangling sink");

  require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, ends) == 0, "second socketpair");
  ::close(ends[1]);
  {
    Tc002Speaker speaker(engine, router, ends[0]);
    const int64_t deadline = monotonicMs() + 2000;
    while (monotonicMs() < deadline && speaker.available()) ::usleep(5000);
    require(!speaker.available(), "a missing helper makes the speaker unavailable");
  }
  std::puts("linux speaker: ok");
  return 0;
}

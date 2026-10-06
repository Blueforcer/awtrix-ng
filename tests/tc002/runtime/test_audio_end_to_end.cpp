#include "../../support.h"
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/CoreEngine.h"
#include "core/sound/AudioRouter.h"
#include "persistence/AssetProbe.h"
#include "platform/linux/host/HostStore.h"
#include "platform/tc002/audio/Tc002AudioSink.h"
#include "platform/tc002/audio/Tc002AudioSources.h"
#include "system/MonotonicClock.h"
#include "test_mp3pcm/vectors.h"

extern "C" {
#include "platform/tc002/audio/pcm/pcm_backend.h"
}
#include "speaker/fake_pcm_device.h"

using namespace awtrix;
using namespace awtrix::tc002;
using namespace mp3vectors;

namespace {

auto& passed = awtrix::test::passed();

constexpr auto check = awtrix::test::require;

bool waitFor(const std::function<bool()>& done, int timeoutMs) {
  const int64_t end = monotonicMs() + timeoutMs;
  while (monotonicMs() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return done();
}

// The fake awtrix_pcm device, playing four times faster than real time.
struct Device {
  static constexpr int64_t kSpeed = 4;
  std::mutex mutex;
  fake_pcm pcm{};
  uint64_t audible = 0, mutedSound = 0;
};

Device* device = nullptr;

int64_t deviceClock(fake_pcm*) { return monotonicMs() * Device::kSpeed; }

// The real helper core and awtrix_pcm bridge behind the socket, driven like ah_run drives them.
struct Helper {
  int fd;
  std::unique_ptr<ap_helper> helper{new ap_helper()};
  ap_ops ops{};
  std::thread thread;
  std::atomic<bool> exited{false};
  unsigned cleanupFailures = 0;

  explicit Helper(int socket) : fd(socket) {
    ops.context = this;
    ops.now_ms = [](void*) { return monotonicMs(); };
    ops.send = [](void* self, const uint8_t* data, size_t size) {
      const ssize_t n = ::send(static_cast<Helper*>(self)->fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n == static_cast<ssize_t>(size)) return 1;
      return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
    };
    ops.ioctl = [](void*, unsigned request, void* argument) {
      int result, error;
      {
        std::lock_guard<std::mutex> lock(device->mutex);
        check(request != AWTRIX_PCM_DRAIN, "the blocking DRAIN ioctl is never used");
        if (request == AWTRIX_PCM_STOP) check(device->pcm.muted, "flushed only while muted");
        if (request == AWTRIX_PCM_SET_GAIN) {
          int32_t db;
          std::memcpy(&db, argument, sizeof db);
          check(db >= -60 && db <= -3, "gain within -60..-3 dB");
        }
        result = fake_pcm_ioctl(&device->pcm, request, argument);
        error = errno;
      }
      errno = error;
      return result;
    };
    ops.write = [](void*, const void* data, size_t size) {
      ssize_t n;
      int error;
      {
        std::lock_guard<std::mutex> lock(device->mutex);
        const bool muted = device->pcm.muted != 0;
        n = fake_pcm_write(&device->pcm, data, size);
        error = errno;
        const int16_t* samples = static_cast<const int16_t*>(data);
        for (ssize_t i = 0; i < n / 2; ++i) {
          if (!samples[i]) continue;
          if (muted) ++device->mutedSound;
          else ++device->audible;
        }
      }
      errno = error;
      return n;
    };
    thread = std::thread([this] { run(); });
  }

  ~Helper() {
    if (thread.joinable()) thread.join();
    ::close(fd);
  }

  void run() {
    ah_state& s = helper->core;
    check(ap_start(helper.get(), &ops) != 0, "helper starts");
    int wait = -1;
    for (;;) {
      ah_flush(&s);
      if (s.broken || s.state == TC002_AUDIO_FAILED) break;
      pollfd p{fd, POLLIN, 0};
      ::poll(&p, 1, wait);
      uint8_t message[TC002_AUDIO_MAX_MESSAGE + 1];
      bool eof = false;
      for (;;) {
        const ssize_t n = ::recv(fd, message, sizeof message, MSG_DONTWAIT | MSG_TRUNC);
        if (n == 0) eof = true;
        if (n <= 0) break;
        ah_receive(&s, message, static_cast<size_t>(n) > TC002_AUDIO_MAX_MESSAGE ? 0 : static_cast<size_t>(n),
                   monotonicMs());
      }
      if (eof) break;
      wait = ah_service(&s, monotonicMs());
    }
    cleanupFailures = ap_shutdown(helper.get());
    exited = true;
  }
};

struct Rig {
  struct Display : IDisplayService {
    void sendScreen() override {}
  } display;
  struct System : ISystemService {
    void reboot() override {}
    void sleep(uint64_t) override {}
    void factoryReset() override {}
    void resetSettings() override {}
  } system;
  sound::AudioRouter router;
  AssetProbe assets;
  CoreEngine engine{router, display, system};
  Device fake;
  std::unique_ptr<Helper> helper;
  std::unique_ptr<Tc002AudioSink> sink;

  Rig() {
    device = &fake;
    fake_pcm_reset(&fake.pcm, deviceClock, nullptr, 0);
    int fds[2];
    check(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) == 0, "socketpair");
    helper.reset(new Helper(fds[1]));
    sink.reset(new Tc002AudioSink(engine, fds[0]));
    router.setTone(sink.get());
    router.setPcm(sink.get());
    router.setAssets(&assets);
    router.setVolumes(100, 60, 50, 40);
  }

  ~Rig() {
    sink.reset();
    helper.reset();
    device = nullptr;
  }

  template <typename T>
  T read(T Device::*member) {
    std::lock_guard<std::mutex> lock(fake.mutex);
    return fake.*member;
  }

  template <typename T>
  T pcm(T fake_pcm::*member) {
    std::lock_guard<std::mutex> lock(fake.mutex);
    return fake.pcm.*member;
  }
};

std::size_t decodedFrames(const uint8_t* data, std::size_t size) {
  const std::string path = host::hostPath("/MP3/probe.mp3");
  std::FILE* f = std::fopen(path.c_str(), "wb");
  check(f && std::fwrite(data, 1, size, f) == size, "probe written");
  std::fclose(f);
  Mp3FileSource source(path);
  std::size_t frames = 0;
  for (;;) {
    const int16_t* samples;
    std::size_t n;
    if (source.next(samples, n) != PcmSource::Read::Data) break;
    frames += n;
  }
  return frames;
}

void writeMp3(const std::string& name, const uint8_t* data, std::size_t size) {
  const std::string path = host::hostPath("/MP3/" + name + ".mp3");
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::FILE* f = std::fopen(path.c_str(), "wb");
  check(f && std::fwrite(data, 1, size, f) == size, "MP3 written");
  std::fclose(f);
}

// A sound object as the API sends it, played for the group; App is a script's own sound.
sound::PlayResult play(sound::AudioRouter& router, const std::string& json, sound::Group group,
                       DispatchDetail& detail) {
  sound::Choices choices;
  check(sound::parse(json, group == sound::Group::App ? sound::Origin::Script : sound::Origin::Play, choices,
                     detail),
        "the sound object parses");
  return router.play(choices, group, "", detail);
}

bool near(uint64_t actual, double expected, double slack) {
  return std::fabs(static_cast<double>(actual) - expected) <= slack;
}

void test_melody() {
  Rig rig;
  DispatchDetail detail;
  check(play(rig.router, R"({"rtttl":"t:d=4,o=5,b=200:c,e,g"})", sound::Group::Alert, detail) == sound::PlayResult::Ok,
        "melody");
  check(waitFor([&] { return !rig.sink->isPlaying(); }, 5000), "melody drains through the helper");
  const double expected = 3.0 * rtttl::noteMs(16, 60 * 1000 * 4 / 200 / 32) * 44100 / 1000;
  check(near(rig.read(&Device::audible), expected, 2048), "every tone sample reaches the device, unmuted");
  check(rig.read(&Device::mutedSound) == 0 && rig.pcm(&fake_pcm::muted_sound) == 0,
        "nothing audible is written while muted");
  check(rig.pcm(&fake_pcm::muted) && rig.pcm(&fake_pcm::gain_db) == ah_volume_db(40),
        "muted afterwards, at the alert level");
}

void test_mp3_formats() {
  Rig rig;
  DispatchDetail detail;
  writeMp3("stereo", ksine_stereo_128k_mp3, sizeof ksine_stereo_128k_mp3);
  writeMp3("fortyeight", ksweep_stereo_128k_48k_mp3, sizeof ksweep_stereo_128k_48k_mp3);
  const std::size_t stereoFrames = decodedFrames(ksine_stereo_128k_mp3, sizeof ksine_stereo_128k_mp3);
  const std::size_t fortyEightFrames = decodedFrames(ksweep_stereo_128k_48k_mp3, sizeof ksweep_stereo_128k_48k_mp3);

  check(play(rig.router, R"("stereo")", sound::Group::App, detail) == sound::PlayResult::Ok, "44.1 kHz stereo MP3");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 8000), "MP3 drains through the helper");
  const uint64_t first = rig.read(&Device::audible);
  check(near(first, static_cast<double>(stereoFrames), 0.02 * stereoFrames + 2048), "downmixed 1:1 at 44.1 kHz");
  check(rig.pcm(&fake_pcm::gain_db) == ah_volume_db(50), "a script's MP3 at the app level");

  check(play(rig.router, R"("fortyeight")", sound::Group::App, detail) == sound::PlayResult::Ok, "48 kHz stereo MP3");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 8000), "48 kHz MP3 drains");
  const uint64_t second = rig.read(&Device::audible) - first;
  check(near(second, fortyEightFrames * 44100.0 / 48000.0, 0.03 * fortyEightFrames + 2048),
        "resampled to 44.1 kHz");
  check(rig.read(&Device::mutedSound) == 0 && rig.pcm(&fake_pcm::muted), "muted between and after");
}

void test_stop_mutes_and_exit_stops() {
  Rig rig;
  DispatchDetail detail;
  check(play(rig.router, R"({"rtttl":"long:d=1,o=5,b=40:c,d,e,f"})", sound::Group::Alert, detail) ==
            sound::PlayResult::Ok,
        "long");
  check(waitFor([&] { return rig.read(&Device::audible) > 4096; }, 3000), "sound is playing");
  const unsigned stops = rig.pcm(&fake_pcm::stops);
  rig.router.stop(sound::Stop::All);
  check(waitFor([&] { return rig.pcm(&fake_pcm::muted) && rig.pcm(&fake_pcm::stops) > stops; }, 300),
        "stop mutes and flushes within 300 ms");
  const unsigned writes = rig.pcm(&fake_pcm::writes);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  check(rig.pcm(&fake_pcm::writes) == writes, "and nothing more is written");
  rig.sink.reset();
  check(waitFor([&] { return rig.helper->exited.load(); }, 2000), "the helper exits when the runtime goes away");
  check(rig.pcm(&fake_pcm::state) == AWTRIX_PCM_PREPARED && rig.pcm(&fake_pcm::muted) &&
            !rig.pcm(&fake_pcm::drains) && !rig.helper->cleanupFailures,
        "and leaves the device muted and stopped");
}

}

int main() {
  char pattern[] = "/tmp/tc002-audio-e2e-XXXXXX";
  const char* dir = ::mkdtemp(pattern);
  check(dir != nullptr, "data directory");
  host::setDataDir(dir);
  std::filesystem::create_directories(std::filesystem::path(host::hostPath("/MP3/x")).parent_path());
  test_melody();
  test_mp3_formats();
  test_stop_mutes_and_exit_stops();
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::printf("tc002 audio end-to-end contracts: %u passed\n", passed.load());
  return 0;
}

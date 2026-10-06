#include "../../support.h"
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/posix/Files.h"
#include "platform/tc002/audio/UrlSounds.h"

using awtrix::tc002::UrlSounds;

namespace {

constexpr auto check = awtrix::test::require;

bool exists(const std::string& path) {
  struct stat st {};
  return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

// The speaker as the sink shows it: what each layer plays, and a count per layer that moves with
// every request.
struct Speaker {
  std::string oneShot, oneShotUrl, loop;
  uint32_t oneShotSeq = 0, loopSeq = 0;
  int oneShots = 0, loops = 0;
  bool ready = true;
  std::vector<std::string> failures;

  UrlSounds::Speaker ports() {
    UrlSounds::Speaker s;
    s.playOneShot = [this](const std::string& file, const std::string& url) {
      if (!ready) return false;
      ++oneShotSeq;
      ++oneShots;
      oneShot = file;
      oneShotUrl = url;
      return true;
    };
    s.playLoop = [this](const std::string& file) {
      if (!ready) return false;
      ++loopSeq;
      ++loops;
      loop = file;
      return true;
    };
    s.stopOneShot = [this] {
      if (oneShot.empty()) return;
      ++oneShotSeq;
      oneShot.clear();
    };
    s.stopLoop = [this] {
      if (loop.empty()) return;
      ++loopSeq;
      loop.clear();
    };
    s.seq = [this](bool isLoop) { return isLoop ? loopSeq : oneShotSeq; };
    s.playing = [this](bool isLoop) { return isLoop ? loop : oneShot; };
    s.failed = [this](const std::string& error, bool) { failures.push_back(error); };
    return s;
  }
};

// The time UrlSounds is told; each wait moves it on by a millisecond per tick.
int64_t now = 0;

template <class F>
bool until(UrlSounds& sounds, F done) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < end) {
    sounds.tick(++now);
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

void settle(UrlSounds& sounds) {
  for (int i = 0; i < 100; ++i) {
    sounds.tick(++now);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}

int main() {
  httplib::Server server;
  std::atomic<int> requests{0};
  std::atomic<bool> hold{false};
  server.Get("/ding.mp3", [&](const auto&, auto& r) {
    ++requests;
    while (hold) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    r.set_content("ding-bytes", "audio/mpeg");
  });
  server.Get("/rain.mp3", [&](const auto&, auto& r) {
    ++requests;
    r.set_content("rain-bytes", "audio/mpeg");
  });
  server.Get("/missing.mp3", [](const auto&, auto& r) { r.status = 404; });
  const int port = server.bind_to_any_port("127.0.0.1");
  check(port > 0, "bind loopback fixture");
  std::thread listener([&] { server.listen_after_bind(); });
  const std::string base = "http://127.0.0.1:" + std::to_string(port);

  char directory[] = "/tmp/awtrix-url-sounds-XXXXXX";
  check(::mkdtemp(directory) != nullptr, "test directory");
  const std::string leftover = std::string(directory) + "/awtrix-sound-old";
  check(::mkdir(leftover.c_str(), 0700) == 0 &&
            awtrix::posix::replaceText(leftover + "/file-x", "stale"),
        "leftover of an earlier run");

  uint64_t memory = 64u << 20;
  Speaker speaker;
  std::string lastLoop;
  {
    UrlSounds sounds(directory, speaker.ports(), [&](uint64_t& bytes) {
      bytes = memory;
      return true;
    });
    check(!exists(leftover + "/file-x") && !exists(leftover), "leftovers of an earlier run removed");

    check(!sounds.play("https://", false), "a URL without a host is refused");

    check(sounds.play(base + "/ding.mp3", false), "one-shot accepted");
    check(until(sounds, [&] { return speaker.oneShots == 1; }), "one-shot plays once fetched");
    std::string content;
    check(awtrix::posix::readText(speaker.oneShot, content) && content == "ding-bytes",
          "the fetched file is the response");
    check(speaker.oneShotUrl == base + "/ding.mp3", "the one-shot is reported by its URL");
    const std::string played = speaker.oneShot;
    speaker.oneShot.clear();
    sounds.tick(now);
    check(exists(played), "the file stays a moment after the one-shot ended");
    requests = 0;
    check(sounds.play(base + "/ding.mp3", false) && speaker.oneShots == 2 && speaker.oneShot == played &&
              requests == 0,
          "the same address again plays that file at once, without fetching it again");
    speaker.oneShot.clear();
    sounds.tick(now);
    now += UrlSounds::kKeepMs - 1;
    sounds.tick(now);
    check(exists(played), "kept for kKeepMs after it ended");
    sounds.tick(++now);
    check(!exists(played), "then the file goes");

    requests = 0;
    check(sounds.play(base + "/rain.mp3", true) && sounds.play(base + "/rain.mp3", true),
          "loop accepted twice");
    check(until(sounds, [&] { return speaker.loops == 1; }), "loop plays once fetched");
    check(sounds.play(base + "/rain.mp3", true), "the same loop again");
    settle(sounds);
    check(requests == 1 && speaker.loops == 1, "the same loop keeps playing, fetched once");

    requests = 0;
    check(sounds.play(base + "/rain.mp3", false), "one-shot of the looping file");
    check(speaker.oneShots == 3 && speaker.oneShot == speaker.loop && requests == 0,
          "plays the loop's file at once");
    const std::string shared = speaker.loop;
    speaker.oneShot.clear();
    sounds.tick(now);
    speaker.loop.clear();
    ++speaker.loopSeq;
    sounds.tick(now);
    check(exists(shared), "the one-shot still keeps the file the loop let go of");
    now += UrlSounds::kKeepMs;
    sounds.tick(now);
    check(!exists(shared), "gone once neither layer plays or keeps it");

    check(sounds.play(base + "/ding.mp3", false), "another one-shot");
    check(until(sounds, [&] { return speaker.oneShots == 4; }), "plays once fetched");
    const std::string stopped = speaker.oneShot;
    speaker.oneShot.clear();
    sounds.tick(now);
    sounds.stop(false);
    check(!exists(stopped), "stopping the one-shot drops its kept file at once");

    check(sounds.play(base + "/missing.mp3", false), "a missing file is accepted");
    check(until(sounds, [&] { return !speaker.failures.empty(); }) &&
              speaker.failures.back() == "HTTP 404" && speaker.oneShots == 4,
          "a missing file reports its status");

    memory = (4u << 20) + 4;
    check(sounds.play(base + "/ding.mp3", false), "accepted while memory is short");
    check(until(sounds, [&] { return speaker.failures.size() == 2; }) &&
              speaker.failures.back() == "not enough memory",
          "short memory refuses the download");
    memory = 64u << 20;

    hold = true;
    check(sounds.play(base + "/ding.mp3", false), "slow one-shot accepted");
    ++speaker.oneShotSeq;
    hold = false;
    settle(sounds);
    check(speaker.oneShots == 4 && speaker.failures.size() == 2,
          "a one-shot someone else replaced meanwhile stays silent");

    hold = true;
    check(sounds.play(base + "/ding.mp3", true), "slow loop accepted");
    sounds.stop(true);
    hold = false;
    settle(sounds);
    check(speaker.loops == 1, "a stopped download never plays");

    speaker.ready = false;
    check(sounds.play(base + "/rain.mp3", false), "accepted before the speaker got busy");
    check(until(sounds, [&] { return speaker.failures.size() == 3; }) &&
              speaker.failures.back() == "speaker unavailable",
          "a busy speaker reports it");
    speaker.ready = true;

    check(sounds.play(base + "/rain.mp3", true), "loop before shutdown");
    check(until(sounds, [&] { return speaker.loops == 2; }), "loop plays before shutdown");
    check(exists(speaker.loop), "loop file exists before shutdown");
    lastLoop = speaker.loop;
  }
  check(!exists(lastLoop), "shutdown removes the files");
  check(::rmdir(directory) == 0, "shutdown leaves the folder empty");

  server.stop();
  listener.join();
  std::puts(
      "url sounds: fetch, replay, cleanup, one download per loop, shared file, status, memory, "
      "replacement, stop, busy speaker and shutdown passed");
}

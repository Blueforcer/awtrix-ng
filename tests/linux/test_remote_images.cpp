#include "../support.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/images/HttpPictureLoader.h"
#include "platform/linux/images/PictureDecoder.h"
#include "platform/linux/images/PictureFit.h"
#include "platform/linux/images/RemoteImageStore.h"
#include "platform/linux/net/HttpGet.h"
#include "remote_image_fixtures.h"

using namespace awtrix;
using images::DecodeFailure;
using media::RemoteImage;
using media::RemoteState;

namespace {

constexpr auto check = awtrix::test::require;

template <class F>
bool until(F done) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

bool near(uint32_t actual, uint32_t expected, int tolerance = 12) {
  for (int shift : {16, 8, 0}) {
    const int a = (actual >> shift) & 0xFF, e = (expected >> shift) & 0xFF;
    if (a - e > tolerance || e - a > tolerance) return false;
  }
  return true;
}

uint32_t at(const RemoteImage& image, int x, int y) { return image.pixels[static_cast<std::size_t>(y) * image.width + x]; }

std::vector<uint32_t> fit(int sw, int sh, int w, int h, uint32_t (*color)(int, int), uint8_t alpha = 255) {
  images::PictureFit picture;
  check(picture.begin(sw, sh, w, h), "fit accepts the sizes");
  for (int y = 0; y < sh; ++y)
    for (int x = 0; x < sw; ++x) {
      const uint32_t c = color(x, y);
      picture.add(x, y, c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF, alpha);
    }
  media::PodBuffer<uint32_t> out;
  check(picture.finish(out), "fit finishes");
  return std::vector<uint32_t>(out.data(), out.data() + out.size());
}

void testFit() {
  const auto stripes = fit(4, 4, 2, 2, [](int x, int) -> uint32_t { return x % 2 ? 0xFFFFFF : 0; });
  check(stripes[0] == 0xBCBCBC && stripes[3] == 0xBCBCBC, "black and white average in linear light");

  const auto wide = fit(6, 2, 2, 2, [](int x, int) -> uint32_t { return x < 2 ? 0xFF0000 : x < 4 ? 0x00FF00 : 0x0000FF; });
  check(wide[0] == 0x00FF00 && wide[1] == 0x00FF00 && wide[2] == 0x00FF00, "a wide picture keeps its middle");

  const auto tall = fit(2, 6, 2, 1, [](int, int y) -> uint32_t { return y < 2 ? 0xFF0000 : y < 4 ? 0x00FF00 : 0x0000FF; });
  check(tall[0] == 0x00FF00 && tall[1] == 0x00FF00, "a tall picture keeps its middle");

  const auto grown = fit(2, 2, 4, 4, [](int x, int y) -> uint32_t { return x == y ? 0xFF0000 : 0x0000FF; });
  check(grown[0] == 0xFF0000 && grown[1] == 0xFF0000 && grown[2] == 0x0000FF && grown[5] == 0xFF0000 &&
            grown[15] == 0xFF0000 && grown[12] == 0x0000FF,
        "a smaller picture repeats its pixels");

  const auto uneven = fit(3, 1, 8, 1, [](int x, int) -> uint32_t { return x == 0 ? 0xFF0000 : x == 1 ? 0x00FF00 : 0x0000FF; });
  check(uneven == std::vector<uint32_t>{0xFF0000, 0xFF0000, 0xFF0000, 0x00FF00, 0x00FF00, 0x0000FF, 0x0000FF,
                                         0x0000FF},
        "an uneven enlargement takes the pixel under each centre");

  const auto clear = fit(2, 2, 1, 1, [](int, int) -> uint32_t { return 0xFFFFFF; }, 0);
  check(clear[0] == 0, "transparency shows as black");

  images::PictureFit picture;
  check(!picture.begin(0, 4, 2, 2) && !picture.begin(4, 4, 0, 2) &&
            !picture.begin(4, 4, images::PictureFit::kMaxSide + 1, 2) &&
            !picture.begin(images::PictureFit::kMaxSourceSide + 1, 4, 2, 2),
        "empty or oversized sizes are refused");
}

void testDecoder() {
  RemoteImage image;
  check(images::decodePicture(kQuadrantsJpeg, sizeof(kQuadrantsJpeg), 16, 16, image) == DecodeFailure::None,
        "baseline JPEG decodes");
  check(image.width == 16 && image.height == 16 && image.pixels.size() == 256 && image.gif.empty(),
        "JPEG fills exactly the target");
  check(near(at(image, 2, 2), 0xFF0000) && near(at(image, 13, 2), 0x00FF00) && near(at(image, 2, 13), 0x0000FF) &&
            near(at(image, 13, 13), 0xFFFFFF),
        "JPEG keeps its middle square");

  check(images::decodePicture(kQuadrantsJpeg, sizeof(kQuadrantsJpeg), 52, 16, image) == DecodeFailure::None &&
            image.width == 52 && near(at(image, 5, 2), 0xFF0000) && near(at(image, 46, 13), 0xFFFFFF),
        "JPEG fills a wide target");

  check(images::decodePicture(kLargeQuadrantsJpeg, sizeof(kLargeQuadrantsJpeg), 16, 16, image) == DecodeFailure::None &&
            near(at(image, 2, 2), 0xFF0000) && near(at(image, 13, 2), 0x00FF00) &&
            near(at(image, 2, 13), 0x0000FF) && near(at(image, 13, 13), 0xFFFFFF),
        "a large subsampled JPEG shrinks to the target");

  check(images::decodePicture(kProgressiveJpeg, sizeof(kProgressiveJpeg), 16, 16, image) == DecodeFailure::None &&
            near(at(image, 2, 2), 0xFF0000) && near(at(image, 13, 2), 0x00FF00) &&
            near(at(image, 2, 13), 0x0000FF) && near(at(image, 13, 13), 0xFFFFFF),
        "a progressive JPEG keeps its middle square");

  check(images::decodePicture(kGreenGif8, sizeof(kGreenGif8), 16, 16, image) == DecodeFailure::None &&
            image.width == 8 && image.height == 8 && image.pixels.empty() &&
            image.gif.size() == sizeof(kGreenGif8),
        "a GIF that fits is kept as it is");
  check(images::decodePicture(kGreenGif32, sizeof(kGreenGif32), 16, 16, image) == DecodeFailure::GifTooLarge &&
            image.gif.empty(),
        "a GIF larger than the target is refused");
  std::vector<uint8_t> heavy(kGreenGif8, kGreenGif8 + sizeof(kGreenGif8));
  heavy.resize(media::kMaxRemoteGifBytes + 1);
  check(images::decodePicture(heavy.data(), heavy.size(), 16, 16, image) == DecodeFailure::GifTooLarge,
        "a GIF beyond the size cap is refused");

  const uint8_t text[] = "<html>no picture</html>";
  check(images::decodePicture(text, sizeof(text), 16, 16, image) == DecodeFailure::Format, "other content is refused");
  check(images::decodePicture(kQuadrantsJpeg, 100, 16, 16, image) == DecodeFailure::Format, "a cut JPEG is refused");
  check(images::decodePicture(kQuadrantsJpeg, sizeof(kQuadrantsJpeg), 0, 16, image) == DecodeFailure::Unfit &&
            images::decodePicture(kQuadrantsJpeg, sizeof(kQuadrantsJpeg), 16, 129, image) == DecodeFailure::Unfit,
        "impossible targets are refused");
}

struct FakeLoader : images::IPictureLoader {
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<std::string> calls;
  bool hold = false;
  bool succeed = true;
  bool aborted = false;

  bool load(const std::string& url, int width, int height, RemoteImage& out) override {
    std::unique_lock<std::mutex> lock(mutex);
    calls.push_back(url + " " + std::to_string(width) + "x" + std::to_string(height));
    changed.notify_all();
    changed.wait(lock, [&] { return !hold || aborted; });
    if (aborted || !succeed) return false;
    out.pixels.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < out.pixels.size(); ++i) out.pixels[i] = 0x112233;
    out.width = width;
    out.height = height;
    return true;
  }
  void abort() override {
    std::lock_guard<std::mutex> lock(mutex);
    aborted = true;
    changed.notify_all();
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    hold = false;
    changed.notify_all();
  }
  std::size_t count() {
    std::lock_guard<std::mutex> lock(mutex);
    return calls.size();
  }
};

void testStore() {
  std::atomic<int64_t> now{1000};
  const auto clock = [&] { return now.load(); };
  RemoteImage image;
  {
    FakeLoader loader;
    images::RemoteImageStore store(loader, clock);
    check(store.get("https://a/1.jpg", 16, 16, image) == RemoteState::kFailed, "nothing is fetched before start");
    store.start();
    loader.hold = true;
    const uint32_t before = store.generation();
    check(store.get("https://a/1.jpg", 16, 16, image) == RemoteState::kPending, "a new picture is pending");
    check(until([&] { return loader.count() == 1; }), "the worker fetches it");
    for (int i = 0; i < 5; ++i)
      check(store.get("https://a/1.jpg", 16, 16, image) == RemoteState::kPending, "still pending");
    loader.release();
    check(until([&] { return store.generation() != before; }), "settling moves the generation on");
    check(store.get("https://a/1.jpg", 16, 16, image) == RemoteState::kReady && image.width == 16 &&
              image.pixels.size() == 256 && image.pixels[0] == 0x112233,
          "a settled picture is handed out");
    check(loader.count() == 1, "one fetch however often it is asked for");
    check(store.get("https://a/1.jpg", 52, 16, image) == RemoteState::kPending, "another size is another picture");
    check(until([&] { return loader.count() == 2; }) && loader.calls[1] == "https://a/1.jpg 52x16",
          "the other size is fetched at that size");
  }
  {
    FakeLoader loader;
    loader.succeed = false;
    images::RemoteImageStore store(loader, clock);
    store.start();
    uint32_t seen = store.generation();
    store.get("https://a/bad.jpg", 8, 8, image);
    check(until([&] { return store.generation() != seen; }), "a failure settles too");
    check(store.get("https://a/bad.jpg", 8, 8, image) == RemoteState::kFailed, "a failed picture reports so");
    now += 29999;
    check(store.get("https://a/bad.jpg", 8, 8, image) == RemoteState::kFailed && loader.count() == 1,
          "no retry before 30 s");
    now += 1;
    seen = store.generation();
    check(store.get("https://a/bad.jpg", 8, 8, image) == RemoteState::kPending, "asked after 30 s, it is fetched again");
    check(until([&] { return store.generation() != seen; }) && loader.count() == 2, "second attempt runs");
    now += 30000;
    check(store.get("https://a/bad.jpg", 8, 8, image) == RemoteState::kFailed, "the second backoff is longer");
    now += 90000;
    seen = store.generation();
    loader.succeed = true;
    check(store.get("https://a/bad.jpg", 8, 8, image) == RemoteState::kPending, "after 2 min it is tried again");
    check(until([&] { return store.generation() != seen; }) &&
              store.get("https://a/bad.jpg", 8, 8, image) == RemoteState::kReady,
          "and can succeed then");
  }
  {
    FakeLoader loader;
    images::StoreLimits limits;
    const std::string base = "https://a/";
    limits.budgetBytes = 3 * (16 * 16 * 4 + base.size() + 1);
    images::RemoteImageStore store(loader, clock, limits);
    store.start();
    for (char name : {'1', '2', '3', '4'}) {
      const uint32_t seen = store.generation();
      store.get(base + name, 16, 16, image);
      check(until([&] { return store.generation() != seen; }), "budget fixture settles");
    }
    check(store.get(base + '4', 16, 16, image) == RemoteState::kReady &&
              store.get(base + '2', 16, 16, image) == RemoteState::kReady,
          "recent pictures stay");
    check(store.get(base + '1', 16, 16, image) == RemoteState::kPending,
          "the least recently asked for picture made room");
  }
  {
    FakeLoader loader;
    images::StoreLimits limits;
    limits.budgetBytes = 16 * 16 * 4 + 2 * std::string("https://a/a").size();
    images::RemoteImageStore store(loader, clock, limits);
    store.start();
    uint32_t seen = store.generation();
    store.get("https://a/a", 16, 16, image);
    check(until([&] { return store.generation() != seen; }), "the first picture settles");
    loader.hold = true;
    store.get("https://a/b", 16, 16, image);
    check(until([&] { return loader.count() == 2; }), "the second is on its way");
    check(store.get("https://a/a", 16, 16, image) == RemoteState::kReady, "the first is asked for meanwhile");
    seen = store.generation();
    loader.release();
    check(until([&] { return store.generation() != seen; }), "the second settles");
    check(store.get("https://a/b", 16, 16, image) == RemoteState::kReady && loader.count() == 2,
          "a picture that just arrived is not the one that makes room");
  }
  {
    FakeLoader loader;
    loader.succeed = false;
    images::StoreLimits limits;
    limits.backoffMs[0] = limits.backoffMs[1] = limits.backoffMs[2] = 50;
    images::RemoteImageStore store(loader, [] {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now().time_since_epoch()).count();
    }, limits);
    store.start();
    uint32_t seen = store.generation();
    store.get("https://a/flaky", 8, 8, image);
    check(until([&] { return store.generation() != seen; }), "the failure settles");
    seen = store.generation();
    check(until([&] { return store.generation() != seen; }) && loader.count() == 1,
          "the end of a backoff moves the generation on by itself");
    check(store.get("https://a/flaky", 8, 8, image) == RemoteState::kPending &&
              until([&] { return loader.count() == 2; }),
          "and asking then fetches it again");
  }
  {
    FakeLoader loader;
    loader.hold = true;
    images::StoreLimits limits;
    limits.maxWaiting = 2;
    images::RemoteImageStore store(loader, clock, limits);
    store.start();
    store.get("https://a/0", 8, 8, image);
    check(until([&] { return loader.count() == 1; }), "the worker is busy");
    for (char name : {'1', '2', '3'}) store.get(std::string("https://a/") + name, 8, 8, image);
    loader.release();
    check(until([&] { return loader.count() == 3; }), "waiting pictures are fetched");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(loader.count() == 3, "beyond the waiting limit nothing was queued");
    const uint32_t seen = store.generation();
    check(store.get("https://a/3", 8, 8, image) == RemoteState::kPending, "the refused one is asked for again");
    check(until([&] { return store.generation() != seen && loader.count() == 4; }), "and fetched then");
  }
  {
    FakeLoader loader;
    loader.hold = true;
    images::RemoteImageStore store(loader, clock);
    store.start();
    store.get("https://a/slow", 8, 8, image);
    check(until([&] { return loader.count() == 1; }), "slow fetch runs");
    const auto started = std::chrono::steady_clock::now();
    store.stop();
    check(std::chrono::steady_clock::now() - started < std::chrono::seconds(1), "stop ends a running fetch");
    check(store.get("https://a/other", 8, 8, image) == RemoteState::kFailed, "a stopped store fetches nothing");
  }
}

void testHttp() {
  httplib::Server server;
  std::atomic<bool> release{false};
  const std::string jpeg(reinterpret_cast<const char*>(kQuadrantsJpeg), sizeof(kQuadrantsJpeg));
  server.Get("/cover.jpg", [&](const auto&, auto& r) { r.set_content(jpeg, "image/jpeg"); });
  server.Get("/missing", [](const auto&, auto& r) { r.status = 404; });
  server.Get("/moved", [](const auto&, auto& r) { r.set_redirect("/cover.jpg"); });
  server.Get("/text", [](const auto&, auto& r) { r.set_content("hello", "text/plain"); });
  server.Get("/huge", [](const auto&, auto& r) {
    r.set_content(std::string(images::HttpPictureLoader::kMaxBytes + 1, 'x'), "image/jpeg");
  });
  server.Get("/slow", [&](const auto&, auto& r) {
    while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    r.set_content("late", "image/jpeg");
  });
  const int port = server.bind_to_any_port("127.0.0.1");
  check(port > 0, "bind loopback fixture");
  std::thread listener([&] { server.listen_after_bind(); });
  check(until([&] { return server.is_running(); }), "server starts");
  const std::string origin = "http://127.0.0.1:" + std::to_string(port);

  std::string origin2, target;
  check(net::splitUrl("HTTP://Host:8/a?b=1#frag", origin2, target) && origin2 == "http://Host:8" &&
            target == "/a?b=1",
        "a URL splits into origin and target");
  check(net::splitUrl("https://host?q", origin2, target) && origin2 == "https://host" && target == "/?q",
        "a query without a path gets one");
  check(!net::splitUrl("ftp://host/a", origin2, target) && !net::splitUrl("http:///a", origin2, target),
        "other URLs do not split");

  std::vector<std::string> log;
  images::HttpPictureLoader loader([&](const std::string& line) { log.push_back(line); });
  RemoteImage image;
  check(loader.load(origin + "/cover.jpg", 16, 16, image) && image.width == 16 && near(at(image, 2, 2), 0xFF0000),
        "a picture is fetched and fitted");
  check(loader.load(origin + "/moved", 16, 16, image), "redirects are followed");
  check(log.empty(), "success logs nothing");

  check(!loader.load(origin + "/missing?token=hidden", 16, 16, image) && log.size() == 1 &&
            log[0] == "picture from 127.0.0.1:" + std::to_string(port) + ": HTTP 404",
        "a failure is logged with the host only");
  check(!loader.load("http://user:secret@127.0.0.1:" + std::to_string(port) + "/x", 16, 16, image) &&
            log.back().rfind("picture from 127.0.0.1:" + std::to_string(port) + ": ", 0) == 0 &&
            log.back().find("secret") == std::string::npos,
        "credentials in a URL stay out of the log");
  check(!loader.load(origin + "/text", 16, 16, image) && log.back().find("not a JPEG") != std::string::npos,
        "content that is no picture is reported");
  check(!loader.load(origin + "/huge", 16, 16, image) && log.back().find("larger than 1 MB") != std::string::npos,
        "an oversized body is refused");
  check(!loader.load("http://127.0.0.1:1/x.jpg", 16, 16, image) && log.back().find("no answer") != std::string::npos,
        "an unreachable host is reported");

  httplib::Server other;
  other.Get("/slow", [&](const auto&, auto& r) {
    while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    r.set_content("late", "image/jpeg");
  });
  const int otherPort = other.bind_to_any_port("127.0.0.1");
  check(otherPort > 0, "bind second fixture");
  std::thread otherListener([&] { other.listen_after_bind(); });
  check(until([&] { return other.is_running(); }), "second server starts");
  server.Get("/hop", [&](const auto&, auto& r) {
    r.set_redirect("http://localhost:" + std::to_string(otherPort) + "/slow");
  });

  net::HttpGet get;
  std::string body;
  auto started = std::chrono::steady_clock::now();
  check(get.fetch(origin + "/slow", {1024, 300, false}, body).failure == net::HttpGet::Failure::Network &&
            std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "the time limit covers a server that never answers");

  const std::size_t logged = log.size();
  std::thread aborter([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    loader.abort();
  });
  started = std::chrono::steady_clock::now();
  check(!loader.load(origin + "/hop", 16, 16, image), "an aborted fetch fails");
  check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "abort ends a running fetch, also behind a redirect to another host");
  aborter.join();
  check(!loader.load(origin + "/cover.jpg", 16, 16, image), "after abort every fetch fails");
  check(log.size() == logged, "aborting logs nothing");
  release = true;
  server.stop();
  listener.join();
  other.stop();
  otherListener.join();
}

}

int main() {
  testFit();
  testDecoder();
  testStore();
  testHttp();
  std::puts("remote images: ok");
  return 0;
}

// Tc002Activation against a fake Post: nothing without consent, when it sends, what the body
// holds, which answers end it, which ones it retries and how long it waits, the marker that keeps
// a later start quiet, a shutdown that ends a request still running, and /api/v1/activation.
#include "../../support.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "platform/tc002/runtime/Tc002Activation.h"
#include "platform/tc002/runtime/Tc002ActivationApi.h"

using namespace awtrix;
namespace fs = std::filesystem;
using awtrix::test::check;
using State = Tc002Activation::State;
using Consent = Tc002Activation::Consent;

namespace {

const std::string kId(64, 'b');

struct FakeHub {
  std::mutex mutex;
  std::vector<int> answers;
  std::vector<std::string> bodies;
  int post(const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex);
    bodies.push_back(body);
    const int status = answers.empty() ? 204 : answers.front();
    if (!answers.empty()) answers.erase(answers.begin());
    return status;
  }
  std::size_t calls() {
    std::lock_guard<std::mutex> lock(mutex);
    return bodies.size();
  }
};

struct Rig {
  fs::path marker;
  fs::path consent;
  FakeHub hub;
  std::unique_ptr<Tc002Activation> activation;
  explicit Rig(const fs::path& dir, std::vector<int> answers = {}, bool agree = true)
      : marker(dir / "activation"), consent(dir / "activation-consent") {
    hub.answers = std::move(answers);
    make();
    if (agree) activation->setConsent(true);
  }
  void make() {
    activation = std::make_unique<Tc002Activation>(
        kId, Tc002Activation::Paths{consent.string(), marker.string()},
        [this](const std::string& body) { return hub.post(body); }, [] {});
  }
  // Ticks at nowMs until the attempt it starts has been answered.
  void settle(int64_t nowMs, bool online = true) {
    activation->tick(nowMs, online);
    for (int i = 0; i < 2000 && activation->state() == State::Sending; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      activation->tick(nowMs, online);
    }
  }
};

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

void waitsForConsent(const fs::path& dir) {
  fs::create_directories(dir / "consent");
  Rig rig(dir / "consent", {}, false);
  check(rig.activation->consent() == Consent::Unasked, "unasked without a consent file");
  rig.settle(0);
  check(rig.hub.calls() == 0, "nothing goes out unasked");
  check(rig.activation->setConsent(false) && read(rig.consent) == "no\n", "a no is kept");
  rig.settle(10);
  check(rig.hub.calls() == 0, "nothing goes out after a no");
  rig.make();
  check(rig.activation->consent() == Consent::Declined, "a later start keeps the no");
  check(rig.activation->setConsent(true) && read(rig.consent) == "yes\n", "a yes is kept");
  rig.settle(20);
  check(rig.hub.calls() == 1, "the yes sends the report");
  rig.make();
  check(rig.activation->consent() == Consent::Given, "a later start keeps the yes");
}

void unwritableConsent(const fs::path& dir) {
  Rig rig(dir / "no-such-directory", {}, false);
  check(!rig.activation->setConsent(true), "an answer the file cannot take is refused");
  check(rig.activation->consent() == Consent::Unasked, "and changes nothing");
  rig.settle(0);
  check(rig.hub.calls() == 0, "so nothing goes out");
}

void api(const fs::path& dir) {
  fs::create_directories(dir / "api");
  Rig rig(dir / "api", {}, false);
  Tc002ActivationApi api(*rig.activation);
  std::string body;
  check(api.handle("GET", "/api/v1/other", "", body) == 0, "other paths are not its own");
  check(api.handle("GET", "/api/v1/activation", "", body) == 200 && body == "{\"consent\":null}", "unasked reads null");
  check(api.handle("PUT", "/api/v1/activation", "{\"consent\":\"yes\"}", body) == 422, "a string is refused");
  check(api.handle("PUT", "/api/v1/activation", "{", body) == 400, "broken JSON is refused");
  check(api.handle("POST", "/api/v1/activation", "{\"consent\":true}", body) == 405, "POST is not allowed");
  check(rig.activation->consent() == Consent::Unasked, "refusals change nothing");
  check(api.handle("PUT", "/api/v1/activation", "{\"consent\":false}", body) == 200 && body == "{\"consent\":false}",
        "a no is answered with the new state");
  check(api.handle("PUT", "/api/v1/activation", "{\"consent\":true}", body) == 200 && body == "{\"consent\":true}",
        "a yes is answered with the new state");
}

void reportsOnceOnline(const fs::path& dir) {
  fs::create_directories(dir / "once");
  Rig rig(dir / "once");
  rig.settle(0, false);
  check(rig.hub.calls() == 0 && rig.activation->state() == State::Pending, "nothing goes out offline");
  rig.settle(10);
  check(rig.hub.calls() == 1 && rig.hub.bodies[0] == "{\"id\":\"" + kId + "\"}", "online, the id and only the id goes out");
  check(rig.activation->state() == State::Done, "204 ends it");
  check(read(rig.marker) == kId + "\n", "the marker keeps the id");
  rig.settle(100000000);
  check(rig.hub.calls() == 1, "nothing more in this run");
  rig.make();
  check(rig.activation->state() == State::Done, "a later start with the marker is done at once");
  rig.settle(0);
  check(rig.hub.calls() == 1, "and sends nothing");
}

void otherMarkerSendsAgain(const fs::path& dir) {
  fs::create_directories(dir / "other");
  std::ofstream(dir / "other/activation") << std::string(64, 'c') << "\n";
  Rig rig(dir / "other");
  check(rig.activation->state() == State::Pending, "a marker with another id does not count");
  rig.settle(0);
  check(rig.hub.calls() == 1 && read(rig.marker) == kId + "\n", "the id is sent and replaces it");
}

void retriesWithGrowingDelays(const fs::path& dir) {
  fs::create_directories(dir / "retry");
  Rig rig(dir / "retry", {0, 503, 429, 408, 0, 0, 204});
  int64_t now = 0;
  rig.settle(now);
  check(rig.hub.calls() == 1 && rig.activation->state() == State::Pending, "no answer leaves it pending");
  for (std::size_t attempt = 0; attempt < 6; ++attempt) {
    const int64_t delay = Tc002Activation::kRetryMs[std::min(attempt, Tc002Activation::kRetryMs.size() - 1)];
    rig.settle(now + delay - 1);
    check(rig.hub.calls() == attempt + 1, "no attempt before the delay ran out");
    now += delay;
    rig.settle(now);
    check(rig.hub.calls() == attempt + 2, "the next attempt once it did");
  }
  check(rig.activation->state() == State::Done, "the seventh answer, 204, ends it");
}

void refusalStops(const fs::path& dir) {
  fs::create_directories(dir / "refused");
  Rig rig(dir / "refused", {422});
  rig.settle(0);
  check(rig.activation->state() == State::Refused, "422 is a refusal");
  rig.settle(100000000);
  check(rig.hub.calls() == 1, "a refusal is not retried");
  check(!fs::exists(rig.marker), "and leaves no marker");
}

void unwritableMarker(const fs::path& dir) {
  fs::create_directories(dir / "marker");
  Rig rig(dir / "marker");
  fs::remove_all(rig.marker);
  fs::create_directories(rig.marker);
  rig.settle(0);
  check(rig.activation->state() == State::Done, "a marker that cannot be written still ends this run");
}

void shutdownEndsARunningRequest() {
  std::mutex mutex;
  std::condition_variable woken;
  bool interrupted = false, started = false;
  const fs::path consent = fs::temp_directory_path() / ("awtrix-activation-consent-" + std::to_string(::getpid()));
  std::ofstream(consent) << "yes\n";
  auto activation = std::make_unique<Tc002Activation>(
      kId, Tc002Activation::Paths{consent.string(), "/nonexistent/activation"},
      [&](const std::string&) {
        std::unique_lock<std::mutex> lock(mutex);
        started = true;
        woken.notify_all();
        woken.wait(lock, [&] { return interrupted; });
        return 0;
      },
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        interrupted = true;
        woken.notify_all();
      });
  activation->tick(0, true);
  {
    std::unique_lock<std::mutex> lock(mutex);
    woken.wait(lock, [&] { return started; });
  }
  activation.reset();
  fs::remove(consent);
  check(interrupted, "destruction interrupts the request and waits for its thread");
}

}

int main() {
  const char* base = std::getenv("TMPDIR");
  std::string buffer = std::string(base && *base ? base : "/tmp") + "/awtrix-tc002-activation-XXXXXX";
  if (!::mkdtemp(&buffer[0])) return 2;
  const fs::path dir = buffer;
  waitsForConsent(dir);
  unwritableConsent(dir);
  api(dir);
  reportsOnceOnline(dir);
  otherMarkerSendsAgain(dir);
  retriesWithGrowingDelays(dir);
  refusalStops(dir);
  unwritableMarker(dir);
  shutdownEndsARunningRequest();
  fs::remove_all(dir);
  return awtrix::test::finish("tc002-activation");
}

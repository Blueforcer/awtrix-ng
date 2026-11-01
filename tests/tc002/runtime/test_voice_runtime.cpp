#include "../../support.h"

#include <stdlib.h>

#include <filesystem>
#include <string>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/tc002/voice/VoiceRuntime.h"

using namespace awtrix;
using awtrix::test::check;
using awtrix::test::require;
using tc002::StreamControl;
using tc002::voice::VoiceRuntime;

namespace {
namespace fs = std::filesystem;

struct Speaker final : tc002::voice::VoiceSpeaker {
  bool available() const override { return true; }
  posix::Hold claimVoiceChannel() override { return posix::Hold([] {}); }
  posix::Hold holdMusic() override { return posix::Hold([] {}); }
  bool quiet() const override { return true; }
  bool play(const std::string&) override { return true; }
  void stop() override {}
  bool finished() const override { return true; }
  bool heard() const override { return true; }
};

fs::path privateFolder(const fs::path& root) {
  fs::create_directories(root);
  return root / "voice-private";
}

struct Rig {
  const fs::path folder;
  Speaker speaker;
  std::vector<StreamControl> microphone;
  bool startable = true;
  unsigned starts = 0;
  VoiceRuntime runtime;

  Rig(const fs::path& root, bool wakeWord, bool hooks = true)
      : folder(privateFolder(root)),
        runtime(folder.string(), speaker,
                [this](StreamControl control) {
                  microphone.push_back(control);
                  return true;
                },
                wakeWord,
                hooks ? VoiceRuntime::Hooks{[] { return std::vector<std::string>{"Chat", "Talk"}; },
                                            [this] {
                                              ++starts;
                                              return startable;
                                            }}
                      : VoiceRuntime::Hooks{}) {}

  httplib::Response call(const std::string& method, const std::string& path, const std::string& body = {},
                         bool trusted = true, const std::string& origin = "http://clock.local") {
    httplib::Request request;
    request.method = method;
    request.path = path;
    request.body = body;
    if (trusted) request.headers.emplace("X-Awtrix-Voice", "1");
    request.headers.emplace("Origin", origin);
    request.headers.emplace("Host", "clock.local");
    httplib::Response response;
    check(runtime.handle(request, response), "the runtime answers " + method + " " + path);
    check(response.get_header_value("Cache-Control") == "no-store", "never cached: " + path);
    return response;
  }
  unsigned probes() const {
    unsigned n = 0;
    for (const auto& control : microphone) n += control.operation == StreamControl::Probe;
    return n;
  }
};

int sent(const httplib::Response& response) { return response.status == -1 ? 200 : response.status; }

bool has(const httplib::Response& response, const std::string& text) {
  return response.body.find(text) != std::string::npos;
}

void startFromTheApi(const fs::path& root) {
  Rig rig(root, true);
  const std::string start = "/api/v1/voice/start";
  auto response = rig.call("GET", start);
  check(sent(response) == 405 && has(response, "methodNotAllowed") && has(response, "allowed: POST"),
        "only POST starts: " + response.body);
  response = rig.call("POST", start, {}, false);
  check(sent(response) == 403 && has(response, "forbiddenOrigin") && rig.starts == 0,
        "without X-Awtrix-Voice nothing starts");
  response = rig.call("POST", start, {}, true, "http://other.local");
  check(sent(response) == 403 && has(response, "forbiddenOrigin") && rig.starts == 0,
        "another origin starts nothing");
  rig.startable = false;
  response = rig.call("POST", start);
  check(sent(response) == 409 && has(response, "voiceUnavailable") && has(response, "assistant not ready") &&
            rig.starts == 1,
        "an assistant that does not start answers 409: " + response.body);
  rig.startable = true;
  response = rig.call("POST", start);
  check(sent(response) == 200 && response.body == R"({"ok":true})" && rig.starts == 2,
        "a started assistant answers 200: " + response.body);

  Rig bare(root / "bare", false, false);
  response = bare.call("POST", start);
  check(sent(response) == 409 && has(response, "voiceUnavailable"), "a runtime without hooks starts nothing");

  httplib::Request other;
  other.method = "GET";
  other.path = "/api/v1/voices";
  httplib::Response ignored;
  check(!rig.runtime.handle(other, ignored), "other paths are not the runtime's");
}

void settingsAnswer(const fs::path& root) {
  Rig rig(root, true);
  auto response = rig.call("GET", "/api/v1/voice");
  check(sent(response) == 200 && has(response, R"("wakeWordAvailable":true)") &&
            has(response, R"("assistantApps":["Chat","Talk"])") && has(response, R"("state":"offline")"),
        "the settings name the wake word and the apps that can answer: " + response.body);
  Rig bare(root / "bare", false, false);
  response = bare.call("GET", "/api/v1/voice");
  check(has(response, R"("wakeWordAvailable":false)") && has(response, R"("assistantApps":[])"),
        "without a wake word or hooks: " + response.body);
}

void wakeWordKeepsTheConnection(const fs::path& root) {
  Rig rig(root, true);
  auto response =
      rig.call("POST", "/api/v1/voice", R"({"url":"http://127.0.0.1:1","token":"secret","enabled":true})");
  require(sent(response) == 200, "the connection settings are stored: " + response.body);
  rig.runtime.tick(1000, true);
  check(rig.probes() == 1 && !has(rig.call("GET", "/api/v1/voice"), R"("state":"offline")"),
        "Voice connects and probes the microphone");
  response = rig.call("POST", "/api/v1/voice", R"({"wakeWord":true})");
  rig.runtime.tick(1001, true);
  check(sent(response) == 200 && rig.probes() == 1 &&
            !has(rig.call("GET", "/api/v1/voice"), R"("state":"offline")"),
        "switching the wake word keeps the connection");
  response = rig.call("POST", "/api/v1/voice", R"({"pipeline":"p"})");
  check(sent(response) == 200 && has(rig.call("GET", "/api/v1/voice"), R"("state":"offline")"),
        "any other change drops it");
  rig.runtime.tick(1002, true);
  check(rig.probes() == 2, "and connects again at once");
  response = rig.call("POST", "/api/v1/voice", R"({"wakeWord":true})", false);
  check(sent(response) == 403 && has(response, "forbiddenOrigin"), "settings need X-Awtrix-Voice too");
}

}

int main() {
  char name[] = "/tmp/voice-runtime-test-XXXXXX";
  require(::mkdtemp(name) != nullptr, "a test folder");
  const fs::path root = name;
  startFromTheApi(root / "api");
  settingsAnswer(root / "get");
  wakeWordKeepsTheConnection(root / "post");
  fs::remove_all(root);
  return awtrix::test::finish("tc002-voice-runtime");
}

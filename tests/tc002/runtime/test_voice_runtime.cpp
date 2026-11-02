#include "../../support.h"

#include <stdlib.h>

#include <filesystem>
#include <string>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/tc002/voice/VoiceApi.h"

using namespace awtrix;
using awtrix::test::check;
using awtrix::test::require;
using tc002::StreamControl;
using tc002::voice::VoiceApi;
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
  bool startable = true, listening = false;
  std::string appProblem;
  unsigned starts = 0;
  VoiceRuntime runtime;
  VoiceApi api;

  Rig(const fs::path& root, bool wakeWord)
      : folder(privateFolder(root)),
        runtime(folder.string(), speaker,
                [this](StreamControl control) {
                  microphone.push_back(control);
                  return true;
                }),
        api(runtime, wakeWord,
            VoiceApi::Hooks{[] { return std::vector<std::string>{"Chat", "Talk"}; },
                            [this] {
                              ++starts;
                              return startable;
                            },
                            [this] { return listening; },
                            [this] { return appProblem.c_str(); }}) {}

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
    check(api.handle(request, response), "the API answers " + method + " " + path);
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

  httplib::Request other;
  other.method = "GET";
  other.path = "/api/v1/voices";
  httplib::Response ignored;
  check(!rig.api.handle(other, ignored), "other paths are not the API's");
}

void settingsAnswer(const fs::path& root) {
  Rig rig(root, true);
  auto response = rig.call("GET", "/api/v1/voice");
  check(sent(response) == 200 && has(response, R"("wakeWordAvailable":true)") &&
            has(response, R"("assistantApps":["Chat","Talk"])") && has(response, R"("state":"offline")"),
        "the settings name the wake word and the apps that can answer: " + response.body);
  Rig bare(root / "bare", false);
  response = bare.call("GET", "/api/v1/voice");
  check(has(response, R"("wakeWordAvailable":false)"), "without a wake word: " + response.body);
  check(has(rig.call("GET", "/api/v1/voice"), R"("wakeListening":false)"), "it does not listen yet");
  rig.listening = true;
  check(has(rig.call("GET", "/api/v1/voice"), R"("wakeListening":true)"), "and says so once it does");
}

void offConnectsNothing(const fs::path& root) {
  Rig rig(root, true);
  auto state = [&rig] { return rig.call("GET", "/api/v1/voice"); };
  check(has(state(), R"("assistant":"off")") && has(state(), R"("enabled":false)") && rig.runtime.off() &&
            !rig.runtime.homeAssistant() && rig.runtime.assistantApp().empty(),
        "a fresh clock has no assistant: " + state().body);
  rig.runtime.tick(1000, true);
  check(rig.probes() == 0 && has(state(), R"("state":"offline")"), "and connects nowhere");
  require(sent(rig.call("POST", "/api/v1/voice", R"({"url":"http://127.0.0.1:1","token":"secret"})")) == 200,
          "address and token are stored");
  rig.runtime.tick(1001, true);
  check(rig.probes() == 0 && rig.runtime.off() && !rig.runtime.ready(), "saving them starts no connection");
  require(sent(rig.call("POST", "/api/v1/voice", R"({"assistant":"ha"})")) == 200, "Home Assistant is chosen");
  rig.runtime.tick(1002, true);
  check(rig.probes() == 1 && has(state(), R"("enabled":true)") && rig.runtime.homeAssistant() && !rig.runtime.off(),
        "choosing Home Assistant connects");
  require(sent(rig.call("POST", "/api/v1/voice", R"({"enabled":false})")) == 200, "enabled false is taken");
  rig.runtime.tick(1003, true);
  check(rig.probes() == 1 && has(state(), R"("state":"offline")") && has(state(), R"("assistant":"off")") &&
            has(state(), R"("tokenSet":true)") && has(state(), R"("url":"http://127.0.0.1:1")"),
        "off closes the connection and keeps the data: " + state().body);
  require(sent(rig.call("POST", "/api/v1/voice", R"({"enabled":true})")) == 200, "enabled true is taken");
  rig.runtime.tick(1004, true);
  check(rig.probes() == 2, "and it connects again without a new address");
  require(sent(rig.call("POST", "/api/v1/voice", R"({"clearToken":true})")) == 200, "the token goes");
  rig.runtime.tick(1005, true);
  check(rig.probes() == 2 && has(state(), R"("assistant":"ha")") && has(state(), R"("state":"offline")") &&
            has(state(), R"("error":"")"),
        "without a token Home Assistant stays the target and nothing connects: " + state().body);
  require(sent(rig.call("POST", "/api/v1/voice", R"({"assistant":"app:Chat"})")) == 200, "an app is chosen");
  check(!rig.runtime.off() && !rig.runtime.homeAssistant() && rig.runtime.assistantApp() == "Chat", "an app is no off");
  Rig bare(root / "bare", true);
  require(sent(bare.call("POST", "/api/v1/voice", R"({"assistant":"ha"})")) == 200,
          "Home Assistant can be chosen before it has an address");
  bare.runtime.tick(1000, true);
  check(bare.probes() == 0, "and waits for one");
}

void statusNamesTheChosenAssistant(const fs::path& root) {
  Rig rig(root, true);
  rig.appProblem = "appMissing";
  auto state = [&rig] { return rig.call("GET", "/api/v1/voice"); };
  check(has(state(), R"("state":"offline")") && has(state(), R"("error":"")") && has(state(), R"("lastError":"")"),
        "off has no state to report and asks no app: " + state().body);
  require(sent(rig.call("POST", "/api/v1/voice", R"({"assistant":"app:Chat"})")) == 200, "an app is chosen");
  check(has(state(), R"("state":"error")") && has(state(), R"("error":"appMissing")"),
        "an app that cannot answer says why: " + state().body);
  rig.appProblem.clear();
  check(has(state(), R"("state":"ready")") && has(state(), R"("error":"")"),
        "an app that can answer is ready: " + state().body);
  rig.runtime.tick(1000, true);
  check(rig.probes() == 0, "and an app needs no connection");
  rig.appProblem = "appOff";
  require(sent(rig.call("POST", "/api/v1/voice", R"({"assistant":"ha"})")) == 200, "Home Assistant is chosen");
  check(has(state(), R"("state":"offline")") && has(state(), R"("error":"")"),
        "with Home Assistant the app does not count: " + state().body);
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
  response = rig.call("POST", "/api/v1/voice", R"({"wakeSensitivity":"low"})");
  rig.runtime.tick(1001, true);
  check(sent(response) == 200 && rig.probes() == 1 && rig.runtime.wakeSensitivity() == tc002::wake::WakeSensitivity::Low &&
            !has(rig.call("GET", "/api/v1/voice"), R"("state":"offline")") &&
            has(rig.call("GET", "/api/v1/voice"), R"("wakeSensitivity":"low")"),
        "changing the sensitivity keeps it too");
  response = rig.call("POST", "/api/v1/voice", R"({"wakeSensitivity":"loud"})");
  check(sent(response) == 422 && has(response, R"("field":"wakeSensitivity")"), "an unknown level is refused: " + response.body);
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
  offConnectsNothing(root / "off");
  statusNamesTheChosenAssistant(root / "status");
  wakeWordKeepsTheConnection(root / "post");
  fs::remove_all(root);
  return awtrix::test::finish("tc002-voice-runtime");
}

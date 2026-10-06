#include "../../support.h"
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "platform/tc002/voice/AssistSession.h"
#include "platform/tc002/voice/StreamClock.h"
using namespace awtrix::tc002;
using namespace awtrix::tc002::voice;
constexpr auto check = awtrix::test::require;
struct Rig {
  std::vector<std::pair<std::string, bool>> sent;
  std::vector<StreamControl> controls;
  bool disconnected = false;
  std::string tts;
  AssistSession session{{[&](std::string s, bool b) {
                           sent.emplace_back(s, b);
                           return true;
                         },
                         [&] { disconnected = true; },
                         [&](StreamControl c) {
                           controls.push_back(c);
                           return true;
                         },
                         [&](const std::string& s) {
                           tts = s;
                           return true;
                         },
                         [] {}}};
  explicit Rig(std::string device = "") {
    session.connecting({true, "http://ha:8123", "secret", "", device}, 0);
    session.receive({1, StreamEvent::Kind::Support, 0, 0, 0, true, {}, {}}, 0);
    session.text(R"({"type":"auth_required"})", 1);
    check(sent.back().first.find("secret") != std::string::npos,
          "authenticate on challenge");
    session.text(R"({"type":"auth_ok"})", 2);
    session.text(
        R"({"id":1,"type":"result","success":true,"result":{"preferred_pipeline":"p","pipelines":[{"id":"p","stt_engine":"stt","tts_engine":"tts"}]}})",
        3);
    check(session.ready(), "pipeline validated");
    disconnected = false;
  }
  void begin() { check(session.begin(100), "begin"); }
  void stt(bool acknowledge = true, int64_t now = 110) {
    session.text(
        R"({"id":2,"type":"event","event":{"type":"run-start","data":{"runner_data":{"stt_binary_handler_id":7}}}})",
        now);
    session.text(
        R"({"id":2,"type":"event","event":{"type":"stt-start","data":{}}})",
        now + 1);
    if (acknowledge)
      session.receive(
          {2, StreamEvent::Kind::Started, 0, 0, now + 2, false, {}, {}},
          now + 2);
  }
};
int main() {
  {
    int64_t local = 0;
    check(localizeCaptureTime(90000000, 90000030, 1030, local) && local == 1000,
          "daemon boot clock maps into young runtime clock by transport age");
    check(localizeCaptureTime(90000000, 90000030, 500030, local) &&
              local == 500000,
          "clock mapping remains correct after runtime restart");
    check(!localizeCaptureTime(90000000, 90000251, 1030, local),
          "clock translation does not hide stale audio");
    check(!localizeCaptureTime(90000031, 90000030, 1030, local),
          "future host capture rejected");
  }
  {
    Rig r;
    check(r.session.begin(100), "start while MCU acknowledgement pending");
    r.stt(false);
    check(r.session.state() == AssistSession::State::Starting,
          "listening indicator waits for actual MCU capture");
    r.session.receive({2, StreamEvent::Kind::Started, 0, 0, 120, false, {}, {}},
                      120);
    check(r.session.state() == AssistSession::State::Listening,
          "listening indicator starts after both sides are ready");
  }
  {
    char directory[] = "/tmp/awtrix-voice-config-XXXXXX";
    check(::mkdtemp(directory) != nullptr, "private test directory");
    ConfigStore store(directory);
    ConfigStore::Rejection error;
    check(store.load(), "new private store");
    check(store.update(
              R"({"url":"http://ha:8123","token":"secret","enabled":true})",
              error),
          "save config");
    check(store.publicJson().find("secret") == std::string::npos,
          "token never returned");
    struct stat info {};
    check(
        ::stat((std::string(directory) + "/voice.json").c_str(), &info) == 0 &&
            (info.st_mode & 0777) == 0600,
        "token file private");
    check(!store.update(R"({"url":"http://attacker:8123"})", error) &&
              error.field == "token",
          "retained token cannot change origin");
    check(!store.update(R"({"url":"http://ha:8123/lovelace"})", error) &&
              error.field == "url",
          "an address with a path names the url field");
    check(!store.update(R"({"enabled":true,"clearToken":true})", error) &&
              error.field == "enabled",
          "voice cannot be on without a token");
    check(store.update(R"({"pipeline":"p1"})", error) &&
              store.get().token == "secret",
          "ordinary edits retain token");
    check(store.update(R"({"device":"0123456789abcdef0123456789abcdef"})",
                       error) &&
              store.publicJson().find("0123456789abcdef") != std::string::npos,
          "device saved and shown");
    check(!store.update(R"({"device":"../x"})", error) &&
              error.field == "device",
          "device is a plain identifier");
    check(store.erase(), "factory reset removes secret");
    ConfigStore reloaded(directory);
    check(reloaded.load() && !reloaded.get().enabled &&
              reloaded.get().token.empty(),
          "reset stays disabled after restart");
    ::rmdir(directory);
  }
  {
    Rig r;
    r.begin();
    check(r.controls.back().operation == StreamControl::Probe,
          "prepare HA before opening microphone");
    r.session.tick(1500);
    check(
        !r.disconnected && r.session.state() == AssistSession::State::Starting,
        "slow cloud startup does not consume or overflow audio buffer");
    r.stt(true, 1600);
    check(r.controls.back().operation == StreamControl::Start,
          "HA readiness admits capture");
  }
  {
    Rig r;
    r.begin();
    r.session.endInput(150);
    check(r.disconnected && !r.session.busy(),
          "ending input during HA preparation cancels immediately");
    r.stt();
    check(r.controls.back().operation == StreamControl::Probe,
          "late HA readiness cannot capture after cancelling");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    std::vector<int16_t> pcm(24, -1234);
    r.session.receive({2, StreamEvent::Kind::Audio, 0, 0, 115, false, pcm, {}},
                      115);
    check(r.sent.back().second && r.sent.back().first.size() == 49 &&
              r.sent.back().first[0] == 7,
          "handler plus exact PCM");
    check(static_cast<unsigned char>(r.sent.back().first[1]) == 46 &&
              static_cast<unsigned char>(r.sent.back().first[2]) == 251,
          "signed little endian");
    r.session.endInput(150);
    check(r.controls.back().operation == StreamControl::Stop,
          "ending input stops capture");
    r.session.receive({2, StreamEvent::Kind::Ended, 24, 0, 160, false, {}, {}},
                      160);
    check(r.sent.back().second && r.sent.back().first == std::string(1, 7),
          "clean terminal sends EOF");
    r.session.text(
        R"({"id":2,"type":"event","event":{"type":"tts-end","data":{"tts_output":{"url":"/api/tts_proxy/answer.wav"}}}})",
        200);
    check(r.tts == "/api/tts_proxy/answer.wav", "nested tts output");
    r.session.text(
        R"({"id":2,"type":"event","event":{"type":"run-end","data":{}}})", 201);
    check(!r.session.ready(), "wait for audible completion");
    r.session.playbackDone(true, 250);
    check(r.session.ready(), "ready after playback");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.receive({2,
                       StreamEvent::Kind::Audio,
                       24,
                       0,
                       120,
                       false,
                       std::vector<int16_t>(24),
                       {}},
                      120);
    check(r.disconnected && r.session.state() == AssistSession::State::Error,
          "gap cancels pipeline without EOF");
    for (const auto& p : r.sent) check(!p.second, "broken capture never sent");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.receive({2,
                       StreamEvent::Kind::Audio,
                       0,
                       0,
                       100,
                       false,
                       std::vector<int16_t>(24),
                       {}},
                      351);
    check(r.disconnected, "stale transported audio fails closed");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.tick(270);
    check(r.controls.back().operation == StreamControl::Keepalive,
          "active capture lease renewed");
    r.session.cancel();
    check(r.controls.back().operation == StreamControl::Stop,
          "cancel revokes microphone");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.text(
        R"({"id":2,"type":"event","event":{"type":"stt-end","data":{}}})", 130);
    const auto count = r.sent.size();
    r.session.receive({2,
                       StreamEvent::Kind::Audio,
                       0,
                       0,
                       131,
                       false,
                       std::vector<int16_t>(24),
                       {}},
                      131);
    r.session.receive({2, StreamEvent::Kind::Ended, 24, 0, 132, false, {}, {}},
                      132);
    check(r.sent.size() == count && !r.disconnected,
          "VAD closes handler; tail and EOF are not sent to a removed handler");
  }
  {
    Rig r;
    r.session.receive({1, StreamEvent::Kind::Support, 0, 0, 10, false, {}, {}},
                      10);
    check(!r.session.ready(), "unavailable microphone prevents PTT");
    r.session.tick(2100);
    check(r.controls.back().operation == StreamControl::Probe,
          "capability probe retried");
    r.session.receive({1, StreamEvent::Kind::Support, 0, 0, 2101, true, {}, {}},
                      2101);
    check(r.session.ready(),
          "late MCU discovery recovers without HA reconnect");
  }
  {
    Rig r;
    r.begin();
    check(r.sent.back().first.find("device_id") == std::string::npos,
          "no device, no device_id");
  }
  {
    Rig r("0123456789abcdef0123456789abcdef");
    r.begin();
    check(r.sent.back().first.find(
              R"("device_id":"0123456789abcdef0123456789abcdef")") !=
              std::string::npos,
          "run carries the device for its area");
  }
  WebSocket::Endpoint e;
  std::string path;
  check(parseOrigin("https://[::1]:8123", e) && e.tls && e.host == "::1",
        "IPv6 origin");
  for (const auto* bad : {"http://user@ha", "http://ha/path", "http://ha:0",
                          "http://ha?x", "http://ha\r\nX:bad", "file://ha", "http://ha:65536",
                          "http://ha:8123@evil", "http://@ha", "http://[1::2::3]", "http://ha#"})
    check(!parseOrigin(bad, e), "reject malformed origin");
  Config c{true, "http://ha:8123", "secret", "", ""};
  check(ttsTarget(c, "http://ha:8123/api/tts_proxy/a.wav", path),
        "same origin TTS");
  for (const auto* bad : {"//other/a", "http://evil/api/tts_proxy/a",
                          "/api/states", "https://ha:8123/api/tts_proxy/a",
                          "http://ha:8123@evil/api/tts_proxy/a", "http://@ha:8123/api/tts_proxy/a"})
    check(!ttsTarget(c, bad, path), "TTS cannot escape configured origin");
  std::puts(
      "Assist session: authentication, pipeline, PCM, EOF, playback, failure "
      "and URL boundaries passed");
}

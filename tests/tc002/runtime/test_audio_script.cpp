#include "../../support.h"
#include <memory>
#include <string>

#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/tc002/audio/AudioScripting.h"

using namespace awtrix;

namespace {
constexpr auto check = awtrix::test::check;

struct Rig {
  script::ScriptServices services;
  AppRegistry apps;
  tc002::AudioScripting audio;
  script::ScriptHost host;
  script::ExtensionHost extensions;
  Canvas canvas{52, 16};

  Rig(tc002::AudioScripting::Beat beat = {}, tc002::AudioScripting::Pitch pitch = {},
      tc002::AudioScripting::Sound sound = {})
      : audio(std::move(beat), std::move(pitch), [] { return 77; }, std::move(sound)),
        host(apps, services, {}, {}), extensions(host, {&audio}) {}

  std::string run(const std::string& expression) {
    check(host.set("AudioProbe", "class App\nvar result\ndef draw() self.result = " + expression +
        " end\ndef check() return self.result end\nend\nreturn App()"), "the audio app installs");
    auto* app = static_cast<script::ScriptApp*>(apps.find("AudioProbe"));
    if (!app) return {};
    RenderCtx context;
    context.nowMs = 1234;
    app->render(canvas, context);
    std::string result;
    app->callCheckForTest(result);
    return result;
  }
};

void absentWithoutExtension() {
  for (const char* call : {"sound.effect('tone')", "sound.beat()", "music.pitch()"}) {
    script::ScriptServices services;
    AppRegistry apps;
    script::ScriptHost host(apps, services, {}, {});
    check(host.set("MissingAudio", std::string("class App\ndef draw() ") + call + " end\nend\nreturn App()"),
          "an app using an unavailable member remains reportable");
    Canvas canvas(32, 8);
    RenderCtx context;
    if (auto* app = apps.find("MissingAudio")) app->render(canvas, context);
    check(!host.errorOf("MissingAudio").empty(), "TC002 audio members require their extension");
  }
}

void frameTimeAndOwner() {
  int64_t beatAt = 0, pitchAt = 0;
  std::string owner, payload;
  script::SoundAction action = script::SoundAction::Play;
  Rig rig([&](int64_t now, double& beat) { beatAt = now; beat = 3.5; return true; },
          [&](int64_t now) { pitchAt = now; return 440.5f; },
          [&](script::SoundAction kind, const std::string& json, const std::string& caller, std::string&) {
            action = kind; owner = caller; payload = json; return 1;
          });
  check(rig.run("str(sound.beat()) + '/' + str(music.pitch()) + '/' + str(sound.effect('tone'))") ==
        "3.5/440.5/true", "the existing audio module names expose the extension results");
  check(beatAt == 1234 && pitchAt == 1234, "analysis follows the rendering frame time");
  check(owner == "AudioProbe" && action == script::SoundAction::Effect && payload == "\"tone\"",
        "an effect carries its caller and sound specification");
}

void unavailableAndInvalid() {
  {
    Rig rig;
    check(rig.run("str(sound.beat()) + '/' + str(music.pitch() == 0.0) + '/' + str(sound.effect('tone'))") ==
          "nil/true/false", "missing audio services retain their empty results");
  }
  {
    Rig rig({}, {}, [](script::SoundAction, const std::string&, const std::string&, std::string& error) {
      error = "invalid sound"; return -1;
    });
    rig.run("sound.effect({})");
    check(!rig.host.errorOf("AudioProbe").empty(), "an invalid effect raises in the calling script");
  }
}
}

int main() {
  absentWithoutExtension();
  frameTimeAndOwner();
  unavailableAndInvalid();
  return awtrix::test::finish("TC002 audio script");
}

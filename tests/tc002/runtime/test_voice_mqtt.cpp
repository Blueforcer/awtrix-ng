#include "../../support.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/api/JsonReader.h"
#include "core/mqtt/ByteSink.h"
#include "platform/tc002/voice/VoiceMqtt.h"

using namespace awtrix;
namespace voice = awtrix::tc002::voice;

constexpr auto check = awtrix::test::require;

std::string text(api::JsonReader r, const char* key) {
  std::string out;
  api::memberValue(r, key).appendString(out);
  return out;
}

int main() {
  int starts = 0;
  std::string result = "untouched";
  check(!voice::handleMqtt("cmd/apps/next", [&] { return ++starts > 0; }, result) &&
            result == "untouched" && starts == 0,
        "another topic is not ours");
  check(!voice::handleMqtt("cmd/voice/start/result", [&] { return ++starts > 0; }, result) &&
            starts == 0,
        "our own reply is no command");

  check(voice::handleMqtt("cmd/voice/start", [&] { return ++starts > 0; }, result) &&
            starts == 1 && result == "{\"ok\":true}",
        "a started request answers ok");
  check(voice::handleMqtt("cmd/voice/start", [&] { ++starts; return false; }, result) && starts == 2,
        "a refused request is answered");
  const api::JsonReader refused{std::string_view(result)};
  bool ok = true;
  check(api::memberValue(refused, "ok").asBool(ok) && !ok, "refusal is not ok");
  const api::JsonReader error = api::memberValue(refused, "error");
  check(text(error, "code") == "unavailable" && text(error, "message") == "voice not ready",
        "refusal says why");

  ha::DiscoveryContext ctx;
  ctx.prefix = "awtrix_1";
  ctx.uid = "uid1";
  ctx.platformEntities = &voice::kStartButton;
  ctx.platformEntityCount = 1;
  ha::StringSink sink;
  ha::emit(ctx, sink);
  const api::JsonReader button =
      api::memberValue(api::memberValue(api::JsonReader{std::string_view(sink.str)}, "cmps"), "assist");
  check(text(button, "p") == "button", "Home Assistant shows a button");
  check(text(button, "cmd_t") == std::string("~/") + voice::kStartTopic, "the button sends the start command");
  check(text(button, "uniq_id") == "uid1_assist", "the button has its own id");

  std::puts("voice mqtt: start command, refusal, foreign topics and the Home Assistant button passed");
}

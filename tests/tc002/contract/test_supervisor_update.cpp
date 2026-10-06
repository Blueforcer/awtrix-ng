#include "../../support.h"
#include <cstdio>
#include <string>

#include "platform/tc002/contract/SupervisorProtocol.h"

using namespace awtrix::tc002;

namespace {
int& failures = awtrix::test::failures();
using awtrix::test::check;

const std::string kSha(64, 'a');

bool decodes(const std::string& json, SupervisorMessage& message) { return decodeSupervisorMessage(json, message); }

std::string ready(const std::string& fields) { return "{\"v\":2,\"type\":\"updateReady\"," + fields + "}"; }

std::string hello(const std::string& fields) { return "{\"v\":2,\"type\":\"hello\",\"version\":\"1.2.3\"" + fields + "}"; }
}

int main() {
  SupervisorMessage message;

  UpdateReady sent{"/tmp/awtrix-update/package.awup", "1.2.0-g1234567890ab-abcdef012345", 1758800000, kSha};
  const std::string wire = encodeUpdateReady(sent);
  check(wire == "{\"v\":2,\"type\":\"updateReady\",\"package\":\"/tmp/awtrix-update/package.awup\","
                "\"release\":\"1.2.0-g1234567890ab-abcdef012345\",\"counter\":1758800000,\"payloadSha256\":\"" +
                    kSha + "\"}", "update ready wire form");
  check(decodes(wire, message) && message.type == MessageType::UpdateReady &&
        message.updateReady.package == sent.package && message.updateReady.release == sent.release &&
        message.updateReady.counter == 1758800000 && message.updateReady.payloadSha256 == kSha,
        "update ready round trip");
  check(encodeUpdateReady({"relative/package.awup", "r", 1, kSha}).empty(), "relative package refused by encoder");
  check(encodeUpdateReady({"/p", "", 1, kSha}).empty(), "empty release refused by encoder");
  check(encodeUpdateReady({"/p", "a/b", 1, kSha}).empty(), "release with slash refused by encoder");
  check(encodeUpdateReady({"/p", "r", 0, kSha}).empty(), "counter 0 refused by encoder");
  check(encodeUpdateReady({"/p", "r", 1ULL << 63, kSha}).empty(), "counter beyond JSON integers refused");
  check(encodeUpdateReady({"/p", "r", 1, std::string(64, 'A')}).empty(), "uppercase digest refused");
  check(encodeUpdateReady({"/" + std::string(256, 'p'), "r", 1, kSha}).empty(), "long package path refused");

  const std::string good = "\"package\":\"/p\",\"release\":\"r\",\"counter\":7,\"payloadSha256\":\"" + kSha + "\"";
  check(decodes(ready(good), message) && message.updateReady.counter == 7, "minimal update ready");
  check(!decodes(ready("\"package\":\"/p\",\"release\":\"r\",\"counter\":7"), message), "digest required");
  check(!decodes(ready("\"package\":\"p\",\"release\":\"r\",\"counter\":7,\"payloadSha256\":\"" + kSha + "\""), message),
        "absolute package required");
  check(!decodes(ready("\"package\":\"/p\",\"release\":\"r\",\"counter\":0,\"payloadSha256\":\"" + kSha + "\""), message),
        "counter 0 refused");
  check(!decodes(ready("\"package\":\"/p\",\"release\":\"r\",\"counter\":-3,\"payloadSha256\":\"" + kSha + "\""), message),
        "negative counter refused");
  check(!decodes(ready("\"package\":\"/p\",\"release\":\"r\",\"counter\":1.5,\"payloadSha256\":\"" + kSha + "\""), message),
        "fractional counter refused");
  check(!decodes(ready("\"package\":\"/p\",\"release\":\"r\",\"counter\":\"7\",\"payloadSha256\":\"" + kSha + "\""), message),
        "string counter refused");
  check(!decodes(ready("\"package\":\"/p\",\"release\":\"r\",\"counter\":7,\"payloadSha256\":\"" + kSha.substr(1) + "\""),
                 message), "short digest refused");
  check(!decodes(ready("\"package\":\"/p\\u0000x\",\"release\":\"r\",\"counter\":7,\"payloadSha256\":\"" + kSha + "\""),
                 message), "NUL in the package path refused");
  check(message.type == MessageType::Invalid, "failed decode leaves an invalid message");

  check(decodes(encodeHello("1.2.3"), message) && message.type == MessageType::Hello && !message.hasUpdate,
        "runtime hello carries no status");
  UpdateStatus update{"failed", "1.2.0-gabc", "the release slot was not written", 5177344};
  const std::string status = encodeHello("1.2.3", update);
  check(status == "{\"v\":2,\"type\":\"hello\",\"version\":\"1.2.3\","
                  "\"update\":{\"state\":\"failed\",\"release\":\"1.2.0-gabc\","
                  "\"error\":\"the release slot was not written\",\"capacity\":5177344}}",
        "supervisor hello wire form");
  check(decodes(status, message) && message.hasUpdate && message.update.state == "failed" &&
        message.update.release == update.release && message.update.error == update.error &&
        message.update.capacity == 5177344, "supervisor hello round trip");
  check(decodes(encodeHello("1.2.3", {"idle", "", "", 0}), message) && message.update.state == "idle" &&
        message.update.capacity == 0, "idle status without a slot round trip");
  check(encodeHello("1.2.3", {"", "", "", 0}).empty(), "empty update state refused by encoder");
  check(encodeHello("1.2.3", {"Boot_Pending", "", "", 0}).empty(), "update state is lower-case words");
  check(encodeHello("1.2.3", {"idle", "", "", 1ULL << 63}).empty(), "capacity beyond JSON integers refused");
  check(!encodeHello("1.2.3", {"boot-pending", "", std::string(256, 'e'), 1}).empty(), "256-byte error fits");

  check(decodes(hello(",\"update\":{\"state\":\"applying\",\"release\":\"r\",\"error\":\"\",\"capacity\":4096}"), message) &&
        message.hasUpdate && message.update.state == "applying" && message.update.capacity == 4096, "update alone");
  check(!decodes(hello(",\"update\":{\"state\":\"x y\",\"release\":\"\",\"error\":\"\",\"capacity\":0}"), message),
        "state charset");
  check(!decodes(hello(",\"update\":{\"state\":\"idle\",\"release\":\"\",\"capacity\":0}"), message), "error required");
  check(!decodes(hello(",\"update\":{\"state\":\"idle\",\"release\":\"\",\"error\":\"\"}"), message), "capacity required");
  check(!decodes(hello(",\"update\":{\"state\":\"idle\",\"release\":\"\",\"error\":\"\",\"capacity\":-1}"), message),
        "negative capacity refused");
  check(!decodes(hello(",\"update\":{\"state\":\"idle\",\"release\":\"\",\"error\":\"\",\"capacity\":\"1\"}"), message),
        "capacity is a number");
  check(!decodes(hello(",\"update\":null"), message), "update is an object");
  check(message.type == MessageType::Invalid, "failed hello decode leaves an invalid message");

  if (failures) return 1;
  std::puts("supervisor update protocol: ok");
  return 0;
}

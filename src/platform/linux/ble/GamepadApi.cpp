#include "platform/linux/ble/GamepadApi.h"

#include <cstdint>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix::ble {
namespace {

constexpr const char* kPath = "/api/v1/gamepad";
constexpr const char* kDevicePrefix = "/api/v1/gamepad/";
constexpr const char* kRemotePath = "/api/v1/gamepad/remote";
constexpr const char* kRemotePrefix = "/api/v1/gamepad/remote/";
constexpr const char* kDefaultName = "Phone";

std::size_t characters(const std::string& text) {
  std::size_t n = 0;
  for (const char c : text)
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
  return n;
}

// The device slot a /api/v1/gamepad/{id} path names, or 0.
int deviceId(const std::string& path) {
  const std::size_t at = std::char_traits<char>::length(kDevicePrefix);
  if (path.size() != at + 1 || path[at] < '1' || path[at] > '0' + kGamepadPlayers) return 0;
  return path[at] - '0';
}

// The session a /api/v1/gamepad/remote/{session} path names, or 0.
uint32_t sessionId(const std::string& path) {
  const std::size_t at = std::char_traits<char>::length(kRemotePrefix);
  if (path.size() <= at || path.size() > at + 10 || path[at] == '0') return 0;
  uint64_t id = 0;
  for (std::size_t i = at; i < path.size(); ++i) {
    if (path[i] < '0' || path[i] > '9') return 0;
    id = id * 10 + static_cast<uint64_t>(path[i] - '0');
  }
  return id <= UINT32_MAX ? static_cast<uint32_t>(id) : 0;
}

}

int GamepadApi::handle(const std::string& method, const std::string& path, const std::string& request,
                       std::string& body) const {
  if (!remote_) return 0;
  if (path == kPath) {
    if (method == "GET") return list(body);
    body = api::errorJson("methodNotAllowed", "allowed: GET");
    return 405;
  }
  if (path == kRemotePath) {
    if (method == "POST") return startRemote(request, body);
    body = api::errorJson("methodNotAllowed", "allowed: POST");
    return 405;
  }
  if (path.rfind(kRemotePrefix, 0) == 0) return endRemote(method, path, body);
  if (!bluetooth_ || path.rfind(kDevicePrefix, 0) != 0) return 0;
  if (path == "/api/v1/gamepad/pair") {
    if (method == "POST") return pair(body);
    body = api::errorJson("methodNotAllowed", "allowed: POST");
    return 405;
  }
  const int id = deviceId(path);
  if (!id) {
    body = api::errorJson("notFound", "no such gamepad");
    return 404;
  }
  if (method != "DELETE") {
    body = api::errorJson("methodNotAllowed", "allowed: DELETE");
    return 405;
  }
  forget_(id);
  body = R"({"ok":true})";
  return 200;
}

int GamepadApi::list(std::string& body) const {
  const auto devices = bluetooth_ ? bluetooth_->devices() : GamepadDevices{};
  body.clear();
  api::JsonWriter w(body);
  w.beginObject().key("devices").beginArray();
  for (int i = 0; i < kGamepadPlayers; ++i) {
    const auto& device = devices[i];
    w.beginObject().key("id").value(i + 1).key("state").value(gamepadStateName(device.state))
        .key("name").value(device.name).key("address").value(device.address).key("player");
    if (device.player) w.value(device.player); else w.null();
    w.endObject();
  }
  w.endArray().key("remotes").beginArray();
  for (const auto& phone : remote_->sessions())
    w.beginObject().key("session").value(static_cast<long long>(phone.id)).key("name").value(phone.name)
        .key("player").value(phone.player).endObject();
  w.endArray().endObject();
  return 200;
}

int GamepadApi::pair(std::string& body) const {
  const auto result = pair_();
  switch (result.status) {
    case GamepadPairStatus::Started:
      body = "{\"ok\":true,\"id\":" + std::to_string(result.id) + "}";
      return 200;
    case GamepadPairStatus::Full:
      body = api::errorJson("gamepadsFull", "no free slot");
      return 409;
    case GamepadPairStatus::Unavailable:
      break;
  }
  body = api::errorJson("unavailable", "not available");
  return 503;
}

// A player no one plays, else one without a phone, else Player 1.
int GamepadApi::freePlayer() const {
  bool phone[kGamepadPlayers + 1] = {}, pad[kGamepadPlayers + 1] = {};
  for (const auto& session : remote_->sessions()) phone[session.player] = true;
  if (bluetooth_)
    for (const auto& device : bluetooth_->devices())
      if (device.state == GamepadStatus::Ready && device.player >= 1 && device.player <= kGamepadPlayers)
        pad[device.player] = true;
  for (int p = 1; p <= kGamepadPlayers; ++p)
    if (!phone[p] && !pad[p]) return p;
  for (int p = 1; p <= kGamepadPlayers; ++p)
    if (!phone[p]) return p;
  return 1;
}

int GamepadApi::startRemote(const std::string& request, std::string& body) const {
  std::string name;
  long long player = 0;
  if (request.find_first_not_of(" \t\r\n") != std::string::npos) {
    api::JsonReader atName, atPlayer;
    if (!api::JsonReader(request).isObject() || !api::readMembers(request, {{"name", &atName}, {"player", &atPlayer}})) {
      body = api::errorJson("invalidJson", "invalid JSON");
      return 400;
    }
    if (api::present(atPlayer) &&
        (!atPlayer.isInteger() || !atPlayer.asLong(player) || player < 1 || player > kGamepadPlayers)) {
      body = api::errorJson("invalidPlayer", "must be 1..2", "player");
      return 400;
    }
    if (api::present(atName) &&
        (!atName.isString() || !atName.appendString(name) || characters(name) > RemoteGamepad::kMaxNameChars)) {
      body = api::errorJson("invalidName", "invalid name", "name");
      return 400;
    }
  }
  if (name.empty()) name = kDefaultName;
  if (!player) player = freePlayer();
  RemoteGamepad::Token token;
  const uint32_t session = remote_->start(name, static_cast<int>(player), token);
  if (!session) {
    body = api::errorJson("unavailable", "not available");
    return 503;
  }
  static const char kHex[] = "0123456789abcdef";
  std::string hex;
  for (const uint8_t b : token) {
    hex += kHex[b >> 4];
    hex += kHex[b & 15];
  }
  body.clear();
  api::JsonWriter(body).beginObject().key("port").value(static_cast<int>(RemoteGamepad::kPort))
      .key("token").value(hex).key("player").value(player).key("session").value(static_cast<long long>(session))
      .endObject();
  return 200;
}

int GamepadApi::endRemote(const std::string& method, const std::string& path, std::string& body) const {
  const uint32_t id = sessionId(path);
  if (!id) {
    body = api::errorJson("notFound", "no such session");
    return 404;
  }
  if (method != "DELETE") {
    body = api::errorJson("methodNotAllowed", "allowed: DELETE");
    return 405;
  }
  if (!remote_->end(id)) {
    body = api::errorJson("notFound", "no such session");
    return 404;
  }
  body = R"({"ok":true})";
  return 200;
}

}

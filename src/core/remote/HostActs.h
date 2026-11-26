#pragma once

#include <functional>
#include <string>
#include <utility>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"

namespace awtrix {

class CoreEngine;

namespace remote {

class RemoteApps;

constexpr const char* kActPath = "/api/v1/scripthost/act";

class HostActs {
 public:
  using Press = std::function<void(int button)>;

  HostActs(CoreEngine& engine, RemoteApps& apps) : engine_(engine), apps_(apps) {}
  void setPress(Press press) { press_ = std::move(press); }

  api::HttpResult request(const std::string& method, bool keyOk, const std::string& body);

 private:
  api::HttpResult run(const std::string& body);
  api::HttpResult sound(const std::string& owner, const api::JsonReader& action, const api::JsonReader& sound);

  CoreEngine& engine_;
  RemoteApps& apps_;
  Press press_;
};

}
}

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>

#include "core/DeviceCapabilities.h"
#include "core/Settings.h"
#include "core/api/JsonWriter.h"
#include "core/remote/HostModules.h"
#include "platform/linux/layout/ScriptLayouts.h"

namespace awtrix {

class LinuxCapabilities {
 public:
  LinuxCapabilities(bool bluetooth, bool scripting, bool voice, const layout::Limits* layouts = nullptr)
      : modules_{{{"ble", bluetooth}, {"gamepad", scripting}, {"oauth", scripting}, {"secret", scripting},
                   {"crypto", scripting}, {"tcp", scripting}, {"layout", layouts && scripting},
                   {"voice", voice}, {}}},
        layouts_(layouts), gamepadRemote_(scripting) {}

  // The script host of an ESP32 clock offers scripts what the clock has, plus the network modules.
  void hostOfClock() {
    for (auto& module : modules_) module.present = module.present && hostModule(module.name);
    layouts_ = nullptr;
    gamepadRemote_ = false;
  }

  // A clock whose scripts run on a script host offers them what the host adds, as an ESP32 clock
  // does, and none of its own modules.
  void scriptsOnHost() {
    for (auto& module : modules_) module.present = hostModule(module.name);
    layouts_ = nullptr;
    gamepadRemote_ = false;
  }

  bool clockFaces = false;
  bool mqttTls = false;
  bool bootSound = false;
  bool enlargeApps = false;
  bool activation = false;

  const DeviceCapabilities::Extra* needs() const { return modules_.data(); }

  void write(api::JsonWriter& writer) const {
    for (const auto& module : modules_)
      if (module.name && module.present) writer.member(module.name, true);
    if (layouts_) {
      const auto& limits = *layouts_;
      writer.key("layouts").beginObject().member("version", 1).key("limits").beginObject()
          .member("regions", limits.regions).member("scrollers", limits.scrollers).member("assets", limits.assets)
          .member("chartPoints", limits.chartPoints).member("textBytes", limits.textBytes)
          .member("preparedBytes", limits.preparedBytes).member("scriptHandles", script::ScriptLayouts::kMaxHandles)
          .member("scriptHandlesPerScript", script::ScriptLayouts::kMaxPerScript).endObject().endObject();
    }
    if (gamepadRemote_) writer.member("gamepadRemote", true);
    if (clockFaces) {
      writer.key("clockFaces").beginArray();
      for (const char* name : kClockFaceNames) writer.value(name);
      writer.endArray();
    }
    if (mqttTls) writer.member("mqttTls", true);
    if (bootSound) writer.member("bootSound", true);
    if (enlargeApps) writer.member("enlargeApps", true);
    if (activation) writer.member("activation", true);
  }

 private:
  static bool hostModule(const char* name) {
    return name && std::any_of(std::begin(remote::kHostModules), std::end(remote::kHostModules),
                               [name](const char* host) { return std::strcmp(host, name) == 0; });
  }

  std::array<DeviceCapabilities::Extra, 9> modules_;
  const layout::Limits* layouts_;
  bool gamepadRemote_;
};

}

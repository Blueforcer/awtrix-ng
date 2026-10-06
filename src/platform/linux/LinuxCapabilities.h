#pragma once

#include <array>

#include "core/DeviceCapabilities.h"
#include "core/Settings.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/layout/ScriptLayouts.h"

namespace awtrix {

class LinuxCapabilities {
 public:
  LinuxCapabilities(bool bluetooth, bool scripting, const layout::Limits* layouts = nullptr)
      : modules_{{{"ble", bluetooth}, {"gamepad", scripting}, {"oauth", scripting},
                   {"crypto", scripting}, {"tcp", scripting}, {"layout", layouts && scripting}, {}}},
        layouts_(layouts), gamepadRemote_(scripting) {}

  bool voice = false;
  bool clockFaces = false;
  bool mqttTls = false;
  bool bootSound = false;
  bool enlargeApps = false;

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
    if (voice) writer.member("voice", true);
    if (clockFaces) {
      writer.key("clockFaces").beginArray();
      for (const char* name : kClockFaceNames) writer.value(name);
      writer.endArray();
    }
    if (mqttTls) writer.member("mqttTls", true);
    if (bootSound) writer.member("bootSound", true);
    if (enlargeApps) writer.member("enlargeApps", true);
  }

 private:
  std::array<DeviceCapabilities::Extra, 7> modules_;
  const layout::Limits* layouts_;
  bool gamepadRemote_;
};

}
